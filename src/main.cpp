#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "pins.h"

TFT_eSPI tft = TFT_eSPI();

// Screen center
#define CENTER_X 120
#define CENTER_Y 120

// Colors
#define BG_COLOR    TFT_BLACK
#define RING_COLOR  TFT_CYAN
#define TEXT_COLOR  TFT_WHITE
#define BTN_COLOR   TFT_GREEN

int pressCount1 = 0;
int pressCount2 = 0;

void drawUI() {
  tft.fillScreen(BG_COLOR);

  // Draw a circular ring to highlight the round display shape
  for (int r = 118; r <= 120; r++) {
    tft.drawCircle(CENTER_X, CENTER_Y, r, RING_COLOR);
  }

  // Title text
  tft.setTextColor(TEXT_COLOR, BG_COLOR);
  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(4);
  tft.drawString("San Jose", CENTER_X, 40);

  // Subtitle
  tft.setTextFont(2);
  tft.drawString("GC9A01 240x240", CENTER_X, 75);

  // Button press counts
  tft.setTextColor(BTN_COLOR, BG_COLOR);
  tft.setTextFont(4);
  char buf[32];
  snprintf(buf, sizeof(buf), "Bottom: %d", pressCount1);
  tft.drawString(buf, CENTER_X, 120);
  snprintf(buf, sizeof(buf), "Top: %d", pressCount2);
  tft.drawString(buf, CENTER_X, 155);

  // Footer
  tft.setTextColor(TFT_DARKGREY, BG_COLOR);
  tft.setTextFont(2);
  tft.drawString("Press buttons!", CENTER_X, 200);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ESP32 Round TFT Boot ===");

  // Enable backlight (GPIO 32, active HIGH)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  Serial.println("Backlight ON (GPIO 32)");

  // Initialize TFT
  tft.init();
  tft.setRotation(0);
  Serial.println("TFT initialized (GC9A01, 240x240)");

  // Setup buttons with internal pull-ups (active LOW)
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  Serial.println("Buttons configured (bottom=GPIO4, top=GPIO19)");

  // Draw initial UI
  drawUI();
  Serial.println("UI drawn — display should be visible");
}

void loop() {
  bool redraw = false;

  // Check button 1 (active LOW, simple debounce)
  if (digitalRead(BTN1_PIN) == LOW) {
    delay(50);  // debounce
    if (digitalRead(BTN1_PIN) == LOW) {
      pressCount1++;
      Serial.printf("Bottom button pressed (%d)\n", pressCount1);
      redraw = true;
      // Wait for release
      while (digitalRead(BTN1_PIN) == LOW) {
        delay(10);
      }
    }
  }

  // Check button 2 (active LOW, simple debounce)
  if (digitalRead(BTN2_PIN) == LOW) {
    delay(50);  // debounce
    if (digitalRead(BTN2_PIN) == LOW) {
      pressCount2++;
      Serial.printf("Top button pressed (%d)\n", pressCount2);
      redraw = true;
      // Wait for release
      while (digitalRead(BTN2_PIN) == LOW) {
        delay(10);
      }
    }
  }

  if (redraw) {
    drawUI();
  }

  delay(20);
}
