#include <Arduino.h>
#include "modes.h"

#define CENTER_X 120
#define CENTER_Y 120
#define BG_COLOR TFT_BLACK

#define MAX_ORBITERS 8
#define INITIAL_ORBITERS 3
#define DOT_RADIUS 5

static const uint16_t palette[] = {
  TFT_RED, TFT_GREEN, TFT_CYAN, TFT_MAGENTA,
  TFT_YELLOW, TFT_ORANGE, TFT_PINK, TFT_WHITE
};

struct Orbiter {
  float angle;     // current angle in radians
  float speed;     // radians per frame
  float radius;    // orbit radius from center
  uint16_t color;
  int16_t prevX, prevY; // previous drawn position for erasure
};

static Orbiter orbiters[MAX_ORBITERS];
static int numOrbiters = 0;
static bool paused = false;

static void initOrbiter(int i) {
  orbiters[i].angle = (TWO_PI / MAX_ORBITERS) * i;
  orbiters[i].speed = 0.02f + 0.015f * i;
  orbiters[i].radius = 30.0f + 15.0f * i;
  orbiters[i].color = palette[i % 8];
  orbiters[i].prevX = -1;
  orbiters[i].prevY = -1;
}

static void orbitsEnter() {
  tft.fillScreen(BG_COLOR);

  // Draw faint center dot
  tft.fillCircle(CENTER_X, CENTER_Y, 2, TFT_DARKGREY);

  numOrbiters = INITIAL_ORBITERS;
  paused = false;
  for (int i = 0; i < MAX_ORBITERS; i++) {
    initOrbiter(i);
  }

  // Draw orbit paths as faint rings
  for (int i = 0; i < numOrbiters; i++) {
    tft.drawCircle(CENTER_X, CENTER_Y, (int)orbiters[i].radius, 0x2104);
  }
}

static void orbitsUpdate() {
  if (paused) return;

  for (int i = 0; i < numOrbiters; i++) {
    Orbiter& o = orbiters[i];

    // Erase previous position
    if (o.prevX >= 0) {
      tft.fillCircle(o.prevX, o.prevY, DOT_RADIUS, BG_COLOR);
      // Redraw the orbit ring segment we just erased over
      tft.drawCircle(CENTER_X, CENTER_Y, (int)o.radius, 0x2104);
    }

    // Advance angle
    o.angle += o.speed;
    if (o.angle > TWO_PI) o.angle -= TWO_PI;

    // Compute new position
    int16_t nx = CENTER_X + (int16_t)(cos(o.angle) * o.radius);
    int16_t ny = CENTER_Y + (int16_t)(sin(o.angle) * o.radius);

    // Draw dot at new position
    tft.fillCircle(nx, ny, DOT_RADIUS, o.color);

    o.prevX = nx;
    o.prevY = ny;
  }

  // Redraw center dot in case an orbiter passed over it
  tft.fillCircle(CENTER_X, CENTER_Y, 2, TFT_DARKGREY);
}

static void orbitsButton(int btn) {
  if (btn == 1) {
    // Bottom button: add/remove orbiter
    if (numOrbiters < MAX_ORBITERS) {
      // Add one — draw its orbit ring
      tft.drawCircle(CENTER_X, CENTER_Y, (int)orbiters[numOrbiters].radius, 0x2104);
      numOrbiters++;
    } else {
      // Wrap back to 1
      // Erase old dots and rings
      tft.fillScreen(BG_COLOR);
      tft.fillCircle(CENTER_X, CENTER_Y, 2, TFT_DARKGREY);
      numOrbiters = 1;
      for (int i = 0; i < MAX_ORBITERS; i++) {
        initOrbiter(i);
      }
      tft.drawCircle(CENTER_X, CENTER_Y, (int)orbiters[0].radius, 0x2104);
    }
  } else if (btn == 2) {
    // Top button: toggle pause
    paused = !paused;
  }
}

extern const Mode orbitsMode = {"Orbits", orbitsEnter, orbitsUpdate, orbitsButton};
