# ESP32 Round TFT Display Project

## Device Overview

**Board**: ESP32-TFT Development Board (EC-Buying or similar Chinese manufacturer)
**MCU**: ESP32-D0WD-V3 (dual-core 240MHz, 4MB Flash; PSRAM enabled in config but may not be present on this chip variant)
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
| Button        | GPIO | Notes |
|---------------|------|-------|
| Bottom button | 4    | User button, active LOW |
| Top button    | 19   | User button, active LOW |

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

## Architecture: Mode System

The display uses a mode-based architecture. Each mode gets full display access and is
defined as a `Mode` struct with function pointers (`enter`, `update`, `onButton`).

- **Mode manager** (`src/main.cpp`): Owns the TFT instance, mode registry, and button
  handling. Distinguishes short press (<500ms) from long press (>=500ms) on release.
- **Mode header** (`src/modes.h`): Defines the `Mode` struct and exports the shared `tft`.
- **Mode files** (`src/mode_*.cpp`): Each implements `enter()`, `update()`, `onButton()`.

### Button behavior

| Action | Effect |
|--------|--------|
| Top long press | Next mode (wraparound) |
| Bottom long press | Previous mode (wraparound) |
| Top short press | Forwarded to mode as `onButton(2)` |
| Bottom short press | Forwarded to mode as `onButton(1)` |

### Current modes

1. **Counter** (`src/mode_counter.cpp`): "San Jose" button-counter demo with press counts.
2. **Orbits** (`src/mode_orbits.cpp`): Animated colored dots orbiting the display center.
   Bottom button adds/removes orbiters; top button toggles pause.
3. **Birthday** (`src/mode_birthday.cpp`): JPEG gallery from SD card `/birthday` folder.
   Bottom button next image; top button previous image.

## SD Card

- Shares HSPI bus with TFT display (MOSI=15, SCLK=14 shared; SD CS=13, MISO=2)
- 1-bit SPI mode (DATA1/DATA2 not connected)
- FAT formatted
- Abstraction layer in `src/sdcard.h` / `src/sdcard.cpp`: `sdInit()`, `sdGetItems()`,
  `sdGetItem()`, `sdIsReady()`
- JPEG decoding via TJpg_Decoder library (renders directly to TFT via callback)
- SD init must happen after `tft.init()` — uses `tft.getSPIinstance()` to share bus

## Build System

- **PlatformIO** with **Arduino** framework
- **Libraries**: TFT_eSPI, TJpg_Decoder (configured via build_flags, no User_Setup.h editing needed)
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

After making code changes, always build and flash (`pio run -t upload`) before asking
the user to test. Do not ask the user to flash — only ask them to verify behavior on
the device.

## Serial Debugging

**Do not** use blocking commands to read the serial port (`cat /dev/...`, `pio device
monitor`). These never exit and `pio device monitor` requires a real terminal. Instead,
use the flash-then-capture pattern with pyserial (which is installed):

```bash
python3 << 'PYEOF'
import serial, time, sys, subprocess

# Flash firmware (resets device at the end)
result = subprocess.run(['pio', 'run', '-t', 'upload'], capture_output=True, text=True, timeout=60)
if result.returncode != 0:
    print("Flash failed!", file=sys.stderr)
    print(result.stderr[-500:], file=sys.stderr)
    sys.exit(1)

print("Flash complete, opening serial...", file=sys.stderr)
time.sleep(0.5)

ser = serial.Serial('/dev/cu.wchusbserial57290163461', 115200, timeout=0.5)
buf = b''
for i in range(16):  # 8 seconds
    chunk = ser.read(4096)
    if chunk:
        buf += chunk
ser.close()

print(buf.decode('utf-8', errors='replace'))
PYEOF
```

Notes:
- The serial port is `/dev/cu.wchusbserial57290163461`
- RTS-based reset does not work on this board's CH340 adapter — flashing is the
  reliable way to trigger a reboot and capture boot output
- macOS does not have `timeout` — use python with time-bounded reads instead
- The user must close any open serial monitor before flashing (port busy error)

## Key Conventions

- Pin definitions are centralized in `platformio.ini` build_flags (for TFT_eSPI) and
  in `src/pins.h` for application-level use
- All display drawing goes through TFT_eSPI library
- Button handling uses INPUT_PULLUP (buttons are active LOW)
- New modes: create `src/mode_foo.cpp`, define an `extern const Mode fooMode`, and
  add it to the `modes[]` array in `src/main.cpp`
- Mode definitions must use `extern const` to ensure external linkage in C++
