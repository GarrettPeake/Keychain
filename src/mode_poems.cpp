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
static const int LINE_SPR_H   = TITLE_LINE_H; // tall enough for any line

// Color palette — RGB565 values chosen to map cleanly to RGB332.
// Each channel is a multiple of the quantization step (R,B: >>2, G: >>3)
// so no rounding error in the final 8-bit output at integer positions.
#define COL_BG     0x0000   // Black background
#define COL_TITLE  0xE500   // Warm gold   (R=28,G=40,B=0 → 332: 7,5,0)
#define COL_BODY   0xFFFF   // Pure white  (R=31,G=63,B=31 → 332: 7,7,3)
#define COL_WRAP   0xA514   // Light gray  (R=20,G=40,B=20 → 332: 5,5,2)

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
static int dWidth[MAX_DLINES];   // pre-computed pixel width per line
static int dCount = 0;

// Scroll state
static float scrollY = 0;
static unsigned long lastFrameMs = 0;
static const float SCROLL_SPEED = 0.9f;
static int topPad = 0;
static int totalHeight = 0;

// Full-screen sprite
static TFT_eSprite spr(&tft);
static bool sprReady = false;

// Temp line sprite for sub-pixel horizontal rendering
static TFT_eSprite lineSpr(&tft);
static bool lineSprReady = false;

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

  // Pre-compute pixel widths for sub-pixel blit
  spr.setTextFont(2);
  for (int i = 0; i < dCount; i++) {
    if (dType[i] == LINE_TITLE)
      dWidth[i] = 0;
    else if (dType[i] == LINE_WRAP)
      dWidth[i] = spr.textWidth(dText[i], 2) + 12;
    else
      dWidth[i] = spr.textWidth(dText[i], 2);
  }

  Serial.printf("Poems: loaded \"%s\" (%d display lines)\n", titleBuf, dCount);
}

// Parabolic left indent from float screen Y.  Returns float for sub-pixel
// horizontal positioning.  k=0.0065 matches the old circle for dy<100.
static float leftEdgeF(float screenY) {
  float midY = screenY + BODY_LINE_H * 0.5f;
  float dy = fabsf(midY - 120.0f);
  return 6.0f + 0.0065f * dy * dy;
}

// Gamma-correction LUTs for perceptually-correct sub-pixel blending.
// Interpolation in gamma-encoded space underestimates brightness (two 50%
// pixels look dimmer than one 100% pixel).  Converting to linear light,
// blending, then back to gamma fixes this.
static uint16_t g2l5[32];   // 5-bit gamma (src R,B) → 16-bit linear
static uint16_t g2l6[64];   // 6-bit gamma (src G)   → 16-bit linear
static uint16_t g2l3[8];    // 3-bit gamma (dst R,G) → 16-bit linear
static uint16_t g2l2[4];    // 2-bit gamma (dst B)   → 16-bit linear
static uint8_t  l2g3[256];  // 8-bit linear → 3-bit gamma (out R,G)
static uint8_t  l2g2[256];  // 8-bit linear → 2-bit gamma (out B)
static bool gammaReady = false;

static void initGammaLUTs() {
  for (int i = 0; i < 32; i++)
    g2l5[i] = (uint16_t)(powf((float)i / 31.0f, 2.2f) * 65535.0f + 0.5f);
  for (int i = 0; i < 64; i++)
    g2l6[i] = (uint16_t)(powf((float)i / 63.0f, 2.2f) * 65535.0f + 0.5f);
  for (int i = 0; i < 8; i++)
    g2l3[i] = (uint16_t)(powf((float)i / 7.0f, 2.2f) * 65535.0f + 0.5f);
  for (int i = 0; i < 4; i++)
    g2l2[i] = (uint16_t)(powf((float)i / 3.0f, 2.2f) * 65535.0f + 0.5f);
  for (int i = 0; i < 256; i++) {
    float v = (float)i / 255.0f;
    float g = powf(v, 1.0f / 2.2f);
    l2g3[i] = (uint8_t)(g * 7.0f + 0.5f);
    l2g2[i] = (uint8_t)(g * 3.0f + 0.5f);
  }
  gammaReady = true;
}

