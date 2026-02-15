#include <Arduino.h>
#include <SD.h>
#include <LittleFS.h>
#include "modes.h"
#include "sdcard.h"
#include "istore.h"

#define BIRTHDAY_FOLDER "/birthday"
#define COPY_BUF_SIZE 4096
#define MAX_COPY_FILES 32

static enum {
  INTAKE_IDLE,
  INTAKE_DONE,
  INTAKE_ERROR,
  INTAKE_NO_SD,
  INTAKE_NO_ISTORE,
  INTAKE_NO_FILES
} intakeState;

static int filesCopied = 0;
static int filesSkipped = 0;
static int filesTotal = 0;

static void drawProgress(int current, int total, const char* filename) {
  tft.fillRect(20, 60, 200, 120, TFT_BLACK);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString("Intake", 120, 40);

  // Progress counter
  char buf[32];
  snprintf(buf, sizeof(buf), "%d / %d", current, total);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString(buf, 120, 100);

  // Filename (truncated to fit display)
  tft.setTextFont(2);
  char shortName[24];
  strncpy(shortName, filename, 23);
  shortName[23] = '\0';
  tft.drawString(shortName, 120, 130);

  // Progress bar
  int barW = 160;
  int barX = (240 - barW) / 2;
  int barY = 155;
  int barH = 12;
  tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
  if (total > 0) {
    int fillW = (barW - 2) * current / total;
    tft.fillRect(barX + 1, barY + 1, fillW, barH - 2, TFT_CYAN);
  }
}

static void drawResult() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  switch (intakeState) {
    case INTAKE_NO_SD:
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextFont(4);
      tft.drawString("No SD Card", 120, 100);
      tft.setTextFont(2);
      tft.drawString("Insert card & reboot", 120, 140);
      break;

    case INTAKE_NO_ISTORE:
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextFont(4);
      tft.drawString("Storage Error", 120, 100);
      tft.setTextFont(2);
      tft.drawString("Internal flash failed", 120, 140);
      break;

    case INTAKE_NO_FILES:
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextFont(4);
      tft.drawString("No Files", 120, 100);
      tft.setTextFont(2);
      tft.drawString("Nothing in /birthday", 120, 140);
      break;

    case INTAKE_ERROR: {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextFont(4);
      tft.drawString("Copy Error", 120, 80);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextFont(2);
      char buf[40];
      snprintf(buf, sizeof(buf), "%d copied, %d failed",
        filesCopied, filesTotal - filesCopied - filesSkipped);
      tft.drawString(buf, 120, 120);
      snprintf(buf, sizeof(buf), "%uKB / %uKB used",
        (unsigned)(istoreUsedBytes() / 1024),
        (unsigned)(istoreTotalBytes() / 1024));
      tft.drawString(buf, 120, 150);
      break;
    }

    case INTAKE_DONE: {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setTextFont(4);
      tft.drawString("Complete!", 120, 70);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextFont(2);
      char buf[48];
      snprintf(buf, sizeof(buf), "%d copied, %d skipped", filesCopied, filesSkipped);
      tft.drawString(buf, 120, 110);
      snprintf(buf, sizeof(buf), "%uKB / %uKB used",
        (unsigned)(istoreUsedBytes() / 1024),
        (unsigned)(istoreTotalBytes() / 1024));
      tft.drawString(buf, 120, 140);
      tft.drawString("Bottom btn: re-sync", 120, 180);
      break;
    }

    default:
      break;
  }
}

static bool copyFile(const char* srcPath, const char* dstPath) {
  File src = SD.open(srcPath, FILE_READ);
  if (!src) {
    Serial.printf("Intake: cannot open SD file %s\n", srcPath);
    return false;
  }

  File dst = LittleFS.open(dstPath, FILE_WRITE, true);
  if (!dst) {
    Serial.printf("Intake: cannot create file %s\n", dstPath);
    src.close();
    return false;
  }

  static uint8_t buf[COPY_BUF_SIZE];
  size_t totalWritten = 0;
  bool success = true;

  while (src.available()) {
    size_t bytesRead = src.read(buf, COPY_BUF_SIZE);
    if (bytesRead == 0) break;

    size_t bytesWritten = dst.write(buf, bytesRead);
    if (bytesWritten != bytesRead) {
      Serial.printf("Intake: write failed at %u bytes (disk full?)\n",
        (unsigned)totalWritten);
      success = false;
      break;
    }
    totalWritten += bytesWritten;
  }

  dst.close();
  src.close();

  if (!success) {
    LittleFS.remove(dstPath);
  } else {
    Serial.printf("Intake: copied %s (%u bytes)\n", dstPath, (unsigned)totalWritten);
  }
  return success;
}

