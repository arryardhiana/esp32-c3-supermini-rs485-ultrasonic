# HARDWARE.md — Spesifikasi Hardware

> Sumber kebenaran tunggal untuk semua hardware project ini.
> **Pin di sini WAJIB sama dengan [`include/config.h`](../include/config.h)** — jika berbeda, config.h yang benar.
> Konfigurasi default firmware: lihat [`include/config.h`](../include/config.h), perilaku: lihat [`src/main.cpp`](../src/main.cpp).

---

## MCU

| Item | Detail |
|------|--------|
| Board | ESP32-C3 SuperMini |
| Identifier PlatformIO | `esp32-c3-devkitm-1` |
| Framework | Arduino via PlatformIO |
| Platform pin | `espressif32@6.1.0` (arduino-esp32 2.0.x) — **wajib**, lihat catatan SoftAP |
| Flash | 4 MB |
| RAM | ~400 KB SRAM |
| USB | USB-CDC native (GPIO20/21) — bukan CH340/CP2102 |

---

## Peta Pin (sesuai `config.h`)

| GPIO | Makro `config.h` | Fungsi | Arah | Catatan |
|:---:|---|---|:---:|---|
| 3  | `SENSOR_RX` | A0221AU TX → ESP RX (UART1) | in | Sensor kirim otomatis; pin TX sensor tidak dipakai |
| 4  | `SDA_PIN` | LCD I2C SDA | i/o | |
| 5  | `SCL_PIN` | LCD I2C SCL | out | |
| 6  | `RS485_TX` | ESP TX → modul RS485 DI (UART0) | out | |
| 7  | `RS485_RX` | modul RS485 RO → ESP RX (UART0) | in | |
| 10 | `BUZZER_PIN` | Buzzer aktif | out | **Active HIGH** (HIGH = bunyi) |
| 2  | — | strapping | — | ⚠ jangan dipakai I/O bebas (mode boot) |
| 8  | — | LED bawaan (active-low) | — | ⚠ strapping |
| 9  | — | tombol BOOT | — | ⚠ strapping |
| 20/21 | — | USB-CDC (`Serial`) | — | ⚡ jangan dipakai selama Serial aktif |

```
              ┌──────[USB-C]──────┐
       3V3  ──┤                   ├── GPIO5   → LCD SCL
        5V  ──┤                   ├── GPIO6   → RS485 DI (TX)
       GND  ──┤  ESP32-C3         ├── GPIO7   ← RS485 RO (RX)
     GPIO0  ──┤  SuperMini        ├── GPIO8   ⚠ strapping (LED)
     GPIO1  ──┤                   ├── GPIO9   ⚠ BOOT
     GPIO2  ──┤  ⚠ strapping      ├── GPIO10  → BUZZER (active HIGH)
     GPIO3  ──┤  ← SENSOR RX      ├── GPIO20  ⚡ USB-CDC
     GPIO4  ──┤  ↔ LCD SDA        ├── GPIO21  ⚡ USB-CDC
              └───────────────────┘
```

---

## Modul

### Sensor Ultrasonik A0221AU (UART Auto)
| Parameter | Value |
|-----------|-------|
| Antarmuka | UART (`HardwareSerial(1)` / UART1) |
| Baud / config | 9600 / 8N1 |
| Mode | **UART Auto** — sensor kirim frame ~tiap 100 ms tanpa trigger |
| Frame | 4 byte: `0xFF  DATA_H  DATA_L  SUM` |
| Jarak (mm) | `(DATA_H << 8) \| DATA_L` |
| Checksum | `(0xFF + DATA_H + DATA_L) & 0xFF` |
| Range valid | 30–4500 mm (blind zone 3 cm) |
| Pin | RX = `SENSOR_RX` (GPIO3); TX sensor tidak disambung |

### LCD 16×2 I2C
| Parameter | Value |
|-----------|-------|
| Antarmuka | I2C |
| Alamat | auto-scan `0x27` lalu `0x3F` saat boot |
| Pin | SDA = `SDA_PIN` (GPIO4), SCL = `SCL_PIN` (GPIO5) |
| Library | `marcoschwartz/LiquidCrystal_I2C@^1.1.4` |

### RS485 / Modbus RTU (Slave)
| Parameter | Value |
|-----------|-------|
| UART | `HardwareSerial(0)` / UART0 (terpisah dari USB-CDC) |
| Baud / config | 9600 / 8N1 |
| Mode | Modbus RTU **Slave**, default ID = 1 (1–247, simpan ke NVS) |
| Modul | **4-pin (VCC TX RX GND)** — auto-direction, **tanpa pin DE/RE** |
| Pin | TX = `RS485_TX` (GPIO6) → DI, RX = `RS485_RX` (GPIO7) ← RO |
| Library | `emelianov/modbus-esp8266@^4.1.0` |

### Buzzer Aktif
| Parameter | Value |
|-----------|-------|
| Tipe | Buzzer aktif (bukan pasif, tanpa PWM) |
| Logic | **Active HIGH** (HIGH = berbunyi) |
| Pin | `BUZZER_PIN` (GPIO10) |
| Driver | jika arus >10 mA, tambah transistor NPN (mis. 2N2222) |

### Web Dashboard (WiFi SoftAP)
| Parameter | Value |
|-----------|-------|
| Mode | WiFi SoftAP |
| SSID / Pass | `AP_SSID` (`GensetMonitor`) / `AP_PASS` (`genset123`) |
| Channel | `AP_CHANNEL` = 6 |
| IP / Port | 192.168.4.1 / 80 |
| TX power | `WIFI_POWER_8_5dBm` (diturunkan, lihat catatan) |

---

## Wiring Ringkas

```
A0221AU  ④TX ──────────► GPIO3        (VCC→5V, GND→GND, RX sensor: NC)
LCD I2C  SDA ─────────── GPIO4
         SCL ─────────── GPIO5        (VCC→5V, GND→GND)
RS485    DI  ◄────────── GPIO6
         RO  ──────────► GPIO7        (VCC→3V3/5V, GND→GND; A/B → bus)
Buzzer   I/O ◄────────── GPIO10       (VCC→5V, GND→GND)
```

---

## Catatan Kritis Hardware

1. **Hanya 2 UART** — ESP32-C3 punya UART0 & UART1 saja. `HardwareSerial(2)` crash. `Serial` (USB-CDC) terpisah dari UART0, jadi UART0 bebas dipakai RS485 dan UART1 untuk sensor.
2. **SoftAP tidak muncul** — dua sebab umum di SuperMini:
   - Bug arduino-esp32 3.3.8 → **pin `platform = espressif32@6.1.0`** (arduino-esp32 2.0.x).
   - Regulator onboard lemah (250 mA) brownout pada TX power default 19.5 dBm → firmware set `WiFi.setTxPower(WIFI_POWER_8_5dBm)` sebelum `softAP()`.
3. **Varian antena IPEX** tanpa antena eksternal tidak memancar — perlu pasang antena + solder jumper RF. Varian ceramic onboard langsung jalan.
4. **NVS via `Preferences`** (bukan `EEPROM.h` AVR) untuk simpan Slave ID, geometri tangki, kalibrasi.

---

## Override Varian

> Isi hanya jika board bukan ESP32-C3 SuperMini. Kosong = pakai default di atas.

_(tidak ada — project ini memakai ESP32-C3 SuperMini)_
