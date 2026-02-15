#pragma once

#include <TFT_eSPI.h>

// Mode interface — each display mode implements these callbacks
struct Mode {
  const char* name;
  void (*enter)();           // called when switching to this mode
  void (*update)();          // called every loop iteration
  void (*onButton)(int btn); // called on short press (1=bottom, 2=top)
};

// Shared TFT instance (owned by main.cpp)
extern TFT_eSPI tft;

// True during the very first enter() call after boot — lets modes skip
// redundant drawing when the display already shows the correct content.
extern bool coldStart;

// Mode registry
extern const Mode modes[];
extern const int modeCount;
