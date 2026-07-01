# Monitor Level BBM Genset

**ESP32-C3 SuperMini** — baca level bahan bakar genset via sensor ultrasonik A0221AU, tampil di LCD 16×2, lapor via Modbus RTU (RS485), dan web dashboard WiFi.

**Versi firmware: v0.5.0**

---

## Fitur

- Pembacaan level BBM non-blocking via sensor ultrasonik A0221AU (antarmuka UART)
- Anti-spike: median filter 7-sampel + gate plausibilitas
- Kalibrasi via tabel `CAL_TABLE` — mendukung geometri tangki non-linear
- Tampilan LCD 16×2 I2C, update tiap 500 ms
- Modbus RTU slave via RS485, 11 holding register
- Web dashboard dark theme WiFi SoftAP, polling JSON otomatis 1.5 detik
- Halaman `/setup` — konfigurasi tangki, kalibrasi, dan Modbus slave ID via browser (tanpa flash ulang)
- **Buzzer aktif** — alarm berbunyi otomatis saat BBM rendah (≤20%) atau kritis (≤10%), bisa di-mute via dashboard
- **OTA update via browser** — upload `firmware.bin` langsung dari halaman `/update`, tanpa kabel USB
- Semua pengaturan runtime tersimpan di NVS (tidak hilang saat power off)
- Seluruh konfigurasi default terpusat di `include/config.h`

---

## Komponen

| # | Komponen | Keterangan |
|---|---|---|
| 1 | ESP32-C3 SuperMini | MCU utama |
| 2 | Sensor ultrasonik A0221AU | Antarmuka UART, dipasang di atas tangki |
| 3 | LCD 16×2 + modul I2C | Alamat otomatis: 0x27 atau 0x3F |
| 4 | Modul RS485 4-pin (VCC TX RX GND) | Auto-direction, tanpa pin DE/RE |
| 5 | Buzzer aktif 5V | Sinyal HIGH = berbunyi, terhubung ke GPIO10 |
| 6 | Catu daya 5V | Untuk MCU, sensor, dan buzzer |

---

## Pinout ESP32-C3 SuperMini

```
              ┌──────[USB-C]──────┐
       3V3  ──┤                   ├── GPIO5   → LCD SCL
        5V  ──┤                   ├── GPIO6   ← RS485 TX  ◄──
       GND  ──┤  ESP32-C3         ├── GPIO7   → RS485 RX  ──►
     GPIO0  ──┤  SuperMini        ├── GPIO8   ⚠ strapping (LED bawaan)
     GPIO1  ──┤                   ├── GPIO9   ⚠ BOOT / strapping
     GPIO2  ──┤  ⚠ strapping      ├── GPIO10  → BUZZER
     GPIO3  ──┤  ◄── SENSOR RX   ├── GPIO20  ⚡ USB-CDC RX (jangan pakai)
     GPIO4  ──┤  ↔── LCD SDA     ├── GPIO21  ⚡ USB-CDC TX (jangan pakai)
              └───────────────────┘
```

> ⚠ = strapping pin — GPIO2, GPIO8, GPIO9 sebaiknya tidak dipakai untuk I/O bebas (bisa mempengaruhi mode boot).  
> ⚡ = GPIO20/GPIO21 adalah pin fisik di board, namun dipakai oleh USB-CDC (`Serial`) — jangan digunakan selama Serial aktif.

---

## Diagram Wiring

### Sensor A0221AU → ESP32-C3

```
  A0221AU (4-pin)   ESP32-C3 SuperMini
  ┌──────────┐       ┌─────────────┐
  │ ① VCC   ├───────┤ 5V / 3.3V  │  (3.3–5V, typ. 5V)
  │ ② GND   ├───────┤ GND        │
  │ ③ RX    │  ✗    │            │  tidak dihubungkan (mode Auto)
  │ ④ TX    ├───────┤ GPIO3      │  ← SENSOR_RX
  └──────────┘       └─────────────┘
```

> Mode UART Auto: sensor kirim data sendiri, pin RX sensor tidak perlu disambungkan.

