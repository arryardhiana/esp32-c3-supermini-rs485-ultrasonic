#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ModbusRTU.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "config.h"

// ── Hardware Serial ───────────────────────────────────────────────────────────
// ESP32-C3 hanya punya UART0 dan UART1 (tidak ada UART2).
// Serial (USB-CDC) terpisah dari UART0, jadi UART0 aman dipakai untuk RS485.
static HardwareSerial SerialSensor(1);   // A0221AU  → UART1
static HardwareSerial SerialRS485(0);    // Modbus RTU → UART0

// ── Objek Global ──────────────────────────────────────────────────────────────
static LiquidCrystal_I2C *lcd = nullptr;
static ModbusRTU  mb;
static WebServer  webServer(80);
static Preferences prefs;

// ── Status & Data ─────────────────────────────────────────────────────────────
static bool     lcdOk    = false;
static bool     sensorOk = false;
static uint8_t  slaveId  = MODBUS_DEFAULT_SLAVE_ID;
static uint16_t hreg[HREG_COUNT];

// Konfigurasi runtime — diload dari NVS saat boot, default ke nilai compile-time config.h
static uint16_t rtTankH   = TANK_HEIGHT_MM;
static uint16_t rtTankL   = TANK_LENGTH_MM;   // panjang dalam tangki (mm)
static uint16_t rtTankW   = TANK_WIDTH_MM;    // lebar dalam tangki (mm)
static uint16_t rtSensOff = SENSOR_OFFSET_MM;
static uint8_t  rtCalN    = CAL_TABLE_SIZE;
static CalPoint rtCal[5];

// ── Scheduler ─────────────────────────────────────────────────────────────────
struct Task { uint32_t interval; uint32_t last; };
static bool due(Task &t) {
    if ((uint32_t)(millis() - t.last) >= t.interval) {
        t.last = millis();
        return true;
    }
    return false;
}
static Task taskLcd = { 500, 0};
static Task taskHb  = {5000, 0};

// ── Buffer Sensor UART ────────────────────────────────────────────────────────
static uint8_t  sBuf[SENSOR_FRAME_LEN];
static uint8_t  sIdx        = 0;
static uint32_t sLastFrameMs = 0;

// ── Median Filter 7-sampel ────────────────────────────────────────────────────
static uint16_t medBuf[7];
static uint8_t  medIdx   = 0;   // posisi tulis circular
static uint8_t  medCount = 0;   // jumlah sampel valid (0–7)

// ─────────────────────────────────────────────────────────────────────────────
// Fungsi Bantu
// ─────────────────────────────────────────────────────────────────────────────