// Byte-swap helper for TFT_eSPI 16-bit sprite buffer (stored swapped for SPI).
static inline uint16_t bswap16(uint16_t v) { return (v >> 8) | (v << 8); }

// Blit 16-bit line sprite into 8-bit main sprite with sub-pixel X interpolation.
// Source is RGB565 (16-bit, byte-swapped), destination is RGB332 (8-bit).
// Interpolation happens in 5/6/5-bit precision, then converts to 3/3/2-bit
// output — gives much better color balance than interpolating in 8-bit directly.
static void subPixelBlit(int srcW, int srcH, float dstXf, int dstY) {
  uint16_t* srcBuf = (uint16_t*)lineSpr.getPointer();
  uint8_t*  dstBuf = (uint8_t*)spr.getPointer();
  if (!srcBuf || !dstBuf) return;

  int dstXi = (int)floorf(dstXf);
  int wRight = (int)((dstXf - (float)dstXi) * 256.0f + 0.5f);
  int wLeft  = 256 - wRight;

  // Fast path: no fractional offset, just convert and copy
  if (wRight < 2) {
    for (int row = 0; row < srcH; row++) {
      int dy = dstY + row;
      if (dy < 0 || dy >= 240) continue;
      uint16_t* sr = srcBuf + row * 240;
      uint8_t*  dr = dstBuf + dy * 240;
      for (int sx = 0; sx < srcW; sx++) {
        uint16_t cs = sr[sx];
        if (cs == 0) continue;
        int dx = dstXi + sx;
        if (dx >= 0 && dx < 240) {
          uint16_t c = bswap16(cs);
          dr[dx] = (((c >> 11) & 0x1F) >> 2 << 5)
                 | (((c >> 5)  & 0x3F) >> 3 << 2)
                 | (( c        & 0x1F) >> 3);
        }
      }
    }
    return;
  }

  // Gamma-correct interpolation: blend in linear light, convert back to gamma.
  for (int row = 0; row < srcH; row++) {
    int dy = dstY + row;
    if (dy < 0 || dy >= 240) continue;
    uint16_t* sr = srcBuf + row * 240;
    uint8_t*  dr = dstBuf + dy * 240;

    for (int sx = 0; sx < srcW; sx++) {
      uint16_t cs = sr[sx];
      if (cs == 0) continue;

      uint16_t c = bswap16(cs);
      int dx = dstXi + sx;

      // Source channels → linear via LUT
      int rl = g2l5[(c >> 11) & 0x1F];
      int gl = g2l6[(c >> 5)  & 0x3F];
      int bl = g2l5[ c        & 0x1F];

      // Left pixel: weight in linear space, add to existing dest (also linearized)
      if (dx >= 0 && dx < 240) {
        int lr = (rl * wLeft) >> 8;
        int lg = (gl * wLeft) >> 8;
        int lb = (bl * wLeft) >> 8;
        uint8_t d = dr[dx];
        lr += g2l3[(d >> 5) & 7];
        lg += g2l3[(d >> 2) & 7];
        lb += g2l2[d & 3];
        if (lr > 65535) lr = 65535;
        if (lg > 65535) lg = 65535;
        if (lb > 65535) lb = 65535;
        dr[dx] = (l2g3[lr >> 8] << 5) | (l2g3[lg >> 8] << 2) | l2g2[lb >> 8];
      }

      // Right pixel
      int dx1 = dx + 1;
      if (dx1 >= 0 && dx1 < 240) {
        int rr = (rl * wRight) >> 8;
        int rg = (gl * wRight) >> 8;
        int rb = (bl * wRight) >> 8;
        uint8_t d = dr[dx1];
        rr += g2l3[(d >> 5) & 7];
        rg += g2l3[(d >> 2) & 7];
        rb += g2l2[d & 3];
        if (rr > 65535) rr = 65535;
        if (rg > 65535) rg = 65535;
        if (rb > 65535) rb = 65535;
        dr[dx1] = (l2g3[rr >> 8] << 5) | (l2g3[rg >> 8] << 2) | l2g2[rb >> 8];
      }
    }
  }
}