### LCD 16×2 I2C → ESP32-C3

```
  Modul I2C LCD     ESP32-C3 SuperMini
  ┌───────────┐      ┌─────────────┐
  │  VCC      ├──────┤ 5V         │
  │  GND      ├──────┤ GND        │
  │  SDA      ├──────┤ GPIO4      │  ← SDA_PIN
  │  SCL      ├──────┤ GPIO5      │  ← SCL_PIN
  └───────────┘      └─────────────┘
  Alamat I2C discan otomatis saat boot (0x27 / 0x3F)
```

### Modul RS485 → ESP32-C3

```
  Modul RS485 4-pin   ESP32-C3 SuperMini
  ┌───────────┐        ┌─────────────┐
  │  VCC      ├────────┤ 3V3 / 5V   │  (sesuai modul)
  │  GND      ├────────┤ GND        │
  │  TX (RO)  ├────────┤ GPIO6      │  ← RS485_TX
  │  RX (DI)  ├────────┤ GPIO7      │  → RS485_RX
  ├───────────┤        └─────────────┘
  │  A (D+)   ├─────── Bus RS485
  │  B (D-)   ├─────── Bus RS485
  └───────────┘
  Modul auto-direction (tanpa pin DE/RE) — arah TX/RX dikendalikan otomatis oleh modul.
```

### Buzzer Aktif → ESP32-C3

```
  Buzzer Aktif      ESP32-C3 SuperMini
  ┌──────────┐       ┌─────────────┐
  │  VCC (+) ├───────┤ 5V (atau 3V3 jika buzzer 3V3)
  │  GND (-) ├───────┤ GND        │
  │  I/O     ├───────┤ GPIO10     │  ← BUZZER_PIN (HIGH = berbunyi)
  └──────────┘       └─────────────┘
```

> Buzzer aktif (bukan pasif) — tidak perlu PWM, cukup sinyal HIGH/LOW.  
> Jika arus buzzer >10 mA, tambahkan transistor NPN (mis. 2N2222) sebagai driver.

---

## Konfigurasi

Semua parameter ada di [`include/config.h`](include/config.h). Tidak ada nilai yang perlu diubah di `main.cpp`.

### 1. Dimensi Tangki

Bisa dikonfigurasi via browser di **http://192.168.4.1/setup** tanpa flash ulang:

| Parameter | Default | Keterangan |
|---|:---:|---|
| Tinggi Dalam | 500 mm | Tinggi fisik dari dasar ke mulut tangki |
| Panjang Dalam | 400 mm | Panjang internal tangki |
| Lebar Dalam | 500 mm | Lebar internal tangki |
| Offset Sensor | 30 mm | Jarak ujung sensor ke mulut tangki (min 30mm) |

Volume dihitung otomatis: **V = Panjang × Lebar × Level / 1.000.000** (akurat untuk tangki persegi panjang). Halaman setup menampilkan volume maks kalkulasi secara real-time saat Anda mengisi dimensi.

Nilai default di `config.h` (hanya dipakai jika NVS kosong):
```c
#define TANK_HEIGHT_MM   500   // mm
#define TANK_LENGTH_MM   400   // mm — 400×500×500/1e6 = 100 L
#define TANK_WIDTH_MM    500   // mm
#define SENSOR_OFFSET_MM  30   // mm
```

### 2. Tabel Kalibrasi

Bisa diatur via browser di **http://192.168.4.1/setup** → bagian **Tabel Kalibrasi**.

Ukur jarak sensor pada beberapa posisi level BBM (2–5 titik):

| Titik | Jarak Sensor (mm) | Level BBM dari Dasar (mm) |
|:---:|:---:|:---:|
| 1 | 30 | 470 |
| 2 | 200 | 280 (contoh titik tengah) |
| 3 | 500 | 0 |

Semakin banyak titik = semakin akurat, terutama untuk tangki silinder/non-persegi.

Nilai default di `config.h`:
```c
static const CalPoint CAL_TABLE[] = {
    { 30, 470},   // sensor baca 30mm  → BBM setinggi 470mm dari dasar
    {500,   0},   // sensor baca 500mm → tangki kosong
};
```

