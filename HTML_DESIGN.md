# HTML Design Template — Fuel Sensor Genset

Template desain untuk halaman web ESP8266/ESP32 yang di-embed di `main.cpp` sebagai C++ raw string.
Semua halaman menggunakan dark theme GitHub-inspired dengan layout mobile-first.

---

## Design Tokens (CSS Variables)

```css
:root {
  --bg:   #0f1419;  /* background utama (body) */
  --card: #1a2129;  /* background card/panel */
  --mut:  #7d8896;  /* teks muted/sekunder, label */
  --txt:  #e6edf3;  /* teks utama */
  --ok:   #3fb950;  /* hijau — sukses, terhubung, simpan */
  --warn: #d29922;  /* kuning — peringatan, data mock */
  --dan:  #f85149;  /* merah — error, terputus, kritis */
  --line: #2a333d;  /* warna border card & divider */
  /* warna tambahan (tidak di variabel tapi sering dipakai) */
  /* #0d1117 — background input/form & bar kosong */
  /* #06210e — teks di atas tombol hijau (kontras) */
}
```

---

## Boilerplate Halaman

Setiap halaman dimulai dengan struktur ini (di-embed sebagai raw string C++):

```html
<!doctype html><html lang="id"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>JUDUL_HALAMAN &middot; Fuel Genset</title><style>
/* --- PASTE CSS SECTION DI SINI --- */
</style></head><body>
<!-- --- PASTE BODY DI SINI --- */
<script>
/* --- PASTE JS DI SINI --- */
</script></body></html>
```

> **Catatan C++:** Embed sebagai `R"html(...)html"` atau `R"(...)"`  
> Semua CSS & JS ditulis **minified** (satu baris per blok) untuk hemat flash.

---

## CSS Base (wajib di setiap halaman)

```css
:root{--bg:#0f1419;--card:#1a2129;--mut:#7d8896;--txt:#e6edf3;--ok:#3fb950;--warn:#d29922;--dan:#f85149;--line:#2a333d}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--txt);font-family:-apple-system,Segoe UI,Roboto,sans-serif;padding:16px;max-width:680px;margin:auto}
```

Hapus `--warn` dan `--dan` jika tidak dipakai di halaman tersebut.

---

## Komponen HTML

### Header

```html
<header>
  <h1>&#9881; Judul Halaman</h1>
  <!-- Kanan: link navigasi atau status koneksi -->
  <a class="lnk" href="/">&larr; Dashboard</a>
</header>
```

**CSS header:**
```css
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}
h1{font-size:18px;font-weight:600}
```

**Ikon umum:** `&#9981;` = ⛉ (genset), `&#9881;` = ⚙ (pengaturan), `&#11014;` = ⬆ (upload OTA)

---

### Indikator Koneksi (dot animasi)

```html
<span><span class="dot" id="dot"></span><span id="conn" style="font-size:12px;color:var(--mut)">menghubungkan&hellip;</span></span>
```

```css
.dot{width:9px;height:9px;border-radius:50%;background:var(--dan);display:inline-block;margin-right:6px;transition:.3s}
.dot.on{background:var(--ok);box-shadow:0 0 8px var(--ok)}
```

```js
// set terhubung: dot.classList.add('on'); conn.textContent='terhubung'
// set terputus:  dot.classList.remove('on'); conn.textContent='terputus'
```

---

### Card (container umum)

```html
<div class="card">
  <h2>Judul Seksi</h2>
  <!-- konten -->
</div>
```

```css
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
h2{font-size:13px;color:var(--mut);text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px}
```

---

### Metric Besar (level BBM)

```html
<div class="card">
  <div class="top">
    <div><span class="big" id="lvl">--</span><span class="unit">%</span></div>
    <div class="sub" id="mm">-- mm</div>
  </div>
  <div class="bar"><div class="fill" id="fill"></div></div>
</div>
```

```css
.top{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:10px}
.big{font-size:44px;font-weight:700;line-height:1}
.unit{font-size:16px;color:var(--mut);margin-left:4px}
.bar{height:14px;background:#0d1117;border-radius:8px;overflow:hidden;border:1px solid var(--line)}
.fill{height:100%;width:0;border-radius:8px;transition:width .6s,background .3s}
.sub{color:var(--mut);font-size:13px}
```

