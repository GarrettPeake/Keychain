#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include "pins.h"
#include "modes.h"
#include "sdcard.h"

TFT_eSPI tft = TFT_eSPI();

// --- TJpg_Decoder callback: render decoded JPEG blocks to TFT ---
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

// --- Mode declarations (defined in mode_*.cpp files) ---
extern const Mode counterMode;
extern const Mode orbitsMode;
extern const Mode birthdayMode;

const Mode modes[] = {birthdayMode, counterMode, orbitsMode};
const int modeCount = sizeof(modes) / sizeof(modes[0]);

static int currentMode = 0;

// --- Button state tracking for long/short press detection ---
#define LONG_PRESS_MS 500

struct ButtonState {
  int pin;
  bool wasPressed;
  bool longFired;      // true if long press already triggered while held
  unsigned long pressStart;
};

static ButtonState btn1 = {BTN1_PIN, false, false, 0};
static ButtonState btn2 = {BTN2_PIN, false, false, 0};

static void switchMode(int delta) {
  currentMode = (currentMode + delta + modeCount) % modeCount;
  Serial.printf("Mode switched to: %s (%d/%d)\n", modes[currentMode].name, currentMode + 1, modeCount);

  // Show brief mode name overlay
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString(modes[currentMode].name, 120, 120);
  delay(400);

  modes[currentMode].enter();
}

// Returns: 0 = no event, 1 = short press (on release), 2 = long press (while held)
static int checkButton(ButtonState& bs) {
  bool pressed = (digitalRead(bs.pin) == LOW);

  if (pressed && !bs.wasPressed) {
    // Just pressed — start timing (with debounce)
    delay(30);
    if (digitalRead(bs.pin) == LOW) {
      bs.wasPressed = true;
      bs.longFired = false;
      bs.pressStart = millis();
    }
  } else if (pressed && bs.wasPressed && !bs.longFired) {
    // Still held — check if threshold reached
    if (millis() - bs.pressStart >= LONG_PRESS_MS) {
      bs.longFired = true;
      return 2;
    }
  } else if (!pressed && bs.wasPressed) {
    // Released
    bs.wasPressed = false;
    if (!bs.longFired) return 1; // short press
    // long press already fired — ignore release
  }

  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ESP32 Round TFT Boot ===");

  // Enable backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  Serial.println("Backlight ON (GPIO 32)");

  // Initialize TFT
  tft.init();
  tft.setRotation(0);
  Serial.println("TFT initialized (GC9A01, 240x240)");

  // Initialize SD card (shares HSPI bus via tft.getSPIinstance())
  if (sdInit()) {
    Serial.println("SD card ready");
  } else {
    Serial.println("SD card not available (continuing without)");
  }

  // Initialize JPEG decoder
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  // Setup buttons
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  Serial.println("Buttons configured (bottom=GPIO4, top=GPIO19)");

  // Enter first mode
  Serial.printf("Starting mode: %s\n", modes[currentMode].name);
  modes[currentMode].enter();
}

void loop() {
  // Check buttons for short/long press
  int b1 = checkButton(btn1);
  int b2 = checkButton(btn2);

  if (b1 == 2) {
    // Bottom long press — previous mode
    switchMode(-1);
  } else if (b1 == 1) {
    // Bottom short press — forward to mode
    Serial.println("Bottom button short press");
    modes[currentMode].onButton(1);
  }

  if (b2 == 2) {
    // Top long press — next mode
    switchMode(1);
  } else if (b2 == 1) {
    // Top short press — forward to mode
    Serial.println("Top button short press");
    modes[currentMode].onButton(2);
  }

  // Let current mode update (for animations)
  modes[currentMode].update();

  delay(16); // ~60fps tick
}