### 3. Protokol Sensor A0221AU

Mode **UART Auto** — sensor mengirim frame tiap ~100 ms secara otomatis, tanpa perlu trigger dari MCU.

| Byte | Isi | Keterangan |
|:---:|---|---|
| 0 | `0xFF` | Header tetap |
| 1 | `DATA_H` | Jarak high byte |
| 2 | `DATA_L` | Jarak low byte |
| 3 | `SUM` | Checksum |

- **Jarak (mm)** = `(DATA_H << 8) | DATA_L`
- **Checksum** = `(0xFF + DATA_H + DATA_L) & 0xFF`
- **Baud rate**: 9600, 8N1
- **Range valid**: 30–4500 mm (blind zone 3 cm)

> Karena mode Auto, pin TX sensor tidak perlu dihubungkan ke MCU (`SENSOR_TX = -1` di config.h).

### 4. Buzzer Alarm

Pin dan threshold dikonfigurasi di `config.h`:

```c
#define BUZZER_PIN               10    // GPIO10 — active buzzer

#define ALARM_LEVEL_LOW_PCT      200   // ×10 → 20.0% — BBM rendah
#define ALARM_LEVEL_CRITICAL_PCT 100   // ×10 → 10.0% — BBM kritis
```

| Kondisi | Pola Bunyi |
|---|---|
| BBM ≤ 10% (kritis) | 2 beep cepat per detik |
| BBM ≤ 20% (rendah) | 1 beep per 3 detik |
| Sensor error | 1 beep singkat per 5 detik |
| Normal | Diam |

Tombol **Mute/Unmute** tersedia di dashboard. Mute tidak disimpan ke NVS — setelah restart, buzzer aktif kembali.

### 5. WiFi SoftAP

```c
#define AP_SSID    "GensetMonitor"
#define AP_PASS    "genset123"
```

| URL | Fungsi |
|---|---|
| **http://192.168.4.1/** | Dashboard (level BBM, volume, status, mute buzzer) |
| **http://192.168.4.1/setup** | Konfigurasi tangki, kalibrasi, Modbus ID |
| **http://192.168.4.1/update** | Upload firmware OTA (file `.bin`) |

Buka dari HP/PC yang terhubung ke WiFi `GensetMonitor`.

> **⚠ Catatan Hardware — ESP32-C3 SuperMini SoftAP tidak muncul**
>
> Dua masalah umum ditemukan saat pengujian dengan board ESP32-C3 SuperMini:
>
> **1. Bug arduino-esp32 3.3.8** — versi ini punya bug di mana SSID SoftAP tidak muncul di scan list perangkat lain.
> Solusi: `platformio.ini` sudah di-pin ke `espressif32@6.1.0` yang menggunakan arduino-esp32 2.0.x (versi stabil).
>
> **2. Regulator onboard kurang kuat** — beberapa batch SuperMini memiliki regulator hanya 250 mA, sedangkan default TX power ESP32-C3 (19.5 dBm) bisa menarik lebih dari itu, menyebabkan brownout sesaat sehingga AP gagal start.
> Solusi: firmware sudah memanggil `WiFi.setTxPower(WIFI_POWER_8_5dBm)` sebelum `softAP()`.
>
> **3. Varian IPEX tanpa antena eksternal** — jalur RF default ke SMD ceramic antenna. Jika ingin pakai konektor IPEX, perlu: pasang antena eksternal + solder jumper pad RF di PCB + lepas SMD antenna.
> Varian dengan ceramic antenna onboard (tertulis "C3" di area antena) langsung berfungsi tanpa modifikasi hardware.

#### OTA Update via Browser

1. Build firmware: `~/.platformio/penv/bin/pio run`
2. Buka **http://192.168.4.1/update**
3. Pilih file `.pio/build/esp32-c3-devkitm-1/firmware.bin`
4. Klik **Upload & Update** — progress bar muncul selama upload
5. Perangkat restart otomatis, halaman redirect ke dashboard dalam 8 detik

### 6. Modbus RTU