```js
// Update bar level dengan warna dinamis:
fill.style.width = Math.max(0, Math.min(100, pct)) + '%';
fill.style.background = pct < 20
  ? 'linear-gradient(90deg,#f85149,#ff7b72)'   // merah
  : pct < 50
    ? 'linear-gradient(90deg,#d29922,#e3b341)' // kuning
    : 'linear-gradient(90deg,#3fb950,#56d364)'; // hijau
```

---

### Grid Metrik Kecil (2 kolom)

```html
<div class="grid">
  <div class="metric">
    <div class="lbl">Suhu</div>
    <div class="val"><span id="t">--</span> &deg;C</div>
  </div>
  <div class="metric">
    <div class="lbl">Kelembaban</div>
    <div class="val"><span id="h">--</span> %</div>
  </div>
  <!-- tambah kolom sesuai kebutuhan -->
</div>
```

```css
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:14px;margin-bottom:14px}
.metric{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px}
.lbl{color:var(--mut);font-size:12px;text-transform:uppercase;letter-spacing:.5px}
.val{font-size:26px;font-weight:600;margin-top:4px}
```

---

### Badges Status

```html
<div class="badges" id="badges"></div>
```

```css
.badges{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}
.badge{font-size:12px;padding:5px 10px;border-radius:20px;background:#0d1117;border:1px solid var(--line);color:var(--mut)}
.badge.ok  {color:var(--ok);  border-color:var(--ok)}
.badge.bad {color:var(--dan); border-color:var(--dan)}
.badge.mock{color:var(--warn);border-color:var(--warn)}
```

```js
// Helper render badge:
function bdg(ok, name) {
  return '<span class="badge ' + (ok ? 'ok' : 'bad') + '">' + name + ': ' + (ok ? 'OK' : '—') + '</span>';
}
// Contoh penggunaan:
badges.innerHTML = bdg(d.dht,'DHT22') + bdg(d.ads,'ADS1115') + bdg(d.rs485,'RS485')
  + (d.mock ? '<span class="badge mock">DATA: MOCK</span>' : '');
```

---

### Log Aktivitas

```html
<div class="card">
  <h2>Log Aktivitas</h2>
  <div class="log" id="log"></div>
</div>
```

```css
.log{font-family:ui-monospace,Menlo,monospace;font-size:12px;max-height:220px;overflow:auto}
.log div{padding:4px 0;border-bottom:1px solid var(--line);color:#c9d1d9}
.log .ts{color:var(--mut);margin-right:8px}
```

```js
// Fetch & render log (urutan terbaru di atas):
async function tlog() {
  try {
    let l = await (await fetch('/api/log')).json();
    log.innerHTML = l.slice().reverse()
      .map(e => '<div><span class="ts">' + up(e.t) + '</span>' + e.m + '</div>')
      .join('');
  } catch(e) {}
}
```

---

### Form Input + Tombol

```html
<div class="set">
  <label class="sub">Label Input</label>
  <input id="myInput" type="number" min="1" max="247" step="1">
  <button id="myBtn">Simpan</button>
  <span id="myMsg" class="sub"></span>
</div>
```

```css
.set{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
input{font:inherit;width:90px;padding:8px 10px;border-radius:8px;border:1px solid var(--line);background:#0d1117;color:var(--txt)}
button{font:inherit;font-weight:600;padding:8px 16px;border-radius:8px;border:none;background:var(--ok);color:#06210e;cursor:pointer}
button:active{opacity:.8}
```

---

### Link Navigasi (tombol outline)

```html
<a class="lnk" href="/setup">&#9881; Pengaturan</a>
```

```css
a.lnk{font-size:13px;font-weight:600;color:var(--ok);text-decoration:none;border:1px solid var(--ok);padding:6px 12px;border-radius:8px}
/* Varian muted (untuk link balik): */
a.lnk{font-size:13px;color:var(--mut);text-decoration:none}
```

---

### Footer

```html
<footer>Fuel Sensor Genset &middot; Modbus RTU Slave &middot; 192.168.4.1</footer>
```

```css
footer{text-align:center;color:var(--mut);font-size:11px;margin-top:18px}
```

---

## JavaScript Patterns

### Uptime Formatter

