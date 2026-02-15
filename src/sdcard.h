#pragma once

#include <SD.h>

// Item types for directory entries
enum SDItemType {
  SD_ITEM_NONE = 0,
  SD_ITEM_JPEG,
  SD_ITEM_MARKDOWN,
  SD_ITEM_DIR,
  SD_ITEM_OTHER
};

// Fixed-size item descriptor
struct SDItem {
  char name[64];
  SDItemType type;
  uint32_t size;
};

#define MAX_SD_ITEMS 32

struct SDItemList {
  SDItem items[MAX_SD_ITEMS];
  int count;
};

// Initialize SD card on shared HSPI bus (call after tft.init())
bool sdInit();

// Check if SD card is ready
bool sdIsReady();

// List items in a folder (e.g. "/birthday")
SDItemList sdGetItems(const char* folder);

// Get info about a single file by full path
SDItem sdGetItem(const char* path);

// Classify a filename by extension (.jpg/.jpeg -> JPEG, .md -> MARKDOWN, else OTHER)
SDItemType classifyFile(const char* name);
