# PLAN.md — Rencana & Konteks Project

> Konteks utama project untuk AI agent. Hardware base → [HARDWARE.md](HARDWARE.md).
> Standar dashboard web → [DESIGN.md](DESIGN.md). Aturan kerja → [../AGENTS.md](../AGENTS.md).

---

## Identitas Project

| Item | Detail |
|------|--------|
| Nama Project | Monitor Level BBM Genset |
| Board Target | ESP32-C3 SuperMini (`esp32-c3-devkitm-1`) |
| Versi Firmware | v0.5.0 |
| Status | Aktif — fitur inti selesai |
| Bahasa kode/UI | Komentar & UI dalam Bahasa Indonesia |

---

## Tujuan Project

Memantau **level bahan bakar (BBM) tangki genset** secara realtime. Sensor ultrasonik
**A0221AU** dipasang di mulut tangki mengukur jarak ke permukaan BBM; firmware mengubahnya
jadi level (cm/%) dan volume (liter) lewat tabel kalibrasi + geometri tangki.

Hasil ditampilkan di **3 kanal sekaligus**:
- **LCD 16×2** di panel lokal (level, volume, status sensor),
- **Modbus RTU slave** via RS485 untuk SCADA/PLC/HMI atasan,
- **Web dashboard** (WiFi SoftAP) untuk operator via HP — termasuk halaman setup & OTA.

Pengguna: operator/teknisi genset yang perlu tahu sisa BBM dan dapat alarm saat menipis.

---

## Sensor / Modul Tambahan (di luar base)

| Modul | Interface | Fungsi | Library |
|-------|-----------|--------|---------|
| A0221AU ultrasonik | UART (UART1, 9600 8N1) | Ukur jarak ke permukaan BBM | — (parse frame manual) |

> RS485/Modbus, LCD I2C, Buzzer, SoftAP = base standard, lihat [HARDWARE.md](HARDWARE.md).

---

## Pin Assignment

> Lengkap di [HARDWARE.md](HARDWARE.md) dan [`include/config.h`](../include/config.h).

| GPIO | Fungsi | Catatan |
|------|--------|---------|
| 3 | Sensor A0221AU RX | hanya RX (mode Auto) |
| 4 / 5 | LCD SDA / SCL | I2C |
| 6 / 7 | RS485 TX / RX | modul 4-pin auto-direction |
| 10 | Buzzer | active HIGH |

---

## Override Hardware

- Board: ESP32-C3 SuperMini (default)
- Platform: `espressif32@6.1.0` (wajib — fix SoftAP)
- Baud RS485: 9600 · Modbus Mode: **Slave** (ID default 1)
- LCD Address: auto-scan 0x27 / 0x3F
- Buzzer: **active HIGH** (override dari base yang active-low)
- AP Channel: 6

---

## Fitur yang Dibangun

- [x] Baca sensor A0221AU (UART, frame non-blocking, checksum)
- [x] Anti-spike: median-of-7 + gate plausibilitas + holdoff spike
- [x] Konversi jarak→level via tabel kalibrasi (interpolasi linear, 2–5 titik)
- [x] Volume dari geometri tangki (P×L×level)
- [x] Tampil di LCD 16×2 (update 500 ms)
- [x] Modbus RTU slave, 10 holding register (append-only)
- [x] Web dashboard SoftAP (polling JSON 1.5 s)
- [x] Halaman `/setup` — tangki, kalibrasi, slave ID (simpan NVS, tanpa flash ulang)
- [x] Alarm buzzer (rendah/kritis/sensor-error) + mute via web
- [x] OTA update via browser (`/update`)
- [x] Semua pengaturan runtime persist di NVS

---

## Geometri & Kalibrasi Tangki

- **Default**: 400 × 500 × 500 mm (P×L×T) = 100 L. Offset sensor 30 mm (≥ blind zone).
- **Volume** = `TANK_LENGTH × TANK_WIDTH × level_mm / 1e6` L (akurat untuk tangki kotak).
- **Level** dari tabel kalibrasi `CAL_TABLE` `{jarak_mm → level_mm}` + interpolasi —
  bukan asumsi linear 2-titik, agar tangki silinder/trapesium tetap akurat.
- Semua dapat diubah runtime via `/setup` (disimpan ke NVS).

---

## Threshold & Logika Alarm

> `HREG_LEVEL_PCT` & threshold dalam satuan ×10 (200 = 20.0%).

| Kondisi | Nilai | Pola Buzzer |
|---------|-------|-------------|
| Normal | level > 20% | diam |
| Rendah | level ≤ `ALARM_LEVEL_LOW_PCT` (20%) | 1 beep / 3 detik |
| Kritis | level ≤ `ALARM_LEVEL_CRITICAL_PCT` (10%) | 2 beep cepat / detik |
| Sensor error | tak ada frame > `SENSOR_TIMEOUT_MS` (5 s) | 1 beep singkat / 5 detik |

Mute via dashboard (`/api/buzzmute`) — **tidak** disimpan NVS; restart = aktif kembali.

---

## Catatan Khusus Varian ESP32-C3 SuperMini

> Wajib dibaca sebelum replikasi — tanpa dua fix ini, SoftAP bisa **tidak muncul sama sekali**
> di sebagian batch board.

**1. Turunkan TX power sebelum `softAP()`** — regulator onboard beberapa batch SuperMini hanya
250 mA, sedangkan default 19.5 dBm bisa menyebabkan brownout sesaat → AP tidak muncul. Firmware
sudah menerapkan ini di [`src/main.cpp`](../src/main.cpp):

```cpp
// Turunkan TX power sebelum AP start — regulator onboard beberapa batch SuperMini
// hanya 250mA, default 19.5dBm bisa menyebabkan brownout sesaat → AP tidak muncul.
WiFi.setTxPower(WIFI_POWER_8_5dBm);
```

**2. Pin platform ke `espressif32@6.1.0`** — arduino-esp32 3.3.8 (bundled di versi terbaru)
punya bug SoftAP tidak muncul di scan. Versi 6.1.0 memakai arduino-esp32 2.0.x yang stabil.

**3. Varian antena IPEX** tanpa antena eksternal tidak memancar (perlu solder jumper RF).
Varian ceramic onboard langsung jalan.

---

## Known Constraints

- Sensor mode Auto: tidak bisa di-trigger, hanya dengar. Buang frame checksum gagal.
- RS485 heartbeat **pasif** — jangan kirim data tak diminta (risiko collision bus).
- Modbus register **append-only** — jangan geser offset lama (kompatibilitas master).
- Mute buzzer sengaja non-persisten (safety: power-cycle mengembalikan alarm).

---

## Lessons Learned

- **SoftAP gagal** karena 2 hal independen (TX power + versi platform, lihat catatan varian di
  atas). Keduanya harus diterapkan; salah satu saja tidak cukup di sebagian batch.
- **`AP_CHANNEL`** sempat bentrok router sekitar di channel 1 → pindah ke 6.
- **UART2 tidak ada** di ESP32-C3 — `HardwareSerial(2)` langsung crash; alokasikan
  UART0 (RS485) + UART1 (sensor), USB-CDC terpisah.
- **Kuantisasi jarak ke 10 mm** + median-of-7 menstabilkan pembacaan dari getaran ±5 mm.
