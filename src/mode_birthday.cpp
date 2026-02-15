#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <TJpg_Decoder.h>
#include "modes.h"
#include "istore.h"

static Preferences prefs;

#define BIRTHDAY_FOLDER "/birthday"
#define MAX_IMAGES 32

static char imagePaths[MAX_IMAGES][80];
static int imageCount = 0;
static int currentImage = 0;

static void showError(const char* line1, const char* line2) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.drawString(line1, 120, 110);
  if (line2) tft.drawString(line2, 120, 130);
}

static void drawCurrentImage() {
  if (imageCount == 0) {
    showError("No images", "Run Intake first");
    return;
  }

  const char* path = imagePaths[currentImage];
  Serial.printf("Birthday: showing %d/%d: %s\n", currentImage + 1, imageCount, path);

  // Get image dimensions to pick a scale factor
  uint16_t w = 0, h = 0;
  TJpgDec.getFsJpgSize(&w, &h, path, LittleFS);
  if (w == 0 || h == 0) {
    showError("Failed to load", path);
    return;
  }

  uint8_t scale = 1;
  while (scale < 8 && (w / (scale * 2) >= 240 || h / (scale * 2) >= 240)) {
    scale *= 2;
  }

  uint16_t sw = w / scale;
  uint16_t sh = h / scale;
  int16_t xOff = (240 - (int16_t)sw) / 2;
  int16_t yOff = (240 - (int16_t)sh) / 2;

  tft.fillScreen(TFT_BLACK);
  TJpgDec.setJpgScale(scale);
  // LittleFS reads from internal flash (not SPI), so no bus contention with TFT.
  // startWrite/endWrite keeps TFT CS asserted for faster block rendering.
  tft.startWrite();
  TJpgDec.drawFsJpg(xOff, yOff, path, LittleFS);
  tft.endWrite();

  // Image counter overlay
  char buf[16];
  snprintf(buf, sizeof(buf), "%d/%d", currentImage + 1, imageCount);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString(buf, 4, 4);
}

static void birthdayEnter() {
  imageCount = 0;
  currentImage = 0;

  if (!istoreIsReady()) {
    showError("Storage not", "available");
    return;
  }

  if (!coldStart) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.drawString("Loading...", 120, 120);
  }

  SDItemList items = istoreGetItems(BIRTHDAY_FOLDER);
  for (int i = 0; i < items.count && imageCount < MAX_IMAGES; i++) {
    // Skip dotfiles
    if (items.items[i].name[0] == '.') continue;
    if (items.items[i].type == SD_ITEM_JPEG) {
      snprintf(imagePaths[imageCount], sizeof(imagePaths[imageCount]),
               "%s/%s", BIRTHDAY_FOLDER, items.items[i].name);
      imageCount++;
    }
  }

  // Restore saved image index (clamped to valid range)
  prefs.begin("birthday", true);  // read-only
  currentImage = prefs.getInt("idx", 0);
  prefs.end();
  if (currentImage >= imageCount) currentImage = 0;

  Serial.printf("Birthday: found %d images, resuming at %d\n", imageCount, currentImage + 1);

  // On cold start the display already shows the correct image from before
  // reboot (GC9A01 GRAM persists while power is maintained). Skip the redraw.
  if (!coldStart) {
    drawCurrentImage();
  }
}

static void birthdayUpdate() {
  // Static display
}

static void birthdayButton(int btn) {
  if (imageCount == 0) return;
  if (btn == 1) {
    currentImage = (currentImage + 1) % imageCount;
  } else if (btn == 2) {
    currentImage = (currentImage - 1 + imageCount) % imageCount;
  }
  prefs.begin("birthday", false);
  prefs.putInt("idx", currentImage);
  prefs.end();
  drawCurrentImage();
}

extern const Mode birthdayMode = {"Birthday", birthdayEnter, birthdayUpdate, birthdayButton};