// Median buffer circular — salin + sort bubble (n ≤ 7)
static uint16_t hitungMedian() {
    if (medCount == 0) return 0;
    uint16_t tmp[7];
    memcpy(tmp, medBuf, medCount * sizeof(uint16_t));
    for (uint8_t i = 0; i < medCount - 1; i++)
        for (uint8_t j = i + 1; j < medCount; j++)
            if (tmp[i] > tmp[j]) { uint16_t t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
    return tmp[medCount / 2];
}

// Interpolasi linear dari tabel kalibrasi runtime rtCal[] (jarak_mm → level_mm)
static uint16_t interpolasi(uint16_t jarak) {
    const uint8_t n = rtCalN;
    if (n == 0) return 0;
    if (jarak <= rtCal[0].jarak)   return rtCal[0].level;
    if (jarak >= rtCal[n-1].jarak) return rtCal[n-1].level;
    for (uint8_t i = 1; i < n; i++) {
        if (jarak <= rtCal[i].jarak) {
            uint16_t j0 = rtCal[i-1].jarak, j1 = rtCal[i].jarak;
            uint16_t l0 = rtCal[i-1].level, l1 = rtCal[i].level;
            int32_t  dl = (int32_t)(l1 - l0) * (int32_t)(jarak - j0);
            return (uint16_t)((int32_t)l0 + dl / (int32_t)(j1 - j0));
        }
    }
    return rtCal[n-1].level;
}

// Perbarui semua holding register Modbus dari state terkini
static void updateRegisters() {
    uint16_t jarak = hreg[HREG_DISTANCE];
    uint16_t levMm = interpolasi(jarak);
    uint16_t levCm = levMm / 10;
    uint16_t levPct = (rtTankH > 0)
                      ? (uint16_t)((uint32_t)levMm * 1000 / rtTankH) : 0;
    // Volume = P × L × level (mm³) / 100.000 → satuan 0,1 L
    // Pakai uint64_t untuk hindari overflow (maks ~9999³ ≈ 10¹²)
    uint64_t mm3   = (uint64_t)rtTankL * rtTankW * levMm;
    uint64_t vol64 = mm3 / 100000ULL;
    uint16_t volDl = (vol64 > 65535ULL) ? 65535 : (uint16_t)vol64;

    hreg[HREG_LEVEL_CM]        = levCm;
    hreg[HREG_LEVEL_PCT]      = levPct;
    hreg[HREG_VOLUME_DL]      = volDl;
    hreg[HREG_STATUS]         = (uint16_t)((sensorOk ? 0x01 : 0)
                                           | (lcdOk  ? 0x02 : 0)
                                           | 0x04);   // bit 2 = WiFi AP selalu aktif
    hreg[HREG_SLAVE_ID]       = slaveId;
    hreg[HREG_RESERVED]       = 0;
    hreg[HREG_FW_MAJOR]       = FW_VER_MAJOR;
    hreg[HREG_FW_MINOR_PATCH] = (uint16_t)(FW_VER_MINOR * 100 + FW_VER_PATCH);

    for (uint8_t i = 0; i < HREG_COUNT; i++) mb.Hreg(i, hreg[i]);
}

// Tampilkan data di LCD (update scheduled, hindari clear() berlebihan)
static void updateLcd() {
    if (!lcdOk || !lcd) return;
    char buf[17];
    // Baris 0: "BBM xxxxcm  xxx%"
    snprintf(buf, sizeof(buf), "BBM%4dcm   %3d%%",
             hreg[HREG_LEVEL_CM],
             hreg[HREG_LEVEL_PCT] / 10);
    lcd->setCursor(0, 0);
    lcd->print(buf);
    // Baris 1: "Vol xxx.xL  OK  "
    snprintf(buf, sizeof(buf), "Vol%4d.%dL  %-3s",
             hreg[HREG_VOLUME_DL] / 10,
             hreg[HREG_VOLUME_DL] % 10,
             sensorOk ? "OK" : "ERR");
    lcd->setCursor(0, 1);
    lcd->print(buf);
}

// ── Web Handlers ──────────────────────────────────────────────────────────────

// Dashboard HTML — dark theme, polling JSON /api/data setiap 1.5 detik
static const char DASHBOARD_HTML[] =
R"html(<!doctype html><html lang="id"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Dashboard &middot; Fuel Genset</title><style>
:root{--bg:#0f1419;--card:#1a2129;--mut:#7d8896;--txt:#e6edf3;--ok:#3fb950;--warn:#d29922;--dan:#f85149;--line:#2a333d}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--txt);font-family:-apple-system,Segoe UI,Roboto,sans-serif;padding:16px;max-width:680px;margin:auto}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}
h1{font-size:18px;font-weight:600}
h2{font-size:13px;color:var(--mut);text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--dan);display:inline-block;margin-right:6px;transition:.3s}
.dot.on{background:var(--ok);box-shadow:0 0 8px var(--ok)}
.top{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:10px}
.big{font-size:48px;font-weight:700;line-height:1}
.unit{font-size:18px;color:var(--mut);margin-left:4px}
.bar{height:14px;background:#0d1117;border-radius:8px;overflow:hidden;border:1px solid var(--line)}
.fill{height:100%;width:0;border-radius:8px;transition:width .6s,background .3s}
.sub{color:var(--mut);font-size:13px}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:14px;margin-bottom:14px}
.metric{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px}
.lbl{color:var(--mut);font-size:12px;text-transform:uppercase;letter-spacing:.5px}
.val{font-size:26px;font-weight:600;margin-top:4px}
.badges{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}
.badge{font-size:12px;padding:5px 10px;border-radius:20px;background:#0d1117;border:1px solid var(--line);color:var(--mut)}
.badge.ok{color:var(--ok);border-color:var(--ok)}
.badge.bad{color:var(--dan);border-color:var(--dan)}
a.lnk{font-size:13px;font-weight:600;color:var(--ok);text-decoration:none;border:1px solid var(--ok);padding:6px 12px;border-radius:8px}
footer{text-align:center;color:var(--mut);font-size:11px;margin-top:18px}
</style></head><body>
<header>
<h1>&#9981; Fuel Monitor</h1>
<span style="display:flex;align-items:center;gap:14px">
<span><span class="dot" id="dot"></span><span id="conn" style="font-size:12px;color:var(--mut)">menghubungkan&hellip;</span></span>
<a class="lnk" href="/setup">&#9881; Setup</a>
</span>
</header>
<div class="card"><h2>Level BBM</h2>
<div class="top">
<div><span class="big" id="pct">--</span><span class="unit">%</span></div>
<div class="sub" id="lvcm">-- cm</div>
</div>
<div class="bar"><div class="fill" id="fill"></div></div>
</div>
<div class="grid">
<div class="metric"><div class="lbl">Volume</div>
<div class="val"><span id="vol">--</span><span style="font-size:16px;color:var(--mut)"> L</span></div></div>
<div class="metric"><div class="lbl">Jarak Sensor</div>
<div class="val"><span id="jarak">--</span><span style="font-size:16px;color:var(--mut)"> cm</span></div></div>
</div>
<div class="badges" id="badges"><span class="badge">memuat&hellip;</span></div>
<footer>Fuel Sensor Genset &middot; FW: )html"
FW_VERSION_STR
R"html( &middot; 192.168.4.1</footer>
<script>
let $=i=>document.getElementById(i);
function bdg(ok,name){return'<span class="badge '+(ok?'ok':'bad')+'">'+name+': '+(ok?'OK':'&#x2014;')+'</span>';}
function up(s){let h=s/3600|0,m=s%3600/60|0,x=s%60;return(h?h+'j ':'')+(m?m+'m ':'')+x+'s';}
async function tick(){
  try{
    let d=await(await fetch('/api/data')).json();
    let p=d.pct10/10;
    $('pct').textContent=Math.round(p);
    $('lvcm').textContent=d.cm+' cm';
    $('vol').textContent=Math.round(d.vol10/10);
    $('jarak').textContent=d.jarak/10;
    let f=$('fill');
    f.style.width=Math.max(0,Math.min(100,p))+'%';
    f.style.background=p<20?'linear-gradient(90deg,#f85149,#ff7b72)':p<50?'linear-gradient(90deg,#d29922,#e3b341)':'linear-gradient(90deg,#3fb950,#56d364)';
    $('badges').innerHTML=bdg(d.sensor,'Sensor')+bdg(d.lcd,'LCD')+bdg(d.wifi,'WiFi AP')+'<span class="badge">'+up(d.up)+'</span>';
    $('dot').classList.add('on');$('conn').textContent='terhubung';
  }catch(e){$('dot').classList.remove('on');$('conn').textContent='terputus';}
}
tick();setInterval(tick,1500);
</script></body></html>)html";