static void drawContent() {
  if (!sprReady) return;

  spr.fillSprite(COL_BG);

  float yf = (float)topPad - scrollY;
  bool pastTitle = false;

  for (int i = 0; i < dCount; i++) {
    if (!pastTitle && dType[i] != LINE_TITLE) {
      yf += TITLE_BODY_GAP;
      pastTitle = true;
    }

    int lh = (dType[i] == LINE_TITLE) ? TITLE_LINE_H : BODY_LINE_H;
    int yi = (int)floorf(yf);

    if (yi + lh < 0) { yf += lh; continue; }
    if (yi >= 240) break;

    switch (dType[i]) {
      case LINE_TITLE:
        if (lineSprReady) {
          lineSpr.fillSprite(COL_BG);
          lineSpr.setTextColor(COL_TITLE);
          lineSpr.setTextDatum(TL_DATUM);
          lineSpr.setTextFont(4);
          int tw = lineSpr.textWidth(dText[i], 4);
          lineSpr.drawString(dText[i], 0, 0);
          subPixelBlit(tw, TITLE_LINE_H, 120.0f - tw * 0.5f, yi);
        } else {
          spr.setTextColor(COL_TITLE);
          spr.setTextDatum(TC_DATUM);
          spr.setTextFont(4);
          spr.drawString(dText[i], 120, yi);
        }
        break;

      case LINE_BODY: {
        float lxf = leftEdgeF(yf);
        if (lineSprReady) {
          lineSpr.fillSprite(COL_BG);
          lineSpr.setTextColor(COL_BODY);
          lineSpr.setTextDatum(TL_DATUM);
          lineSpr.setTextFont(2);
          lineSpr.drawString(dText[i], 0, 0);
          subPixelBlit(dWidth[i], BODY_LINE_H, lxf, yi);
        } else {
          spr.setTextColor(COL_BODY);
          spr.setTextDatum(TL_DATUM);
          spr.setTextFont(2);
          spr.drawString(dText[i], (int)(lxf + 0.5f), yi);
        }
        break;
      }

      case LINE_WRAP: {
        float lxf = leftEdgeF(yf);
        if (lineSprReady) {
          lineSpr.fillSprite(COL_BG);
          int ay = BODY_LINE_H / 2;
          lineSpr.fillTriangle(0, ay - 3, 0, ay + 3, 4, ay, COL_WRAP);
          lineSpr.setTextColor(COL_BODY);
          lineSpr.setTextDatum(TL_DATUM);
          lineSpr.setTextFont(2);
          lineSpr.drawString(dText[i], 12, 0);
          subPixelBlit(dWidth[i], BODY_LINE_H, lxf, yi);
        } else {
          int lx = (int)(lxf + 0.5f);
          int ay = yi + (BODY_LINE_H / 2);
          spr.fillTriangle(lx, ay - 3, lx, ay + 3, lx + 4, ay, COL_WRAP);
          spr.setTextColor(COL_BODY);
          spr.setTextDatum(TL_DATUM);
          spr.setTextFont(2);
          spr.drawString(dText[i], lx + 12, yi);
        }
        break;
      }
    }

    yf += lh;
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

  if (!gammaReady) initGammaLUTs();

  if (lineSprReady) { lineSpr.deleteSprite(); lineSprReady = false; }
  if (sprReady) spr.deleteSprite();
  spr.setColorDepth(8);
  sprReady = (spr.createSprite(240, 240) != nullptr);
  if (!sprReady) {
    showError("Sprite alloc", "failed");
    return;
  }
  lineSpr.setColorDepth(16);
  lineSprReady = (lineSpr.createSprite(240, LINE_SPR_H) != nullptr);

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