static void runIntake() {
  tft.fillScreen(TFT_BLACK);
  filesCopied = 0;
  filesSkipped = 0;
  filesTotal = 0;

  if (!sdIsReady()) {
    intakeState = INTAKE_NO_SD;
    drawResult();
    return;
  }

  if (!istoreIsReady()) {
    intakeState = INTAKE_NO_ISTORE;
    drawResult();
    return;
  }

  // Scan SD card
  static SDItemList sdItems;
  sdItems = sdGetItems(BIRTHDAY_FOLDER);

  // Filter to valid files (skip dotfiles)
  struct CopyEntry { char sdPath[80]; char iPath[80]; bool needsCopy; };
  static CopyEntry entries[MAX_COPY_FILES];
  int entryCount = 0;

  for (int i = 0; i < sdItems.count && entryCount < MAX_COPY_FILES; i++) {
    // Skip dotfiles (macOS resource forks ._*, .DS_Store, etc.)
    if (sdItems.items[i].name[0] == '.') continue;

    CopyEntry& e = entries[entryCount];
    snprintf(e.sdPath, sizeof(e.sdPath), "%s/%s",
      BIRTHDAY_FOLDER, sdItems.items[i].name);
    snprintf(e.iPath, sizeof(e.iPath), "%s/%s",
      BIRTHDAY_FOLDER, sdItems.items[i].name);

    // Check if already copied (same name and size)
    if (istoreExists(e.iPath)) {
      SDItem existing = istoreGetItem(e.iPath);
      e.needsCopy = (existing.size != sdItems.items[i].size);
    } else {
      e.needsCopy = true;
    }
    entryCount++;
  }

  filesTotal = entryCount;

  if (entryCount == 0) {
    intakeState = INTAKE_NO_FILES;
    drawResult();
    return;
  }

  // Count files needing copy
  int needsCopyCount = 0;
  for (int i = 0; i < entryCount; i++) {
    if (entries[i].needsCopy) needsCopyCount++;
  }

  if (needsCopyCount == 0) {
    filesSkipped = entryCount;
    intakeState = INTAKE_DONE;
    drawResult();
    return;
  }

  // Ensure /birthday directory exists on LittleFS
  if (!LittleFS.exists(BIRTHDAY_FOLDER)) {
    LittleFS.mkdir(BIRTHDAY_FOLDER);
  }

  // Copy files with progress
  bool anyError = false;
  int progressIndex = 0;
  for (int i = 0; i < entryCount; i++) {
    if (!entries[i].needsCopy) {
      filesSkipped++;
      continue;
    }

    progressIndex++;
    const char* shortName = strrchr(entries[i].sdPath, '/');
    shortName = shortName ? shortName + 1 : entries[i].sdPath;
    drawProgress(progressIndex, needsCopyCount, shortName);

    // Check free space
    SDItem sdItem = sdGetItem(entries[i].sdPath);
    if (sdItem.size > istoreFreeBytes()) {
      Serial.printf("Intake: not enough space for %s (%u > %u free)\n",
        entries[i].sdPath, sdItem.size, (unsigned)istoreFreeBytes());
      anyError = true;
      break;
    }

    if (copyFile(entries[i].sdPath, entries[i].iPath)) {
      filesCopied++;
    } else {
      anyError = true;
      break;
    }
  }

  intakeState = anyError ? INTAKE_ERROR : INTAKE_DONE;
  drawResult();
}

static void intakeEnter() {
  runIntake();
}

static void intakeUpdate() {
  // Static display — nothing to animate
}

static void intakeButton(int btn) {
  if (btn == 1) {
    // Bottom button: re-run intake (re-sync)
    runIntake();
  }
}

extern const Mode intakeMode = {"Intake", intakeEnter, intakeUpdate, intakeButton};