```c
#define RS485_BAUD              9600
#define MODBUS_DEFAULT_SLAVE_ID 1    // dapat diubah via web dashboard
```

---

## Peta Register Modbus

Function code: **FC03** (Read Holding Registers). Semua register read-only dari sisi master.

| Offset | Nama | Skala | Contoh nilai | Keterangan |
|:---:|---|:---:|:---:|---|
| 0 | `DISTANCE` | 1 mm | 245 | Jarak sensor raw |
| 1 | `LEVEL_CM` | 1 cm | 25 | Level BBM setelah kalibrasi |
| 2 | `LEVEL_PCT` | 0.1 % | 510 | Level 51.0% |
| 3 | `VOLUME_DL` | 0.1 L | 510 | Volume 51.0 L |
| 4 | `STATUS` | bit flag | 0x07 | b0=Sensor OK, b1=LCD OK, b2=WiFi OK |
| 5 | `SLAVE_ID` | — | 1 | Slave ID aktif |
| 6 | _(reserved)_ | — | 0 | — |
| 7 | `FW_MAJOR` | — | 0 | Versi firmware major |
| 8 | `FW_MINOR_PATCH` | — | 500 | minor×100+patch → v0.5.0 |
| 9 | `BUZZER_STATUS` | bit flag | 0x02 | b0=mute, b1-2=alarm (0=none 1=rendah 2=kritis 3=sensor-err) |
| 10 | `HW_TYPE` | — | 1 | Board ID hardware (1 = ESP32-C3 SuperMini) |

> **Append-only** — offset yang sudah ada tidak boleh diubah atau digeser agar kompatibel dengan master yang sudah terpasang.

---

## Build & Flash

```bash
# Build (wajib sukses sebelum flash)
~/.platformio/penv/bin/pio run

# Flash ke MCU
~/.platformio/penv/bin/pio run -t upload

# Monitor serial (115200 baud)
~/.platformio/penv/bin/pio device monitor
```

> Jika auto-reset gagal saat upload: tahan tombol **BOOT** → tekan **RESET** → lepas keduanya → jalankan perintah upload.

---

## Output Serial

Boot normal:
```
[LCD] Ditemukan di 0x27
[SENSOR] UART Serial1 @9600 baud, RX=GPIO3
[MODBUS] Slave ID=1 @9600 baud, RX=GPIO6 TX=GPIO7 (auto-dir)
[WIFI] SoftAP 'GensetMonitor' IP 192.168.4.1
═══ Fuel Sensor v0.5.0 | Slave 1 | AP 'GensetMonitor' | 192.168.4.1 | Buzzer GPIO10 ═══
```

Heartbeat tiap 5 detik:
```
[HB] v0.1.0 | ID:1 | Jarak:245mm | Lv:255mm(51.0%) | Vol:51.0L | Sen:OK
```

---

## Struktur Proyek

```
├── include/
│   └── config.h      ← semua konfigurasi (pin, kalibrasi, register, versi)
├── src/
│   └── main.cpp      ← scheduler non-blocking, sensor, Modbus, LCD, web
├── platformio.ini    ← board & library dependencies
├── CLAUDE.md         ← panduan untuk AI coding tool
└── AGENTS.md         ← aturan wajib pengembangan
```

---

## Riwayat Versi

| Versi | Tanggal | Perubahan |
|---|---|---|
| v0.5.0 | 2026-06-23 | Modul buzzer aktif (GPIO10) + OTA update via browser (/update) |
| v0.4.0 | 2026-06-22 | Dukungan modul RS485 4-pin (auto-direction, tanpa pin DE/RE) |
| v0.3.0 | 2026-06-22 | Volume dari dimensi tangki (P×L×T) — akurat, tanpa perlu ukur kapasitas manual |
| v0.2.0 | 2026-06-22 | Halaman `/setup`: konfigurasi tangki, kalibrasi, dan Modbus slave ID via web (NVS) |
| v0.1.0 | 2026-06-22 | Rilis awal: sensor UART, LCD I2C, Modbus RTU slave, web SoftAP, kalibrasi tabel |
