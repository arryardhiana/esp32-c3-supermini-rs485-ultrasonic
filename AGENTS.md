# Panduan AI / Agent — Fuel Sensor Genset (ESP32-C3 SuperMini)

Instruksi untuk AI coding tool (Claude Code, Cursor, dll) yang bekerja di repo ini.
Detail lengkap ada di [README.md](README.md) — file ini ringkasan aturan yang **wajib dipatuhi**.

## Konteks proyek
Monitoring BBM genset: **ESP32-C3 SuperMini** sebagai **Modbus RTU slave** (RS485), baca level BBM via sensor ultrasonik **A0221AU** (antarmuka **UART**), tampil di **LCD 16x2** (I2C) & web dashboard SoftAP. PlatformIO, board `esp32-c3-devkitm-1`. Komentar kode **bahasa Indonesia**.

## Perintah penting
```bash
~/.platformio/penv/bin/pio run            # build (WAJIB sukses sebelum commit)
~/.platformio/penv/bin/pio run -t upload  # flash
~/.platformio/penv/bin/pio device monitor # serial @115200
```
- **Selalu** pakai `~/.platformio/penv/bin/pio` — `pio` di PATH kena error Python 3.14.
- ESP32-C3 SuperMini: saat upload mungkin perlu tahan tombol **BOOT** lalu tekan **RESET**, atau set `upload_resetmethod = default` di platformio.ini jika auto-reset gagal.
- Setelah tiap perubahan: `pio run` → pastikan **[SUCCESS]** sebelum commit.

## Aturan WAJIB

1. **Non-blocking mutlak** — dilarang `delay()` di `loop()` (kecuali `delay(50)` sekali saat boot). Pakai scheduler `due()` + `millis()`. Sensor ultrasonik A0221AU: baca frame UART secara non-blocking via `HardwareSerial` (`Serial1` atau `Serial2`) — karakter per karakter di `loop()`, parse frame lengkap saat delimiter tiba. LCD 16x2: update display via scheduler, jangan blokir `loop()`.

2. **Init non-fatal** — bila modul (LCD/sensor) tak terdeteksi atau UART sensor tidak merespons saat boot, sistem tetap jalan + log peringatan. Jangan blocking/hang.

3. **Config-driven** — pin, kalibrasi, alamat, timeout, kredensial AP semuanya di [include/config.h](include/config.h). Jangan hardcode di `main.cpp`.

4. **Peta register Modbus = append-only** — hanya tambah register di offset terbesar. JANGAN ubah/geser offset lama (jaga kompatibilitas master). Saat menambah: update `HREG_*` + `HREG_COUNT`, isi nilainya, update banner serial + tabel register di README. Nilai = integer berskala (hindari float).

5. **Versioning tiap rilis** — ubah HANYA `FW_VER_MAJOR/MINOR/PATCH` di config.h (string `FW_VERSION` & register versi reg 7/8 ikut otomatis via makro). PATCH=fix, MINOR=fitur kompatibel, MAYOR=tak-kompatibel. Lalu update Riwayat Versi + badge di README, `git tag -a vX.Y.Z`, `git push origin main --tags`.

6. **Update README tiap perubahan berhasil** — fitur, pin, register, kalibrasi, status. Jangan menunggu diminta. Branch utama: `main`.

## Gotchas
- **ESP32-C3 strapping pin** (hindari untuk I/O bebas): GPIO2, GPIO8, GPIO9 (BOOT). RS485 dan sensor UART alokasikan di pin aman — definisikan di config.h.
- **Tidak ada SoftwareSerial** — ESP32-C3 punya **2** hardware UART saja: UART0 dan UART1. `HardwareSerial(2)` tidak ada dan langsung crash. `Serial` (USB-CDC) adalah entitas terpisah dari UART0, sehingga UART0 tetap bisa dipakai bebas. Gunakan `HardwareSerial SerialSensor(1)` untuk A0221AU dan `HardwareSerial SerialRS485(0)` untuk RS485. Pin TX/RX bebas dikonfigurasi via `begin(baud, config, RX_PIN, TX_PIN)`.
- **USB-CDC (`Serial0`)** di ESP32-C3 SuperMini = serial via USB langsung (bukan CH340/CP2102). Jangan pakai GPIO20/GPIO21 untuk keperluan lain jika Serial0 aktif.
- WiFi SoftAP pada ESP32-C3 tidak mengganggu hardware UART (berbeda dengan ESP8266 bit-bang) — namun tetap buang frame UART yang parse-nya gagal dan set timeout frame wajar.
- **NVS menggantikan EEPROM** — gunakan library `Preferences` (built-in ESP32 Arduino) untuk simpan Slave ID & pengaturan. Jangan pakai `EEPROM.h` versi AVR. Commit ke NVS hanya saat user simpan (bukan di loop).
- LCD 16x2 via I2C: scan alamat I2C saat boot (`0x27` atau `0x3F`), log ke serial jika tidak ditemukan. Hindari `lcd.clear()` terlalu sering (lambat ~2ms, blokir bus I2C). Pin I2C ESP32-C3 bebas dikonfigurasi — definisikan `SDA_PIN` & `SCL_PIN` di config.h.
- Sensor ultrasonik mengukur jarak secara linear, tetapi konversi `jarak_mm → level_mm` bisa NON-LINEAR tergantung geometri tangki (silinder, trapesium, dll) → pakai tabel kalibrasi `CAL_TABLE` + interpolasi, bukan asumsi linear 2-titik.
- Anti-spike level: median-of-7 + gate plausibilitas (buang sampel di luar rentang tabel atau di luar batas fisik tangki).
- Heartbeat RS485 **pasif** (amati poll master via callback) — JANGAN kirim data tak diminta ke bus (risiko collision).

## Struktur
- [include/config.h](include/config.h) — semua konfigurasi (pin, kalibrasi, register, versi, AP, NVS key)
- [src/main.cpp](src/main.cpp) — scheduler non-blocking, sensor UART, Modbus, LCD, web server
- [platformio.ini](platformio.ini) — board & dependencies
