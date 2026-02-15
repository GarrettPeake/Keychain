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

// Mode registry
extern const Mode modes[];
extern const int modeCount;
