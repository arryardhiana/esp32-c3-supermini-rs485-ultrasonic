#pragma once
#include <stdint.h>

// ── Versi Firmware ────────────────────────────────────────────────────────────
#define FW_VER_MAJOR 0
#define FW_VER_MINOR 4
#define FW_VER_PATCH 0
#define _FWSTR1(x) #x
#define _FWSTR(x)  _FWSTR1(x)
#define FW_VERSION_STR "v" _FWSTR(FW_VER_MAJOR) "." _FWSTR(FW_VER_MINOR) "." _FWSTR(FW_VER_PATCH)

// ── Pin ───────────────────────────────────────────────────────────────────────
#define SDA_PIN      4
#define SCL_PIN      5
#define SENSOR_RX    3    // A0221AU → ESP32 RX
#define SENSOR_TX   -1    // sensor hanya kirim; -1 = tidak dikonfigurasi
#define RS485_RX     6
#define RS485_TX     7
// Modul RS485 4-pin (VCC TX RX GND) — tanpa pin DE/RE, arah dikendalikan otomatis oleh modul

// ── UART Sensor A0221AU ───────────────────────────────────────────────────────
// Mode: UART Auto — sensor mengirim frame setiap ~100ms tanpa perlu trigger.
// Frame 4 byte: 0xFF  DATA_H  DATA_L  SUM
//   Jarak (mm)  = (DATA_H << 8) | DATA_L
//   Checksum    = (0xFF + DATA_H + DATA_L) & 0xFF
// Range valid   : 30–4500 mm (blind zone 3 cm)
// Ref: A02 Series datasheet (Shenzhen Dianying) + DYP-A02YUW community docs
#define SENSOR_BAUD       9600
#define SENSOR_FRAME_LEN  4
#define SENSOR_HEADER     0xFF
#define SENSOR_TIMEOUT_MS    5000  // error jika >5 detik tanpa frame valid
#define SENSOR_SPIKE_MAX_MM    20  // perubahan median maks per frame sebelum dianggap spike (mm)
#define SENSOR_SPIKE_HOLDOFF    5  // terima nilai baru paksa setelah N frame spike berturut-turut

// ── LCD 16×2 I2C ──────────────────────────────────────────────────────────────
#define LCD_ADDR_PRIMARY   0x27
#define LCD_ADDR_SECONDARY 0x3F
#define LCD_COLS  16
#define LCD_ROWS   2

// ── RS485 / Modbus RTU ────────────────────────────────────────────────────────
#define RS485_BAUD              9600
#define MODBUS_DEFAULT_SLAVE_ID 1

// ── WiFi SoftAP ───────────────────────────────────────────────────────────────
#define AP_SSID    "GensetMonitor"
#define AP_PASS    "genset123"
#define AP_CHANNEL 1

// ── NVS ───────────────────────────────────────────────────────────────────────
#define NVS_NAMESPACE    "fuelsensor"
#define NVS_KEY_SLAVE_ID "slave_id"
#define NVS_KEY_TANK_H   "tank_h"
#define NVS_KEY_TANK_L   "tank_l"
#define NVS_KEY_TANK_W   "tank_w"
#define NVS_KEY_SENS_OFF "sens_off"
#define NVS_KEY_CAL_N    "cal_n"

// ── Geometri Tangki ───────────────────────────────────────────────────────────
#define TANK_HEIGHT_MM   500   // tinggi fisik tangki dari dasar ke mulut (mm)
#define TANK_LENGTH_MM   400   // panjang dalam tangki (mm) — L×W×H/1e6 = liter
#define TANK_WIDTH_MM    500   // lebar dalam tangki (mm)   — contoh: 400×500×500=100L
#define SENSOR_OFFSET_MM  30   // jarak ujung sensor ke mulut tangki (mm)
                               // ≥30mm karena blind zone sensor A0221AU = 30mm

// ── Tabel Kalibrasi {jarak_sensor_mm, level_BBM_mm} ──────────────────────────
// PLACEHOLDER — ganti dengan data kalibrasi aktual tangki Anda.
// jarak_mm : pembacaan sensor dari atas (kecil = BBM penuh)
// level_mm : tinggi BBM dari dasar tangki
// Catatan: sensor ultrasonik mengukur jarak secara linear; non-linearitas
// berasal dari geometri tangki (silinder, trapesium, dll).
struct CalPoint { uint16_t jarak; uint16_t level; };
static const CalPoint CAL_TABLE[] = {
    { 30, 470},   // nyaris penuh
    {500,   0},   // kosong
};
static const uint8_t CAL_TABLE_SIZE = sizeof(CAL_TABLE) / sizeof(CalPoint);

// ── Peta Holding Register Modbus (append-only!) ───────────────────────────────
#define HREG_DISTANCE        0   // jarak sensor raw (mm)
#define HREG_LEVEL_CM        1   // level BBM setelah kalibrasi (cm)
#define HREG_LEVEL_PCT       2   // level BBM ×10  (0–1000 = 0.0%–100.0%)
#define HREG_VOLUME_DL       3   // volume ×10     (1000 = 100.0 L)
#define HREG_STATUS          4   // b0=sensor OK, b1=LCD OK, b2=WiFi AP OK
#define HREG_SLAVE_ID        5   // Modbus slave ID aktif (read-only bagi master)
#define HREG_RESERVED        6
#define HREG_FW_MAJOR        7   // versi firmware major
#define HREG_FW_MINOR_PATCH  8   // minor×100 + patch  (mis. v0.1.0 → 100)
#define HREG_COUNT           9   // total register
