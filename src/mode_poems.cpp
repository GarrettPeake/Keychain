#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "modes.h"
#include "istore.h"

static Preferences prefs;

#define POEMS_FOLDER "/poems"
#define MAX_POEMS    16
#define MAX_POEM_SIZE 2048

// Display line types
enum LineType : uint8_t { LINE_TITLE, LINE_BODY, LINE_WRAP };

#define MAX_DLINES    128
#define MAX_DLINE_LEN 34
#define TITLE_WRAP    16
#define BODY_WRAP     32

// Layout
static const int TITLE_LINE_H = 28;
static const int BODY_LINE_H  = 20;
static const int TITLE_BODY_GAP = 20;

// Poem file list
static char poemPaths[MAX_POEMS][80];
static int poemCount = 0;
static int currentPoem = 0;

// Raw content
static char bodyBuf[MAX_POEM_SIZE];
static char titleBuf[64];

// Pre-processed display lines
static char dText[MAX_DLINES][MAX_DLINE_LEN];
static LineType dType[MAX_DLINES];
static int dCount = 0;

// Scroll state
static float scrollY = 0;
static unsigned long lastFrameMs = 0;
static const float SCROLL_SPEED = 0.4f;
static int topPad = 0;
static int totalHeight = 0;

// Full-screen sprite
static TFT_eSprite spr(&tft);
static bool sprReady = false;

static void addLine(const char* text, int len, LineType type) {
  if (dCount >= MAX_DLINES) return;
  if (len >= MAX_DLINE_LEN) len = MAX_DLINE_LEN - 1;
  memcpy(dText[dCount], text, len);
  dText[dCount][len] = '\0';
  dType[dCount] = type;
  dCount++;
}

static void wordWrap(const char* text, int maxChars, LineType firstType, LineType wrapType) {
  if (!text || !*text) {
    addLine("", 0, firstType);
    return;
  }
  bool first = true;
  while (*text) {
    int len = strlen(text);
    LineType type = first ? firstType : wrapType;
    if (len <= maxChars) {
      addLine(text, len, type);
      break;
    }
    int breakAt = maxChars;
    while (breakAt > 0 && text[breakAt] != ' ') breakAt--;
    if (breakAt == 0) breakAt = maxChars;
    addLine(text, breakAt, type);
    text += breakAt;
    while (*text == ' ') text++;
    first = false;
  }
}

static void loadPoem() {
  dCount = 0;
  scrollY = 0;
  lastFrameMs = millis();
  titleBuf[0] = '\0';

  if (poemCount == 0) return;

  File f = LittleFS.open(poemPaths[currentPoem], "r");
  if (!f) return;

  size_t len = f.size();
  if (len >= MAX_POEM_SIZE) len = MAX_POEM_SIZE - 1;
  f.readBytes(bodyBuf, len);
  bodyBuf[len] = '\0';
  f.close();

  char* p = bodyBuf;

  // Extract title from "# " line
  if (p[0] == '#' && p[1] == ' ') {
    char* nl = strchr(p, '\n');
    if (nl) {
      if (nl > p && *(nl - 1) == '\r') *(nl - 1) = '\0';
      *nl = '\0';
      strncpy(titleBuf, p + 2, sizeof(titleBuf) - 1);
      titleBuf[sizeof(titleBuf) - 1] = '\0';
      p = nl + 1;
      while (*p == '\r' || *p == '\n') p++;
    } else {
      strncpy(titleBuf, p + 2, sizeof(titleBuf) - 1);
      titleBuf[sizeof(titleBuf) - 1] = '\0';
      p += strlen(p);
    }
  } else {
    strncpy(titleBuf, "Untitled", sizeof(titleBuf));
  }

  // Title lines (wrapped at 16 chars, all centered)
  wordWrap(titleBuf, TITLE_WRAP, LINE_TITLE, LINE_TITLE);

  int titleLines = dCount;
  int titleBlockH = titleLines * TITLE_LINE_H;
  topPad = (240 - titleBlockH) / 2;
  if (topPad < 20) topPad = 20;

  // Body lines (wrapped at 32 chars)
  while (*p) {
    char lineBuf[256];
    char* nl = strchr(p, '\n');
    int lineLen;
    if (nl) {
      lineLen = nl - p;
      if (lineLen > 0 && p[lineLen - 1] == '\r') lineLen--;
      if (lineLen >= (int)sizeof(lineBuf)) lineLen = sizeof(lineBuf) - 1;
      memcpy(lineBuf, p, lineLen);
      lineBuf[lineLen] = '\0';
      p = nl + 1;
    } else {
      lineLen = strlen(p);
      if (lineLen >= (int)sizeof(lineBuf)) lineLen = sizeof(lineBuf) - 1;
      memcpy(lineBuf, p, lineLen);
      lineBuf[lineLen] = '\0';
      p += strlen(p);
    }
    wordWrap(lineBuf, BODY_WRAP, LINE_BODY, LINE_WRAP);
  }

  // Total content height (with padding so last lines scroll to center)
  totalHeight = topPad;
  bool pastTitle = false;
  for (int i = 0; i < dCount; i++) {
    if (!pastTitle && dType[i] != LINE_TITLE) {
      totalHeight += TITLE_BODY_GAP;
      pastTitle = true;
    }
    totalHeight += (dType[i] == LINE_TITLE) ? TITLE_LINE_H : BODY_LINE_H;
  }
  totalHeight += 120; // bottom padding so last line can reach center

  Serial.printf("Poems: loaded \"%s\" (%d display lines)\n", titleBuf, dCount);
}

