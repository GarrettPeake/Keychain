#include <Arduino.h>
#include <SD.h>
#include <LittleFS.h>
#include "modes.h"
#include "sdcard.h"
#include "istore.h"

#define COPY_BUF_SIZE 4096
#define MAX_FOLDERS 16

static enum {
  INTAKE_IDLE,
  INTAKE_DONE,
  INTAKE_ERROR,
  INTAKE_NO_SD,
  INTAKE_NO_ISTORE,
  INTAKE_NO_FILES
} intakeState;

static int filesCopied = 0;
static int filesTotal = 0;
static int foldersFound = 0;

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
      tft.drawString("No Folders", 120, 100);
      tft.setTextFont(2);
      tft.drawString("No folders on SD card", 120, 140);
      break;

    case INTAKE_ERROR: {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextFont(4);
      tft.drawString("Copy Error", 120, 80);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextFont(2);
      char buf[40];
      snprintf(buf, sizeof(buf), "%d/%d copied", filesCopied, filesTotal);
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
      snprintf(buf, sizeof(buf), "%d folders, %d files", foldersFound, filesCopied);
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

// Truncate a filename to 32 chars, preserving the extension
static void truncateName(const char* in, char* out, size_t outSize) {
  size_t len = strlen(in);
  if (len <= 32) {
    strncpy(out, in, outSize);
    out[outSize - 1] = '\0';
    return;
  }
  const char* dot = strrchr(in, '.');
  if (dot) {
    size_t extLen = strlen(dot);        // e.g. ".md" = 3
    size_t baseLen = 32 - extLen;       // chars left for the base
    if (baseLen < 1) baseLen = 1;
    snprintf(out, outSize, "%.*s%s", (int)baseLen, in, dot);
  } else {
    snprintf(out, outSize, "%.32s", in);
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

// Count total files across all folders for progress display
static int countFiles(char folders[][64], int folderCount) {
  int total = 0;
  for (int f = 0; f < folderCount; f++) {
    char sdFolder[80];
    snprintf(sdFolder, sizeof(sdFolder), "/%s", folders[f]);
    SDItemList items = sdGetItems(sdFolder);
    for (int i = 0; i < items.count; i++) {
      if (items.items[i].name[0] == '.') continue;
      if (items.items[i].type == SD_ITEM_DIR) continue;
      total++;
    }
  }
  return total;
}

static void runIntake() {
  tft.fillScreen(TFT_BLACK);
  filesCopied = 0;
  filesTotal = 0;
  foldersFound = 0;

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

  // Discover top-level folders on SD card root
  static char folders[MAX_FOLDERS][64];
  int folderCount = 0;

  SDItemList rootItems = sdGetItems("/");
  for (int i = 0; i < rootItems.count && folderCount < MAX_FOLDERS; i++) {
    if (rootItems.items[i].name[0] == '.') continue;
    if (rootItems.items[i].type != SD_ITEM_DIR) continue;
    strncpy(folders[folderCount], rootItems.items[i].name, 63);
    folders[folderCount][63] = '\0';
    folderCount++;
  }

  foldersFound = folderCount;

  if (folderCount == 0) {
    intakeState = INTAKE_NO_FILES;
    drawResult();
    return;
  }

  Serial.printf("Intake: found %d folders on SD\n", folderCount);
  for (int f = 0; f < folderCount; f++) {
    Serial.printf("  /%s\n", folders[f]);
  }

  // Count total files for progress
  filesTotal = countFiles(folders, folderCount);
  if (filesTotal == 0) {
    intakeState = INTAKE_NO_FILES;
    drawResult();
    return;
  }

  // Wipe LittleFS before mirroring
  Serial.println("Intake: wiping internal storage...");
  drawProgress(0, filesTotal, "Wiping storage...");
  istoreWipe();

  // Copy each folder and its files
  bool anyError = false;
  int progressIndex = 0;

  for (int f = 0; f < folderCount && !anyError; f++) {
    char sdFolder[80];
    char iFolder[80];
    snprintf(sdFolder, sizeof(sdFolder), "/%s", folders[f]);
    snprintf(iFolder, sizeof(iFolder), "/%s", folders[f]);

    // Create folder on LittleFS
    LittleFS.mkdir(iFolder);
    Serial.printf("Intake: mirroring %s\n", sdFolder);

    // List files in this SD folder
    SDItemList items = sdGetItems(sdFolder);
    for (int i = 0; i < items.count && !anyError; i++) {
      if (items.items[i].name[0] == '.') continue;
      if (items.items[i].type == SD_ITEM_DIR) continue;

      progressIndex++;
      drawProgress(progressIndex, filesTotal, items.items[i].name);

      char srcPath[128];
      char dstPath[128];
      char shortName[33];
      snprintf(srcPath, sizeof(srcPath), "%s/%s", sdFolder, items.items[i].name);
      truncateName(items.items[i].name, shortName, sizeof(shortName));
      snprintf(dstPath, sizeof(dstPath), "%s/%s", iFolder, shortName);

      // Check free space
      if (items.items[i].size > istoreFreeBytes()) {
        Serial.printf("Intake: not enough space for %s (%u > %u free)\n",
          srcPath, items.items[i].size, (unsigned)istoreFreeBytes());
        anyError = true;
        break;
      }

      if (copyFile(srcPath, dstPath)) {
        filesCopied++;
      } else {
        anyError = true;
      }
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
