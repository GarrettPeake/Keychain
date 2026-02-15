#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "sdcard.h"
#include "pins.h"

extern TFT_eSPI tft;

static bool ready = false;

static SDItemType classifyFile(const char* name) {
  const char* dot = strrchr(name, '.');
  if (!dot) return SD_ITEM_OTHER;
  if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
    return SD_ITEM_JPEG;
  if (strcasecmp(dot, ".md") == 0)
    return SD_ITEM_MARKDOWN;
  return SD_ITEM_OTHER;
}

bool sdInit() {
  Serial.printf("SD: init CS=%d, MISO=%d, MOSI=%d, SCLK=%d\n",
    SD_CS_PIN, SD_MISO_PIN, TFT_MOSI, TFT_SCLK);

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  // TFT_eSPI initializes HSPI without MISO (displays don't read back).
  // The ESP32 SPI library guards pin attachment behind an _initted flag,
  // so calling begin() again won't attach new pins. We must end() first
  // to reset the flag, then re-begin() with MISO included.
  SPIClass& spi = tft.getSPIinstance();
  spi.end();
  spi.begin(TFT_SCLK, SD_MISO_PIN, TFT_MOSI, -1);

  // Try mounting at lower frequency first (more reliable for init)
  Serial.println("SD: attempting mount...");
  if (!SD.begin(SD_CS_PIN, spi, 4000000)) {
    Serial.println("SD: mount at 4MHz failed, retrying at 1MHz...");
    if (!SD.begin(SD_CS_PIN, spi, 1000000)) {
      Serial.println("SD: mount failed at all speeds");
      return false;
    }
  }

  uint8_t cardType = SD.cardType();
  Serial.printf("SD: cardType=%d\n", cardType);
  if (cardType == CARD_NONE) {
    Serial.println("SD: no card detected");
    SD.end();
    return false;
  }

  ready = true;
  Serial.printf("SD: %s, %lluMB\n",
    cardType == CARD_MMC ? "MMC" :
    cardType == CARD_SD  ? "SD"  :
    cardType == CARD_SDHC ? "SDHC" : "?",
    SD.cardSize() / (1024 * 1024));
  return true;
}

bool sdIsReady() {
  return ready;
}

SDItemList sdGetItems(const char* folder) {
  SDItemList result;
  result.count = 0;

  if (!ready) return result;

  File root = SD.open(folder);
  if (!root || !root.isDirectory()) {
    Serial.printf("SD: cannot open folder %s\n", folder);
    if (root) root.close();
    return result;
  }

  File file = root.openNextFile();
  while (file && result.count < MAX_SD_ITEMS) {
    SDItem& item = result.items[result.count];
    strncpy(item.name, file.name(), sizeof(item.name) - 1);
    item.name[sizeof(item.name) - 1] = '\0';
    item.type = file.isDirectory() ? SD_ITEM_OTHER : classifyFile(file.name());
    item.size = file.size();
    result.count++;
    file.close();
    file = root.openNextFile();
  }
  if (file) file.close();
  root.close();
  return result;
}

SDItem sdGetItem(const char* path) {
  SDItem item;
  item.type = SD_ITEM_NONE;
  item.size = 0;
  item.name[0] = '\0';

  if (!ready) return item;

  File file = SD.open(path);
  if (!file) return item;

  const char* slash = strrchr(path, '/');
  const char* name = slash ? (slash + 1) : path;
  strncpy(item.name, name, sizeof(item.name) - 1);
  item.name[sizeof(item.name) - 1] = '\0';
  item.type = file.isDirectory() ? SD_ITEM_OTHER : classifyFile(name);
  item.size = file.size();
  file.close();
  return item;
}