// Left edge of the circle at a given screen y (for body text curve).
// Continues smoothly past the display edge so text slides off to the right.
static int leftEdge(int screenY) {
  int midY = screenY + BODY_LINE_H / 2;
  int dy = abs(midY - 120);
  int R = 114; // display radius with small margin
  if (dy >= R) {
    // Extrapolate linearly beyond the circle so indent keeps growing
    return 120 + (dy - R) * 2;
  }
  return (int)(120.0f - sqrtf((float)(R * R - dy * dy))) + 6;
}

static void drawContent() {
  if (!sprReady) return;

  spr.fillSprite(TFT_BLACK);

  int y = topPad - (int)scrollY;
  bool pastTitle = false;

  for (int i = 0; i < dCount; i++) {
    if (!pastTitle && dType[i] != LINE_TITLE) {
      y += TITLE_BODY_GAP;
      pastTitle = true;
    }

    int lh = (dType[i] == LINE_TITLE) ? TITLE_LINE_H : BODY_LINE_H;

    if (y + lh < 0) { y += lh; continue; }
    if (y >= 240) break;

    switch (dType[i]) {
      case LINE_TITLE:
        spr.setTextColor(TFT_ORANGE, TFT_BLACK);
        spr.setTextDatum(TC_DATUM);
        spr.setTextFont(4);
        spr.drawString(dText[i], 120, y);
        break;

      case LINE_BODY: {
        int lx = leftEdge(y);
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
        spr.setTextDatum(TL_DATUM);
        spr.setTextFont(2);
        spr.drawString(dText[i], lx, y);
        break;
      }

      case LINE_WRAP: {
        int lx = leftEdge(y);
        int ay = y + (BODY_LINE_H / 2);
        spr.fillTriangle(lx, ay - 3, lx, ay + 3, lx + 4, ay, TFT_DARKGREY);
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
        spr.setTextDatum(TL_DATUM);
        spr.setTextFont(2);
        spr.drawString(dText[i], lx + 12, y);
        break;
      }
    }

    y += lh;
  }

  spr.pushSprite(0, 0);
}

static void showError(const char* line1, const char* line2) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.drawString(line1, 120, 110);
  if (line2) tft.drawString(line2, 120, 130);
}

static void poemsEnter() {
  poemCount = 0;
  currentPoem = 0;

  if (sprReady) spr.deleteSprite();
  spr.setColorDepth(8);
  sprReady = (spr.createSprite(240, 240) != nullptr);
  if (!sprReady) {
    showError("Sprite alloc", "failed");
    return;
  }

  if (!istoreIsReady()) {
    showError("Storage not", "available");
    return;
  }

  SDItemList items = istoreGetItems(POEMS_FOLDER);
  for (int i = 0; i < items.count && poemCount < MAX_POEMS; i++) {
    if (items.items[i].name[0] == '.') continue;
    if (items.items[i].type == SD_ITEM_MARKDOWN) {
      snprintf(poemPaths[poemCount], sizeof(poemPaths[poemCount]),
               "%s/%s", POEMS_FOLDER, items.items[i].name);
      poemCount++;
    }
  }

  if (poemCount == 0) {
    showError("No poems found", "Add .md to /poems");
    return;
  }

  // Sort alphabetically
  for (int i = 0; i < poemCount - 1; i++) {
    for (int j = i + 1; j < poemCount; j++) {
      if (strcmp(poemPaths[i], poemPaths[j]) > 0) {
        char tmp[80];
        memcpy(tmp, poemPaths[i], 80);
        memcpy(poemPaths[i], poemPaths[j], 80);
        memcpy(poemPaths[j], tmp, 80);
      }
    }
  }

  prefs.begin("poems", true);
  currentPoem = prefs.getInt("idx", 0);
  prefs.end();
  if (currentPoem >= poemCount) currentPoem = 0;

  Serial.printf("Poems: found %d poems, resuming at %d\n", poemCount, currentPoem + 1);

  loadPoem();

  if (!coldStart) {
    drawContent();
  }
}

static void poemsUpdate() {
  if (poemCount == 0 || dCount == 0) return;

  int maxScroll = totalHeight - 240;
  if (maxScroll <= 0) return;

  unsigned long now = millis();
  if (now - lastFrameMs < 16) return;
  lastFrameMs = now;

  scrollY += SCROLL_SPEED;

  if (scrollY > maxScroll + 80) {
    // Advance to the next poem
    currentPoem = (currentPoem + 1) % poemCount;
    prefs.begin("poems", false);
    prefs.putInt("idx", currentPoem);
    prefs.end();
    loadPoem();
  }

  drawContent();
}

static void poemsButton(int btn) {
  if (poemCount == 0) return;

  if (btn == 1) {
    currentPoem = (currentPoem + 1) % poemCount;
  } else if (btn == 2) {
    currentPoem = (currentPoem - 1 + poemCount) % poemCount;
  }

  prefs.begin("poems", false);
  prefs.putInt("idx", currentPoem);
  prefs.end();

  loadPoem();
  drawContent();
}

extern const Mode poemsMode = {"Poems", poemsEnter, poemsUpdate, poemsButton};