```js
function up(s) {
  let h = s/3600|0, m = s%3600/60|0, x = s%60;
  return (h ? h+'j ' : '') + (m ? m+'m ' : '') + x+'s';
}
```

### Fetch API Data (polling)

```js
async function tick() {
  try {
    let d = await (await fetch('/api/data')).json();
    // update DOM dengan d.level, d.temp, d.humid, d.volt, d.up, d.id, d.dht, d.ads, d.rs485, d.mock, d.mm
    dot.classList.add('on');
    conn.textContent = 'terhubung';
  } catch(e) {
    dot.classList.remove('on');
    conn.textContent = 'terputus';
  }
}
tick();
setInterval(tick, 1500);   // polling setiap 1.5 detik
setInterval(tlog, 3000);   // log setiap 3 detik
```

### API Endpoints Tersedia

| Endpoint | Method | Fungsi |
|---|---|---|
| `/api/data` | GET | Data sensor lengkap (JSON) |
| `/api/log` | GET | Array log aktivitas |
| `/api/setid?id=N` | GET | Set Modbus Slave ID |
| `/api/cal` | GET | Baca kalibrasi saat ini |
| `/api/calsave?h=&v0=&v1=&v2=` | GET | Simpan kalibrasi 3 titik |

### Respons `/api/data`

```json
{
  "level": 75.3,
  "mm": 452,
  "temp": 28.5,
  "humid": 65.0,
  "volt": 1.823,
  "up": 3600,
  "id": 1,
  "dht": true,
  "ads": true,
  "rs485": true,
  "mock": false
}
```

---

## Checklist Halaman Baru

- [ ] Gunakan `lang="id"` di `<html>`
- [ ] Include `--bg`, `--card`, `--mut`, `--txt`, `--ok`, `--line` minimal
- [ ] `max-width:680px;margin:auto` di body untuk centering
- [ ] Semua hardcode (IP, nama AP) konsisten: `192.168.4.1`
- [ ] Teks UI dalam Bahasa Indonesia
- [ ] CSS & JS ditulis minified (satu baris) untuk hemat flash ESP
- [ ] Tidak ada library eksternal (CDN) — semua inline (offline-capable)
- [ ] Semua `fetch()` dibungkus `try/catch` — tampilkan state error ke user

---

## Contoh Halaman Baru (Skeleton)

```html
<!doctype html><html lang="id"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NAMA &middot; Fuel Genset</title><style>
:root{--bg:#0f1419;--card:#1a2129;--mut:#7d8896;--txt:#e6edf3;--ok:#3fb950;--line:#2a333d}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--txt);font-family:-apple-system,Segoe UI,Roboto,sans-serif;padding:16px;max-width:680px;margin:auto}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}
h1{font-size:18px;font-weight:600}h2{font-size:13px;color:var(--mut);text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
.sub{color:var(--mut);font-size:13px}
.set{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
input{font:inherit;width:90px;padding:8px 10px;border-radius:8px;border:1px solid var(--line);background:#0d1117;color:var(--txt)}
button{font:inherit;font-weight:600;padding:8px 16px;border-radius:8px;border:none;background:var(--ok);color:#06210e;cursor:pointer}
button:active{opacity:.8}
a.lnk{font-size:13px;color:var(--mut);text-decoration:none}
footer{text-align:center;color:var(--mut);font-size:11px;margin-top:18px}
</style></head><body>
<header><h1>&#9881; Nama Halaman</h1><a class="lnk" href="/">&larr; Dashboard</a></header>

<div class="card"><h2>Seksi Pertama</h2>
<div class="set">
  <label class="sub">Label</label>
  <input id="val" type="number">
  <button id="btn">Simpan</button>
  <span id="msg" class="sub"></span>
</div></div>

<footer>Fuel Sensor Genset &middot; 192.168.4.1</footer>
<script>
let $=i=>document.getElementById(i);
$('btn').onclick=async()=>{
  let v=$('val').value;
  $('msg').textContent='menyimpan...';
  try{
    let r=await(await fetch('/api/endpoint?param='+v)).json();
    $('msg').textContent=r.ok?'tersimpan':('gagal: '+(r.err||''));
  }catch(e){$('msg').textContent='error koneksi'}
};
</script></body></html>
```