static void webHandleRoot() {
    webServer.send_P(200, "text/html", DASHBOARD_HTML);
}

static void webHandleApiData() {
    char json[220];
    snprintf(json, sizeof(json),
        "{\"jarak\":%u,\"cm\":%u,\"pct10\":%u,\"vol10\":%u,"
        "\"up\":%lu,\"id\":%u,\"sensor\":%s,\"lcd\":%s,\"wifi\":true}",
        (unsigned)hreg[HREG_DISTANCE],
        (unsigned)hreg[HREG_LEVEL_CM],
        (unsigned)hreg[HREG_LEVEL_PCT],
        (unsigned)hreg[HREG_VOLUME_DL],
        (unsigned long)(millis() / 1000UL),
        (unsigned)slaveId,
        sensorOk ? "true" : "false",
        lcdOk    ? "true" : "false"
    );
    webServer.sendHeader("Cache-Control", "no-cache");
    webServer.send(200, "application/json", json);
}

static void webHandleApiSetId() {
    if (webServer.hasArg("id")) {
        int newId = webServer.arg("id").toInt();
        if (newId >= 1 && newId <= 247) {
            slaveId = (uint8_t)newId;
            mb.slave(slaveId);
            prefs.begin(NVS_NAMESPACE, false);
            prefs.putUChar(NVS_KEY_SLAVE_ID, slaveId);
            prefs.end();
            Serial.printf("[MODBUS] Slave ID diubah ke %d dan disimpan ke NVS\n", slaveId);
            webServer.send(200, "application/json", "{\"ok\":true}");
            return;
        }
    }
    webServer.send(400, "application/json", "{\"ok\":false}");
}

