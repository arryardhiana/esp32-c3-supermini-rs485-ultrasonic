# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash Commands

```bash
~/.platformio/penv/bin/pio run                  # build — must succeed before every commit
~/.platformio/penv/bin/pio run -t upload        # flash to device
~/.platformio/penv/bin/pio device monitor       # serial monitor @115200
```

**Always** use the full path `~/.platformio/penv/bin/pio` — the `pio` on PATH breaks under Python 3.14.

If auto-reset fails during upload, hold **BOOT** then press **RESET**, or add `upload_resetmethod = default` to `platformio.ini`.

## Project Overview

ESP32-C3 SuperMini monitoring level BBM genset:
- Membaca level BBM via sensor ultrasonik **A0221AU** (UART, frame non-blocking)
- Menampilkan di **LCD 16x2** (I2C)
- Melayani **Modbus RTU slave** via RS485
- Web dashboard via **WiFi SoftAP**

Board: `esp32-c3-devkitm-1` · Framework: Arduino · Build system: PlatformIO

## File Structure

| File | Isi |
|---|---|
| `include/config.h` | **Semua** konfigurasi: pin, kalibrasi, alamat, versi FW, AP, NVS key |
| `src/main.cpp` | Scheduler non-blocking, sensor UART, Modbus, LCD, web server |
| `platformio.ini` | Board & library dependencies |

## Aturan Wajib

**Non-blocking mutlak** — `delay()` di `loop()` dilarang (kecuali `delay(50)` sekali saat boot). Semua tugas periodik pakai scheduler `due()` + `millis()`. Sensor A0221AU: baca UART karakter per karakter di `loop()`, parse frame saat delimiter tiba.

**Init non-fatal** — jika LCD atau sensor tidak merespons saat boot, sistem tetap jalan dan log peringatan. Tidak boleh blocking/hang.

**Config-driven** — pin, kalibrasi, timeout, kredensial AP semuanya di `config.h`. Tidak boleh hardcode di `main.cpp`.

**Modbus register map = append-only** — hanya tambah register di offset terbesar. Jangan ubah/geser offset lama. Saat menambah: update `HREG_*` + `HREG_COUNT`, isi nilainya, update banner serial + tabel register di README.

**Versioning** — ubah hanya `FW_VER_MAJOR/MINOR/PATCH` di `config.h`; string `FW_VERSION` dan register versi (reg 7/8) ikut otomatis via makro. Setelah rilis: update README + `git tag -a vX.Y.Z`.

**Update README setiap perubahan berhasil** — fitur, pin, register, kalibrasi, status. Jangan tunggu diminta.

**Komentar kode dalam bahasa Indonesia.**

## Hardware Gotchas

- **Strapping pin** — hindari GPIO2, GPIO8, GPIO9 untuk I/O bebas: GPIO2 mempengaruhi mode boot, GPIO8 terhubung ke LED bawaan (active-low), GPIO9 adalah tombol BOOT.
- **Tidak ada SoftwareSerial, ESP32-C3 hanya punya UART0 dan UART1** — `HardwareSerial(2)` tidak ada, langsung crash. `Serial` (USB-CDC) terpisah dari UART0 sehingga UART0 tetap bebas dipakai. Gunakan `HardwareSerial(1)` untuk A0221AU dan `HardwareSerial(0)` untuk RS485. Pin TX/RX dikonfigurasi via `begin(baud, config, RX_PIN, TX_PIN)`.
- **USB-CDC** (`Serial`) = GPIO20/GPIO21. Pin ini terekspos secara fisik di board (posisi bawah sisi kanan), tetapi jangan dipakai untuk I/O eksternal selama `Serial` aktif.
- **NVS** — gunakan library `Preferences` (bukan `EEPROM.h` AVR) untuk simpan Slave ID. Commit ke NVS hanya saat user simpan, bukan di loop.
- **LCD** — scan alamat I2C saat boot (`0x27` atau `0x3F`). Hindari `lcd.clear()` terlalu sering (~2ms, blokir bus). `SDA_PIN` & `SCL_PIN` didefinisikan di `config.h`.
- **Geometri tangki non-linear** — sensor ultrasonik mengukur jarak secara linear, tetapi konversi `jarak_mm → level_mm` bisa non-linear tergantung bentuk tangki (silinder, trapesium, dll). Pakai tabel kalibrasi `CAL_TABLE` + interpolasi, bukan asumsi linear 2-titik. Anti-spike: median-of-7 + gate plausibilitas.
- **RS485 heartbeat pasif** — amati poll master via callback saja. Jangan kirim data tak diminta ke bus (risiko collision).
