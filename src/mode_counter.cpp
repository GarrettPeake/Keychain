#include <Arduino.h>
#include "modes.h"

#define CENTER_X 120
#define CENTER_Y 120

#define BG_COLOR    TFT_BLACK
#define RING_COLOR  TFT_CYAN
#define TEXT_COLOR  TFT_WHITE
#define BTN_COLOR   TFT_GREEN

static int pressCount1 = 0;
static int pressCount2 = 0;

static void drawUI() {
  tft.fillScreen(BG_COLOR);

  // Circular ring border
  for (int r = 118; r <= 120; r++) {
    tft.drawCircle(CENTER_X, CENTER_Y, r, RING_COLOR);
  }

  // Title
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

static void counterEnter() {
  pressCount1 = 0;
  pressCount2 = 0;
  drawUI();
}

static void counterUpdate() {
  // Static display — nothing to animate
}

static void counterButton(int btn) {
  if (btn == 1) pressCount1++;
  else if (btn == 2) pressCount2++;
  drawUI();
}

extern const Mode counterMode = {"Counter", counterEnter, counterUpdate, counterButton};