// Halaman pengaturan — konfigurasi tangki, kalibrasi, Modbus slave ID
static const char SETUP_HTML[] =
R"html(<!doctype html><html lang="id"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pengaturan &middot; Fuel Genset</title><style>
:root{--bg:#0f1419;--card:#1a2129;--mut:#7d8896;--txt:#e6edf3;--ok:#3fb950;--dan:#f85149;--line:#2a333d}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--txt);font-family:-apple-system,Segoe UI,Roboto,sans-serif;padding:16px;max-width:680px;margin:auto}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}
h1{font-size:18px;font-weight:600}
h2{font-size:13px;color:var(--mut);text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
.sub{color:var(--mut);font-size:13px}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:10px}
.row label{color:var(--mut);font-size:13px;min-width:150px}
input[type=number]{font:inherit;width:80px;padding:7px 10px;border-radius:8px;border:1px solid var(--line);background:#0d1117;color:var(--txt)}
.btn{font:inherit;font-weight:600;padding:8px 16px;border-radius:8px;border:none;background:var(--ok);color:#06210e;cursor:pointer}
.btn:active{opacity:.8}
.btn-sm{font-size:12px;padding:5px 10px}
a.lnk{font-size:13px;color:var(--mut);text-decoration:none}
.cr{display:flex;gap:8px;align-items:center;margin-bottom:8px}
.cr .lbl{color:var(--mut);font-size:12px;min-width:60px}
.del{padding:3px 8px;border-radius:6px;border:1px solid var(--dan);background:transparent;color:var(--dan);cursor:pointer;font-size:12px}
footer{text-align:center;color:var(--mut);font-size:11px;margin-top:18px}
</style></head><body>
<header><h1>&#9881; Pengaturan</h1><a class="lnk" href="/">&#8592; Dashboard</a></header>
<div class="card"><h2>Geometri Tangki</h2>
<div class="row"><label>Tinggi Dalam (cm)</label><input id="th" type="number" min="1" max="999"></div>
<div class="row"><label>Panjang Dalam (cm)</label><input id="tl" type="number" min="1" max="999"></div>
<div class="row"><label>Lebar Dalam (cm)</label><input id="tw" type="number" min="1" max="999"></div>
<div class="row"><label>Volume Maks</label><span class="sub" id="vh" style="font-size:15px;font-weight:600">-- L</span></div>
<div class="row"><label>Offset Sensor (cm)</label><input id="so" type="number" min="3" max="50"></div>
<p class="sub">Offset = jarak ujung sensor ke mulut tangki &mdash; min 3 cm (blind zone A0221AU)</p>
<br><button class="btn" id="btnT">Simpan</button>
<span id="msgT" class="sub" style="margin-left:10px"></span>
</div>
<div class="card"><h2>Tabel Kalibrasi</h2>
<p class="sub" style="margin-bottom:12px">Jarak = pembacaan sensor dari atas &bull; Level = tinggi BBM dari dasar tangki (cm)</p>
<div id="calRows"></div>
<div style="display:flex;gap:10px;align-items:center;margin-top:8px">
<button class="btn btn-sm" id="btnAdd">+ Titik</button>
<span class="sub">2&ndash;5 titik, urutan jarak naik</span>
</div>
<br><button class="btn" id="btnC">Simpan</button>
<span id="msgC" class="sub" style="margin-left:10px"></span>
</div>
<div class="card"><h2>Modbus RTU</h2>
<div class="row"><label>Slave ID (1&ndash;247)</label><input id="sid" type="number" min="1" max="247"></div>
<button class="btn" id="btnS">Simpan</button>
<span id="msgS" class="sub" style="margin-left:10px"></span>
</div>
<footer>Fuel Sensor Genset &middot; FW: )html"
FW_VERSION_STR
R"html( &middot; 192.168.4.1</footer>
<script>
let $=i=>document.getElementById(i),pts=[];
function render(){
  $('calRows').innerHTML=pts.map((p,i)=>'<div class="cr"><span class="lbl">Titik '+(i+1)+'</span>J: <input type="number" data-i="'+i+'" data-f="j" value="'+p.j.toFixed(1)+'" min="3" max="450" step="0.1"> cm &rarr; L: <input type="number" data-i="'+i+'" data-f="l" value="'+p.l.toFixed(1)+'" min="0" max="999.9" step="0.1"> cm'+(pts.length>2?' <button class="del" data-i="'+i+'">&times;</button>':'')+'</div>').join('');
  $('calRows').querySelectorAll('input').forEach(el=>el.oninput=()=>{pts[+el.dataset.i][el.dataset.f]=+el.value});
  $('calRows').querySelectorAll('.del').forEach(el=>el.onclick=()=>{pts.splice(+el.dataset.i,1);render()});
}
function updateVH(){
  let h=+$('th').value,l=+$('tl').value,w=+$('tw').value;
  $('vh').textContent=(h>0&&l>0&&w>0)?(h*l*w/1000).toFixed(1)+' L':'-- L';
}
$('th').oninput=$('tl').oninput=$('tw').oninput=updateVH;
async function load(){
  try{
    let d=await(await fetch('/api/cfg')).json();
    $('th').value=d.th;$('tl').value=d.tl;$('tw').value=d.tw;$('so').value=d.so;$('sid').value=d.id;
    pts=d.cal&&d.cal.length>=2?d.cal.map(p=>({j:p.j,l:p.l})):[{j:3.0,l:47.0},{j:50.0,l:0.0}];
  }catch(e){pts=[{j:3.0,l:47.0},{j:50.0,l:0.0}];}
  render();updateVH();
}
$('btnAdd').onclick=()=>{if(pts.length<5){pts.push({j:25.0,l:23.5});render();}};
$('btnT').onclick=async()=>{
  $('msgT').textContent='menyimpan...';
  try{let r=await(await fetch('/api/cfgsave?th='+$('th').value+'&tl='+$('tl').value+'&tw='+$('tw').value+'&so='+$('so').value)).json();$('msgT').textContent=r.ok?'tersimpan':'gagal';}
  catch(e){$('msgT').textContent='error koneksi';}
};
$('btnC').onclick=async()=>{
  $('msgC').textContent='menyimpan...';
  let s=pts.slice().sort((a,b)=>a.j-b.j);
  let q='/api/calsave?n='+s.length+s.map((p,i)=>'&j'+i+'='+p.j+'&l'+i+'='+p.l).join('');
  try{let r=await(await fetch(q)).json();$('msgC').textContent=r.ok?'tersimpan':'gagal';}
  catch(e){$('msgC').textContent='error koneksi';}
};
$('btnS').onclick=async()=>{
  $('msgS').textContent='menyimpan...';
  try{let r=await(await fetch('/api/setid?id='+$('sid').value)).json();$('msgS').textContent=r.ok?'tersimpan':'gagal';}
  catch(e){$('msgS').textContent='error koneksi';}
};
load();
</script></body></html>)html";

static void webHandleSetup() {
    webServer.send_P(200, "text/html", SETUP_HTML);
}

static void webHandleApiCfg() {
    // Bangun JSON array kalibrasi
    char cal[160];
    int pos = 0;
    pos += snprintf(cal + pos, sizeof(cal) - pos, "[");
    for (uint8_t i = 0; i < rtCalN; i++) {
        if (i > 0) pos += snprintf(cal + pos, sizeof(cal) - pos, ",");
        pos += snprintf(cal + pos, sizeof(cal) - pos,
            "{\"j\":%.1f,\"l\":%.1f}",
            rtCal[i].jarak / 10.0f, rtCal[i].level / 10.0f);
    }
    snprintf(cal + pos, sizeof(cal) - pos, "]");

    char json[320];
    // Kirim dalam cm: UI menerima/menampilkan cm, internal tetap mm
    snprintf(json, sizeof(json),
        "{\"th\":%u,\"tl\":%u,\"tw\":%u,\"so\":%u,\"id\":%u,\"cal\":%s}",
        (unsigned)(rtTankH / 10), (unsigned)(rtTankL / 10), (unsigned)(rtTankW / 10),
        (unsigned)(rtSensOff / 10), (unsigned)slaveId, cal);
    webServer.sendHeader("Cache-Control", "no-cache");
    webServer.send(200, "application/json", json);
}

static void webHandleApiCfgSave() {
    bool ok = false;
    if (webServer.hasArg("th") && webServer.hasArg("tl") &&
        webServer.hasArg("tw") && webServer.hasArg("so")) {
        int th = webServer.arg("th").toInt();   // cm
        int tl = webServer.arg("tl").toInt();   // cm
        int tw = webServer.arg("tw").toInt();   // cm
        int so = webServer.arg("so").toInt();   // cm
        if (th >= 1 && th <= 999 && tl >= 1 && tl <= 999 &&
            tw >= 1 && tw <= 999 && so >= 3 && so <= 50) {
            rtTankH   = (uint16_t)(th * 10);   // simpan dalam mm
            rtTankL   = (uint16_t)(tl * 10);
            rtTankW   = (uint16_t)(tw * 10);
            rtSensOff = (uint16_t)(so * 10);
            prefs.begin(NVS_NAMESPACE, false);
            prefs.putUShort(NVS_KEY_TANK_H,   rtTankH);
            prefs.putUShort(NVS_KEY_TANK_L,   rtTankL);
            prefs.putUShort(NVS_KEY_TANK_W,   rtTankW);
            prefs.putUShort(NVS_KEY_SENS_OFF, rtSensOff);
            prefs.end();
            // Hitung volume maks untuk log
            uint64_t vmL = (uint64_t)rtTankL * rtTankW * rtTankH / 1000000ULL;
            Serial.printf("[CFG] Tangki: H=%dcm P=%dcm L=%dcm | Vmaks=%llu L | off=%dcm\n",
                th, tl, tw, vmL, so);
            ok = true;
        }
    }
    webServer.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void webHandleApiCalSave() {
    bool ok = false;
    if (webServer.hasArg("n")) {
        uint8_t n = (uint8_t)webServer.arg("n").toInt();
        if (n >= 2 && n <= 5) {
            bool valid = true;
            CalPoint tmp[5];
            char key[8];
            for (uint8_t i = 0; i < n && valid; i++) {
                snprintf(key, sizeof(key), "j%d", i);
                if (!webServer.hasArg(key)) { valid = false; break; }
                tmp[i].jarak = (uint16_t)(webServer.arg(key).toFloat() * 10.0f + 0.5f);
                snprintf(key, sizeof(key), "l%d", i);
                if (!webServer.hasArg(key)) { valid = false; break; }
                tmp[i].level = (uint16_t)(webServer.arg(key).toFloat() * 10.0f + 0.5f);
            }
            if (valid) {
                rtCalN = n;
                memcpy(rtCal, tmp, n * sizeof(CalPoint));
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putUChar(NVS_KEY_CAL_N, rtCalN);
                for (uint8_t i = 0; i < n; i++) {
                    snprintf(key, sizeof(key), "cal_j%d", i);
                    prefs.putUShort(key, rtCal[i].jarak);
                    snprintf(key, sizeof(key), "cal_l%d", i);
                    prefs.putUShort(key, rtCal[i].level);
                }
                prefs.end();
                Serial.printf("[CFG] Kalibrasi: %d titik tersimpan\n", rtCalN);
                ok = true;
            }
        }
    }
    webServer.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ── Proses Frame Sensor ───────────────────────────────────────────────────────

// Format A0221AU UART Auto: 0xFF DATA_H DATA_L SUM
// SUM = (0xFF + DATA_H + DATA_L) & 0xFF
static void prosesSensorFrame() {
    uint8_t cs = (uint8_t)(SENSOR_HEADER + sBuf[1] + sBuf[2]);
    if (sBuf[3] != cs) {
        Serial.printf("[SENSOR] Checksum error — exp=0x%02X got=0x%02X\n", cs, sBuf[3]);
        return;
    }
    uint16_t jarak = ((uint16_t)sBuf[1] << 8) | sBuf[2];

    // Gate plausibilitas: buang sampel di luar batas fisik tangki (gunakan config runtime)
    const uint16_t jarakMin = rtSensOff;
    // +500mm buffer agar silinder di atas tangki tetap tercakup range plausibel
    const uint16_t jarakMax = (uint16_t)(rtTankH + rtSensOff + 500);
    if (jarak < jarakMin || jarak > jarakMax) {
        Serial.printf("[SENSOR] Jarak di luar rentang (%d mm) — dibuang\n", jarak);
        return;
    }

    // Masukkan ke buffer median circular (7 slot)
    medBuf[medIdx] = jarak;
    medIdx = (medIdx + 1) % 7;
    if (medCount < 7) medCount++;

    uint16_t medianJarak = hitungMedian();

    // Filter spike: tolak perubahan mendadak melebihi SENSOR_SPIKE_MAX_MM
    static uint16_t lastJarakFiltered = 0;
    static uint8_t  spikeCount = 0;
    if (lastJarakFiltered == 0) {
        // Inisialisasi: terima frame pertama tanpa filter
        lastJarakFiltered = medianJarak;
    } else if (spikeCount >= SENSOR_SPIKE_HOLDOFF) {
        // Terlalu banyak spike berturut → paksa terima (misal: pengisian cepat)
        Serial.printf("[SENSOR] Spike paksa diterima setelah %d frame, Δ=%d mm\n",
            spikeCount, (int)medianJarak - (int)lastJarakFiltered);
        lastJarakFiltered = medianJarak;
        spikeCount = 0;
    } else {
        uint16_t delta = (medianJarak > lastJarakFiltered)
                         ? (medianJarak - lastJarakFiltered)
                         : (lastJarakFiltered - medianJarak);
        if (delta > SENSOR_SPIKE_MAX_MM) {
            spikeCount++;
            Serial.printf("[SENSOR] Spike #%d: %d→%d mm (Δ=%u, maks=%u)\n",
                spikeCount, lastJarakFiltered, medianJarak, delta, SENSOR_SPIKE_MAX_MM);
            medianJarak = lastJarakFiltered;  // pertahankan nilai terakhir valid
        } else {
            lastJarakFiltered = medianJarak;
            spikeCount = 0;
        }
    }

    // Kuantisasi ke 10mm terdekat (ceiling) — getaran ±5mm tetap di bucket yang sama
    medianJarak = ((medianJarak + 9) / 10) * 10;
    hreg[HREG_DISTANCE] = medianJarak;
    sLastFrameMs = millis();

    if (!sensorOk) {
        sensorOk = true;
        Serial.printf("[SENSOR] Frame valid pertama: %d mm\n", jarak);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(50);   // satu-satunya delay yang diizinkan

    // ── NVS: load semua pengaturan tersimpan ──
    prefs.begin(NVS_NAMESPACE, true);   // read-only
    slaveId   = prefs.getUChar(NVS_KEY_SLAVE_ID, MODBUS_DEFAULT_SLAVE_ID);
    rtTankH   = prefs.getUShort(NVS_KEY_TANK_H,   TANK_HEIGHT_MM);
    rtTankL   = prefs.getUShort(NVS_KEY_TANK_L,   TANK_LENGTH_MM);
    rtTankW   = prefs.getUShort(NVS_KEY_TANK_W,   TANK_WIDTH_MM);
    rtSensOff = prefs.getUShort(NVS_KEY_SENS_OFF, SENSOR_OFFSET_MM);
    uint8_t savedCalN = prefs.getUChar(NVS_KEY_CAL_N, 0);
    if (savedCalN >= 2 && savedCalN <= 5) {
        rtCalN = savedCalN;
        char key[8];
        for (uint8_t i = 0; i < rtCalN; i++) {
            snprintf(key, sizeof(key), "cal_j%d", i);
            rtCal[i].jarak = prefs.getUShort(key, 0);
            snprintf(key, sizeof(key), "cal_l%d", i);
            rtCal[i].level = prefs.getUShort(key, 0);
        }
    } else {
        // Default: salin dari CAL_TABLE compile-time
        rtCalN = CAL_TABLE_SIZE;
        for (uint8_t i = 0; i < rtCalN; i++) rtCal[i] = CAL_TABLE[i];
    }
    prefs.end();

    // ── I2C + LCD ──
    Wire.begin(SDA_PIN, SCL_PIN);
    const uint8_t kAddrs[] = {LCD_ADDR_PRIMARY, LCD_ADDR_SECONDARY};
    uint8_t lcdAddr = 0;
    for (uint8_t i = 0; i < 2; i++) {
        Wire.beginTransmission(kAddrs[i]);
        if (Wire.endTransmission() == 0) { lcdAddr = kAddrs[i]; break; }
    }
    if (lcdAddr) {
        lcd = new LiquidCrystal_I2C(lcdAddr, LCD_COLS, LCD_ROWS);
        lcd->init();
        lcd->backlight();
        lcd->setCursor(0, 0); lcd->print("Fuel Monitor    ");
        lcd->setCursor(0, 1); lcd->print(FW_VERSION_STR " Booting");
        lcdOk = true;
        Serial.printf("[LCD] Ditemukan di 0x%02X\n", lcdAddr);
    } else {
        Serial.println("[LCD] PERINGATAN: tidak ditemukan di 0x27/0x3F — sistem tetap jalan");
    }

    // ── Sensor UART (A0221AU) ──
    SerialSensor.begin(SENSOR_BAUD, SERIAL_8N1, SENSOR_RX, SENSOR_TX);
    Serial.printf("[SENSOR] UART Serial1 @%d baud, RX=GPIO%d\n", SENSOR_BAUD, SENSOR_RX);

    // ── RS485 + Modbus RTU Slave ──
    SerialRS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    mb.begin(&SerialRS485);   // 4-pin module — tanpa DE/RE, arah otomatis
    mb.slave(slaveId);
    mb.addHreg(0, 0, HREG_COUNT);
    Serial.printf("[MODBUS] Slave ID=%d @%d baud, RX=GPIO%d TX=GPIO%d (auto-dir)\n",
        slaveId, RS485_BAUD, RS485_RX, RS485_TX);

    // ── WiFi SoftAP ──
    WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
    Serial.printf("[WIFI] SoftAP '%s' IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // ── Web Server ──
    webServer.on("/",            webHandleRoot);
    webServer.on("/setup",       webHandleSetup);
    webServer.on("/api/data",    webHandleApiData);
    webServer.on("/api/cfg",     webHandleApiCfg);
    webServer.on("/api/cfgsave", webHandleApiCfgSave);
    webServer.on("/api/calsave", webHandleApiCalSave);
    webServer.on("/api/setid",   webHandleApiSetId);
    webServer.begin();

    // Isi nilai register versi (statis, tidak berubah saat runtime)
    hreg[HREG_FW_MAJOR]       = FW_VER_MAJOR;
    hreg[HREG_FW_MINOR_PATCH] = FW_VER_MINOR * 100 + FW_VER_PATCH;
    updateRegisters();

    Serial.printf("═══ Fuel Sensor %s | Slave %d | AP '%s' | %s ═══\n",
        FW_VERSION_STR, slaveId, AP_SSID,
        WiFi.softAPIP().toString().c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop — semua non-blocking
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
    // ── Sensor UART: baca karakter per karakter ───────────────────────────────
    while (SerialSensor.available()) {
        uint8_t b = (uint8_t)SerialSensor.read();
        if (sIdx == 0 && b != SENSOR_HEADER) continue;   // buang sampai dapat header
        sBuf[sIdx++] = b;
        if (sIdx >= SENSOR_FRAME_LEN) {
            prosesSensorFrame();
            sIdx = 0;
        }
    }

    // ── Modbus RTU: proses request dari master ────────────────────────────────
    mb.task();

    // ── Web Server ────────────────────────────────────────────────────────────
    webServer.handleClient();

    // ── Update register + LCD (terjadwal 500 ms) ─────────────────────────────
    if (due(taskLcd)) {
        // Deteksi timeout sensor
        if (sensorOk && (uint32_t)(millis() - sLastFrameMs) > SENSOR_TIMEOUT_MS) {
            sensorOk = false;
            Serial.println("[SENSOR] Timeout — tidak ada frame, tandai ERROR");
        }
        updateRegisters();
        updateLcd();
    }

    // ── Heartbeat serial monitor (terjadwal 5 s) ─────────────────────────────
    if (due(taskHb)) {
        Serial.printf("[HB] %s | ID:%d | Jarak:%dcm | Lv:%dcm(%d%%) | Vol:%dL | Sen:%s\n",
            FW_VERSION_STR, slaveId,
            hreg[HREG_DISTANCE] / 10,
            hreg[HREG_LEVEL_CM],
            hreg[HREG_LEVEL_PCT] / 10,
            (hreg[HREG_VOLUME_DL] + 5) / 10,
            sensorOk ? "OK" : "ERROR");
    }
}
