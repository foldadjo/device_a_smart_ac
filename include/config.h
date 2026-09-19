#pragma once

// ============================================================
// DEVICE A - SMART AC CONTROLLER
// ESP32-S3 N16R8
// Pin lama untuk IR/RS485 tetap disimpan agar bisa dipakai lagi nanti.
// Firmware aktif: AC IR remote, status display, speaker MAX98357A,
// microphone I2S, dan energy meter RS485.
// ============================================================

// ---------- Device identity ----------
#define DEVICE_ID "device-ac-01"

// ---------- Infrared ----------
#define PIN_IR_RX          4
#define PIN_IR_TX          10

// ---------- RS485 / MAX485 ----------
// UART1 dipakai khusus energy meter.
#define PIN_RS485_RX       16
#define PIN_RS485_TX       17
#define PIN_RS485_DE_RE    18

#define RS485_BAUD         2400
// Kehua/KHDM60-1P Modbus RTU default: 2400 baud, parity E/Even, 1 stop bit.
#define RS485_SERIAL_CONFIG SERIAL_8E1
#define MODBUS_SLAVE_ID    1

// ---------- Audio output - MAX98357A ----------
#define PIN_I2S_BCLK       8
#define PIN_I2S_LRC        9
#define PIN_I2S_DOUT       11

// ---------- Audio input - I2S microphone ----------
// Mic memakai I2S RX terpisah dari speaker.
#define PIN_MIC_I2S_BCLK   6
#define PIN_MIC_I2S_WS     7
#define PIN_MIC_I2S_DIN    15
#define MIC_SAMPLE_RATE_HZ 16000
#define MIC_LOG_INTERVAL_MS 1000

// ---------- OLED / LCD I2C ----------
#define PIN_OLED_SDA       11
#define PIN_OLED_SCL       21
#define OLED_I2C_ADDR      0x3C

// ---------- IR learning ----------
#define IR_CAPTURE_BUFFER_SIZE 1024
#define IR_TIMEOUT_MS      50
#define IR_MIN_UNKNOWN_SIZE 12

// Raw timing maximum yang disimpan ke RAM.
// 750 cukup untuk mayoritas frame AC.
// Bila frame remote AC sangat panjang, bisa dinaikkan.
#define MAX_IR_RAW_LEN     750

// ---------- Meter polling ----------
#define METER_POLL_INTERVAL_MS 2000
