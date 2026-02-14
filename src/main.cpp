#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pins.h"

TFT_eSPI tft = TFT_eSPI();

// Display geometry
static const int16_t CX = 120;  // Center X
static const int16_t CY = 120;  // Center Y
static const int16_t R  = 119;  // Usable radius (display is 240x240 but circular)

// Color palette for the demo
static const uint16_t COLORS[] = {
  TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW,
  TFT_CYAN, TFT_MAGENTA, TFT_ORANGE, TFT_WHITE
};
static const int NUM_COLORS = sizeof(COLORS) / sizeof(COLORS[0]);

int currentDemo = 0;
const int NUM_DEMOS = 3;

// Button state tracking
bool btn1Last = HIGH;
bool btn2Last = HIGH;
unsigned long btn1DebounceTime = 0;
unsigned long btn2DebounceTime = 0;
const unsigned long DEBOUNCE_MS = 50;

// --- Drawing routines ---

void drawCirclePattern() {
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < 8; i++) {
    float angle = i * PI / 4.0;
    int16_t x = CX + cos(angle) * 60;
    int16_t y = CY + sin(angle) * 60;
    tft.fillCircle(x, y, 25, COLORS[i]);
  }
  tft.fillCircle(CX, CY, 20, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.drawString("HELLO", CX, CY, 2);
}

void drawBullseye() {
  tft.fillScreen(TFT_BLACK);
  for (int r = R; r > 0; r -= 15) {
    uint16_t color = COLORS[(r / 15) % NUM_COLORS];
    tft.fillCircle(CX, CY, r, color);
  }
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("BTN1: Next", CX, CY - 10, 2);
  tft.drawString("BTN2: Prev", CX, CY + 10, 2);
}

void drawRadialLines() {
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < 36; i++) {
    float angle = i * PI / 18.0;
    int16_t x1 = CX + cos(angle) * 30;
    int16_t y1 = CY + sin(angle) * 30;
    int16_t x2 = CX + cos(angle) * R;
    int16_t y2 = CY + sin(angle) * R;
    tft.drawLine(x1, y1, x2, y2, COLORS[i % NUM_COLORS]);
  }
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("RADIAL", CX, CY, 4);
}

void showDemo(int demo) {
  switch (demo) {
    case 0: drawCirclePattern(); break;
    case 1: drawBullseye();      break;
    case 2: drawRadialLines();   break;
  }
}

// --- Setup & Loop ---

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Round TFT - Proof of Concept");

  // Button pins
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);

  // Backlight on
  #ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  #endif

  // Initialize display
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Startup splash
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ESP32 TFT", CX, CY - 20, 4);
  tft.drawString("GC9A01 240x240", CX, CY + 10, 2);
  tft.drawString("Press a button!", CX, CY + 35, 2);
  delay(2000);

  showDemo(currentDemo);
}

void loop() {
  // BTN1: next demo
  bool btn1Reading = digitalRead(BTN1_PIN);
  if (btn1Reading == LOW && btn1Last == HIGH && (millis() - btn1DebounceTime) > DEBOUNCE_MS) {
    btn1DebounceTime = millis();
    currentDemo = (currentDemo + 1) % NUM_DEMOS;
    Serial.printf("BTN1 -> Demo %d\n", currentDemo);
    showDemo(currentDemo);
  }
  btn1Last = btn1Reading;

  // BTN2: previous demo
  bool btn2Reading = digitalRead(BTN2_PIN);
  if (btn2Reading == LOW && btn2Last == HIGH && (millis() - btn2DebounceTime) > DEBOUNCE_MS) {
    btn2DebounceTime = millis();
    currentDemo = (currentDemo - 1 + NUM_DEMOS) % NUM_DEMOS;
    Serial.printf("BTN2 -> Demo %d\n", currentDemo);
    showDemo(currentDemo);
  }
  btn2Last = btn2Reading;

  delay(10);
}
