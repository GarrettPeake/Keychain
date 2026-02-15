#include <Arduino.h>
#include <TJpg_Decoder.h>
#include "modes.h"
#include "sdcard.h"

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
    showError("No images in", BIRTHDAY_FOLDER);
    return;
  }

  const char* path = imagePaths[currentImage];
  Serial.printf("Birthday: showing %d/%d: %s\n", currentImage + 1, imageCount, path);

  // Get image dimensions to pick a scale factor
  uint16_t w = 0, h = 0;
  TJpgDec.getSdJpgSize(&w, &h, path);
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
  // Do NOT use startWrite/endWrite here — drawSdJpg alternates between
  // SD reads and TFT writes on the shared SPI bus. Holding TFT CS low
  // would cause bus contention during SD reads.
  TJpgDec.drawSdJpg(xOff, yOff, path);

  // Image counter overlay
  char buf[16];
  snprintf(buf, sizeof(buf), "%d/%d", currentImage + 1, imageCount);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString(buf, 4, 4);
}

static void birthdayEnter() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("Loading...", 120, 120);

  imageCount = 0;
  currentImage = 0;

  if (!sdIsReady()) {
    showError("SD card not", "available");
    return;
  }

  SDItemList items = sdGetItems(BIRTHDAY_FOLDER);
  for (int i = 0; i < items.count && imageCount < MAX_IMAGES; i++) {
    // Skip macOS resource fork files (._*)
    if (items.items[i].name[0] == '.') continue;
    if (items.items[i].type == SD_ITEM_JPEG) {
      snprintf(imagePaths[imageCount], sizeof(imagePaths[imageCount]),
               "%s/%s", BIRTHDAY_FOLDER, items.items[i].name);
      imageCount++;
    }
  }

  Serial.printf("Birthday: found %d images\n", imageCount);
  drawCurrentImage();
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
  drawCurrentImage();
}

extern const Mode birthdayMode = {"Birthday", birthdayEnter, birthdayUpdate, birthdayButton};
