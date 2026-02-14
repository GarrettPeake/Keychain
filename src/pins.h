#pragma once

// ============================================================
// Pin Definitions for ESP32 1.28" Round TFT Board
// Pin mapping verified from listing schematic
// ============================================================

// TFT display pins are configured in platformio.ini build_flags
// (TFT_MOSI=15, TFT_SCLK=14, TFT_CS=5, TFT_DC=27, TFT_RST=33, TFT_BL=32)

// User buttons (active LOW — press pulls to GND)
#define BTN1_PIN  4   // SW1
#define BTN2_PIN  18  // SW2

// Battery ADC (100K/100K voltage divider, requires jumper pad shorted)
#define BAT_ADC_PIN 35
