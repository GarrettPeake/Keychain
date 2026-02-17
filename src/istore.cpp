#include <Arduino.h>
#include <LittleFS.h>
#include "istore.h"

static bool ready = false;

bool istoreInit() {
  Serial.println("istore: initializing LittleFS...");

  // begin(true) = format on first mount (when partition is unformatted)
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println("istore: LittleFS mount failed");
    return false;
  }

  ready = true;
  Serial.printf("istore: ready, total=%uKB, used=%uKB, free=%uKB\n",
    (unsigned)(LittleFS.totalBytes() / 1024),
    (unsigned)(LittleFS.usedBytes() / 1024),
    (unsigned)((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024));
  return true;
}

bool istoreIsReady() {
  return ready;
}

SDItemList istoreGetItems(const char* folder) {
  SDItemList result;
  result.count = 0;

  if (!ready) return result;

  File root = LittleFS.open(folder);
  if (!root || !root.isDirectory()) {
    Serial.printf("istore: cannot open folder %s\n", folder);
    if (root) root.close();
    return result;
  }

  File file = root.openNextFile();
  while (file && result.count < MAX_SD_ITEMS) {
    SDItem& item = result.items[result.count];

    // LittleFS File.name() returns full path — extract basename
    const char* fullPath = file.name();
    const char* slash = strrchr(fullPath, '/');
    const char* baseName = slash ? (slash + 1) : fullPath;

    strncpy(item.name, baseName, sizeof(item.name) - 1);
    item.name[sizeof(item.name) - 1] = '\0';
    item.type = file.isDirectory() ? SD_ITEM_DIR : classifyFile(baseName);
    item.size = file.size();
    result.count++;
    file.close();
    file = root.openNextFile();
  }
  if (file) file.close();
  root.close();
  return result;
}

SDItem istoreGetItem(const char* path) {
  SDItem item;
  item.type = SD_ITEM_NONE;
  item.size = 0;
  item.name[0] = '\0';

  if (!ready) return item;

  File file = LittleFS.open(path);
  if (!file) return item;

  const char* slash = strrchr(path, '/');
  const char* name = slash ? (slash + 1) : path;
  strncpy(item.name, name, sizeof(item.name) - 1);
  item.name[sizeof(item.name) - 1] = '\0';
  item.type = file.isDirectory() ? SD_ITEM_DIR : classifyFile(name);
  item.size = file.size();
  file.close();
  return item;
}

bool istoreExists(const char* path) {
  if (!ready) return false;
  return LittleFS.exists(path);
}

static void removeRecursive(const char* path) {
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) {
    dir.close();
    LittleFS.remove(path);
    return;
  }
  File child = dir.openNextFile();
  while (child) {
    // child.name() may or may not include a leading slash depending on
    // the ESP32 Arduino core version — build the full path from parent.
    const char* name = child.name();
    const char* base = strrchr(name, '/');
    base = base ? (base + 1) : name;
    char childPath[128];
    if (strcmp(path, "/") == 0) {
      snprintf(childPath, sizeof(childPath), "/%s", base);
    } else {
      snprintf(childPath, sizeof(childPath), "%s/%s", path, base);
    }
    bool isDir = child.isDirectory();
    child.close();
    if (isDir) {
      removeRecursive(childPath);
    } else {
      LittleFS.remove(childPath);
    }
    child = dir.openNextFile();
  }
  dir.close();
  if (strcmp(path, "/") != 0) {
    LittleFS.rmdir(path);
  }
}

void istoreWipe() {
  if (!ready) return;
  Serial.println("istore: wiping all files...");
  removeRecursive("/");
  Serial.printf("istore: wipe complete, free=%uKB\n",
    (unsigned)(istoreFreeBytes() / 1024));
}

size_t istoreTotalBytes() {
  if (!ready) return 0;
  return LittleFS.totalBytes();
}

size_t istoreUsedBytes() {
  if (!ready) return 0;
  return LittleFS.usedBytes();
}

size_t istoreFreeBytes() {
  if (!ready) return 0;
  return LittleFS.totalBytes() - LittleFS.usedBytes();
}
