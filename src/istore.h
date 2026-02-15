#pragma once

#include "sdcard.h"  // reuse SDItem, SDItemType, SDItemList

// Initialize LittleFS internal storage (formats on first mount)
bool istoreInit();

// Check if internal storage is ready
bool istoreIsReady();

// List items in a folder on internal storage (e.g. "/birthday")
SDItemList istoreGetItems(const char* folder);

// Get info about a single file on internal storage
SDItem istoreGetItem(const char* path);

// Check if a file exists on internal storage
bool istoreExists(const char* path);

// Wipe all files and folders from internal storage
void istoreWipe();

// Storage capacity
size_t istoreTotalBytes();
size_t istoreUsedBytes();
size_t istoreFreeBytes();
