# ESP32 Round TFT Display Project

## Device Overview

**Board**: ESP32-TFT Development Board (EC-Buying or similar Chinese manufacturer)
**MCU**: ESP32 WROVER module (dual-core 240MHz, 16MB Flash, 8MB PSRAM — 4MB addressable)
**Display**: 1.28" round IPS TFT, GC9A01 driver, 240x240 resolution, 65K colors, SPI interface
**Connectivity**: WiFi 2.4G + Bluetooth
**Power**: USB-C (5V) or 3.7V lithium battery with onboard charging (300mA default, 500mA max)
**Peripherals**: TF/SD card slot, 3 side buttons (1 power + 2 user-programmable), battery voltage ADC

## Display Details

- Controller: GC9A01
- Resolution: 240x240 RGB
- Active display area: Ø32.4mm (circular)
- Interface: 4-wire SPI
- Voltage: 2.8V–3.3V
- Backlight: 2x LED
- No touch capability (non-touch variant)

## Pin Mapping (verified from listing schematic)

### TFT Display (HSPI)
| Signal    | GPIO | Notes |
|-----------|------|-------|
| MOSI/SDA  | 15   | HSPI bus |
| SCLK/SCL  | 14   | HSPI bus |
| CS        | 5    | Chip select |
| DC        | 27   | Data/Command |
| RST       | 33   | Reset |
| BL        | 32   | Backlight enable |

### User Buttons
| Button | GPIO | Notes |
|--------|------|-------|
| SW1    | 4    | User button, active LOW |
| SW2    | 18   | User button, active LOW |

### TF/SD Card (shares HSPI bus with display)
| Signal     | GPIO | Notes |
|------------|------|-------|
| DATA3 (CS) | 13   | SD chip select |
| CMD (MOSI) | 15   | Shared with TFT SDA |
| CLK        | 14   | Shared with TFT SCL |
| DATA0      | 2    | SD data / MISO |

### Battery ADC
| Signal  | GPIO | Notes |
|---------|------|-------|
| BAT_ADC | 35   | Through 100K/100K voltage divider, requires jumper pad |

## Project Goal

Proof of concept: draw graphics to the round TFT display and respond to the 2 user
buttons for basic interaction.

## Build System

- **PlatformIO** with **Arduino** framework
- **Library**: TFT_eSPI (configured via build_flags, no User_Setup.h editing needed)
- **Board**: esp32dev (ESP32 WROVER module)

## Build & Upload

```bash
# Build
pio run

# Upload
pio run -t upload

# Serial monitor
pio device monitor -b 115200
```

## Key Conventions

- Pin definitions are centralized in `platformio.ini` build_flags (for TFT_eSPI) and
  in `src/pins.h` for application-level use
- All display drawing goes through TFT_eSPI library
- Button handling uses INPUT_PULLUP (buttons are active LOW)
