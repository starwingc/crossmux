#include "PixelShelf.h"

#include <GfxRenderer.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "InxItemLayout.h"
#include "PixelShelfArt.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace PixelShelf {
namespace {

using namespace PixelShelfArt;

constexpr int kFolderSize = 16;
inline constexpr const char* kFolderArt[kFolderSize] = {
    "................", "................", ".#####..........", "#.....#.........",
    "#......#######..", "#.............#.", "################", "#..............#",
    "#..............#", "#..............#", "#..............#", "#..............#",
    "#..............#", "#..............#", "################", "................"};

// '#' ink, '.' paper, 'c' paper unless a cover shows through, ' ' untouched.
void drawCell(const GfxRenderer& r, const int x, const int y, const char cell, const bool overCover,
              const int scale = 1) {
  if (cell == ' ' || (cell == 'c' && overCover)) return;
  if (scale == 1)
    r.drawPixel(x, y, cell == '#');
  else
    r.fillRect(x, y, scale, scale, cell == '#');
}

void drawFrame(const GfxRenderer& r, const Rect& card, const bool overCover) {
  const int right = card.x + card.width - 1;
  const int bottom = card.y + card.height - 1;
  // Edges: each depth into the border is one solid line along all four sides.
  for (int d = 0; d < kCorner; ++d) {
    const char cell = kEdgeArt[d];
    if (cell == ' ' || (cell == 'c' && overCover)) continue;
    const bool ink = cell == '#';
    const int spanW = card.width - 2 * kCorner;
    const int spanH = card.height - 2 * kCorner;
    r.fillRect(card.x + kCorner, card.y + d, spanW, 1, ink);
    r.fillRect(card.x + kCorner, bottom - d, spanW, 1, ink);
    r.fillRect(card.x + d, card.y + kCorner, 1, spanH, ink);
    r.fillRect(right - d, card.y + kCorner, 1, spanH, ink);
  }
  // Corners: the top-left art mirrored into the other three.
  for (int dy = 0; dy < kCorner; ++dy) {
    for (int dx = 0; dx < kCorner; ++dx) {
      const char cell = kCornerArt[dy][dx];
      drawCell(r, card.x + dx, card.y + dy, cell, overCover);
      drawCell(r, right - dx, card.y + dy, cell, overCover);
      drawCell(r, card.x + dx, bottom - dy, cell, overCover);
      drawCell(r, right - dx, bottom - dy, cell, overCover);
    }
  }
}

void drawPill(const GfxRenderer& r, const Rect& pill, const char* label) {
  r.fillRoundedRect(pill.x, pill.y, pill.width, pill.height, pill.height / 2, Color::White);
  r.drawRoundedRect(pill.x, pill.y, pill.width, pill.height, 2, pill.height / 2, true);
  if (!label || !*label) return;
  const std::string shown = r.truncatedText(SMALL_FONT_ID, label, pill.width - 12, EpdFontFamily::BOLD);
  const int textW = r.getTextWidth(SMALL_FONT_ID, shown.c_str(), EpdFontFamily::BOLD);
  const int textY = pill.y + (pill.height - r.getLineHeight(SMALL_FONT_ID)) / 2;
  r.drawText(SMALL_FONT_ID, pill.x + (pill.width - textW) / 2, textY, shown.c_str(), true, EpdFontFamily::BOLD);
}

// Card body shared by the no-cover and folder cards: frame, title band and divider.
// Returns the y just under the divider.
int drawTitledCard(const GfxRenderer& r, const Rect& card, const char* title) {
  r.fillRect(card.x, card.y, card.width, card.height, false);
  drawFrame(r, card, false);
  const Rect inner = coverRect(card);
  const int titleH = inner.height * 30 / 100;
  UITheme::drawCenteredWrappedText(r, Rect{inner.x + 4, inner.y + 2, inner.width - 8, titleH}, UI_10_FONT_ID,
                                   title ? title : "", 3);
  const int waveY = inner.y + titleH + 4;
  for (int x = inner.x + 2; x < inner.x + inner.width - 2; ++x) {
    for (int row = 0; row < kWaveH; ++row)
      drawCell(r, x, waveY + row, kWaveArt[row][(x - inner.x - 2) % kWaveW], false);
  }
  return waveY + kWaveH;
}


uint32_t titleHash(const char* text) {
  uint32_t hash = 2166136261u;
  for (const char* p = text ? text : ""; *p; ++p) hash = (hash ^ static_cast<uint8_t>(*p)) * 16777619u;
  return hash;
}

constexpr int kSpineFont = UI_10_FONT_ID;

int spineGlyphWidth(const GfxRenderer& r) { return r.getTextWidth(kSpineFont, "\xe4\xb9\xa6"); }  // 书

// The title stacked one character per line, top to bottom, centred on cx. When it does not fit
// between top and bottom the last slot becomes three pixel dots.
void drawVerticalTitleIn(const GfxRenderer& r, int font, int cx, int top, int bottom, const char* text, bool black,
                         bool measureOnly, bool* fits);

// Uses the spine font, or the small font when the whole title only fits that way.
void drawVerticalTitle(const GfxRenderer& r, const int cx, const int top, const int bottom, const char* text,
                       const bool black) {
  bool fits = true;
  drawVerticalTitleIn(r, kSpineFont, cx, top, bottom, text, black, true, &fits);
  drawVerticalTitleIn(r, fits ? kSpineFont : SMALL_FONT_ID, cx, top, bottom, text, black, false, &fits);
}

void drawVerticalTitleIn(const GfxRenderer& r, const int font, const int cx, const int top, const int bottom,
                         const char* text, const bool black, const bool measureOnly, bool* fits) {
  const int kSpineFont = font;
  const int lineH = r.getLineHeight(kSpineFont);
  const int cjkStep = std::max(8, lineH * 85 / 100);
  const int asciiStep = std::max(8, lineH * 62 / 100);
  struct Glyph {
    char bytes[5];
    int step;
  };
  Glyph glyphs[48];
  int count = 0;
  const auto* p = reinterpret_cast<const unsigned char*>(text ? text : "");
  while (*p && count < 48) {
    const unsigned char* start = p;
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (cp == ' ' || cp == '_') {
      glyphs[count++] = Glyph{{0}, asciiStep / 2};
      continue;
    }
    // Brackets turn with the text: drawn as pixel arcs opening downwards / upwards.
    if (cp == '(' || cp == 0xFF08 || cp == '[' || cp == 0x3010) {
      glyphs[count++] = Glyph{{'\x01'}, asciiStep};
      continue;
    }
    if (cp == ')' || cp == 0xFF09 || cp == ']' || cp == 0x3011) {
      glyphs[count++] = Glyph{{'\x02'}, asciiStep};
      continue;
    }
    Glyph g{{0}, cp < 0x80 ? asciiStep : cjkStep};
    const size_t len = std::min<size_t>(4, p - start);
    std::copy(start, start + len, g.bytes);
    glyphs[count++] = g;
  }
  int total = 0;
  for (int i = 0; i < count; ++i) total += glyphs[i].step;
  const int avail = bottom - top;
  int shown = count;
  bool clipped = false;
  *fits = total <= avail;
  if (measureOnly) return;
  if (total > avail) {
    clipped = true;
    int used = asciiStep;  // room for the dots
    shown = 0;
    while (shown < count && used + glyphs[shown].step <= avail) used += glyphs[shown++].step;
    total = used;
  }
  int y = top + (avail - total) / 2 - (lineH - cjkStep) / 2;
  for (int i = 0; i < shown; ++i) {
    if (glyphs[i].bytes[0] == '\x01' || glyphs[i].bytes[0] == '\x02') {
      const bool open = glyphs[i].bytes[0] == '\x01';
      const int arcY = y + (lineH - cjkStep) / 2 + (open ? asciiStep / 2 : 2);
      r.fillRect(cx - 6, open ? arcY : arcY + 4, 13, 2, black);
      r.fillRect(cx - 8, arcY + (open ? 2 : 0), 2, 4, black);
      r.fillRect(cx + 7, arcY + (open ? 2 : 0), 2, 4, black);
    } else if (glyphs[i].bytes[0]) {
      const int w = r.getTextWidth(kSpineFont, glyphs[i].bytes);
      const int asciiLift = glyphs[i].step == asciiStep ? (cjkStep - asciiStep) / 2 : 0;
      r.drawText(kSpineFont, cx - w / 2, y - asciiLift, glyphs[i].bytes, black);
    }
    y += glyphs[i].step;
  }
  if (clipped) {
    const int dotsY = y + (lineH - cjkStep) / 2 + 2;
    for (int d = 0; d < 3; ++d) r.fillRect(cx - 1, dotsY + d * 5, 3, 3, black);
  }
}

void drawPixelArt(const GfxRenderer& r, const char* const* art, const int w, const int h, const int x, const int y,
                  const int scale, const bool black) {
  for (int row = 0; row < h; ++row)
    for (int col = 0; col < w; ++col)
      if (art[row][col] == '#') r.fillRect(x + col * scale, y + row * scale, scale, scale, black);
}

void drawFileBox(const GfxRenderer& r, const Rect& s, const char* name) {
  r.fillRect(s.x, s.y, s.width, s.height, false);
  r.drawRect(s.x, s.y, s.width, s.height, 3, true);
  r.drawLine(s.x + 6, s.y + 3, s.x + 6, s.y + s.height - 4, true);  // the box's folded side
  const int scale = s.width >= 46 ? 2 : 1;
  const int iconW = kFolderSize * scale;
  const int iconY = s.y + 10;
  drawPixelArt(r, kFolderArt, kFolderSize, kFolderSize, s.x + 3 + (s.width - 3 - iconW) / 2, iconY, scale, true);
  // Finger hole near the bottom.
  const int holeW = std::min(22, s.width - 18);
  const int holeY = s.y + s.height - 24;
  r.fillRoundedRect(s.x + 3 + (s.width - 3 - holeW) / 2, holeY, holeW, 12, 6, Color::Black);
  r.fillRoundedRect(s.x + 3 + (s.width - 3 - holeW) / 2 + 3, holeY + 3, holeW - 6, 6, 3, Color::White);
  // Paper label holding the name.
  const Rect label{s.x + 10, iconY + iconW + 8, s.width - 16, holeY - 8 - (iconY + iconW + 8)};
  if (label.height < 24) return;
  r.drawRect(label.x, label.y, label.width, label.height, 2, true);
  drawVerticalTitle(r, label.x + label.width / 2, label.y + 6, label.y + label.height - 6, name, true);
}

// A trailing "(author)" / "（译者）" / "【tag】" is left off the spine; the open book shows it.
std::string spineTitle(const char* title) {
  std::string text(title ? title : "");
  size_t cut = std::string::npos;
  for (const char* mark : {"(", "\xef\xbc\x88", "[", "\xe3\x80\x90"}) cut = std::min(cut, text.find(mark));
  if (cut != std::string::npos && cut > 0) text.erase(cut);
  while (!text.empty() && text.back() == ' ') text.pop_back();
  return text;
}

void drawBookSpine(const GfxRenderer& r, const Rect& s, const char* fullTitle) {
  const uint32_t hash = titleHash(fullTitle);
  const std::string shortTitle = spineTitle(fullTitle);
  const char* title = shortTitle.c_str();
  const int x = s.x, y = s.y, w = s.width, h = s.height;
  const int cx = x + w / 2;
  switch (hash % 4) {
    case 0:  // dark cloth, double gilt bands
      r.fillRect(x, y, w, h, true);
      for (const int by : {y + 8, y + 13, y + h - 15, y + h - 10}) r.fillRect(x + 3, by, w - 6, 2, false);
      drawVerticalTitle(r, cx, y + 22, y + h - 22, title, false);
      break;
    case 1:  // paper spine with black caps
      r.fillRect(x, y, w, h, false);
      r.drawRect(x, y, w, h, 2, true);
      r.fillRect(x, y, w, 16, true);
      r.fillRect(x, y + h - 12, w, 12, true);
      r.fillRect(cx - 2, y + 6, 4, 4, false);
      drawVerticalTitle(r, cx, y + 22, y + h - 18, title, true);
      break;
    case 2:  // speckled board with a paper label
      r.fillRectDither(x, y, w, h, Color::DarkGray);
      r.drawRect(x, y, w, h, 2, true);
      r.fillRect(x + 5, y + 16, w - 10, h - 40, false);
      r.drawRect(x + 5, y + 16, w - 10, h - 40, 1, true);
      drawVerticalTitle(r, cx, y + 22, y + h - 30, title, true);
      r.fillRect(x + 2, y + h - 14, w - 4, 2, true);
      break;
    default: {  // plain paper with ruled head and a diamond at the tail
      r.fillRect(x, y, w, h, false);
      r.drawRect(x, y, w, h, 2, true);
      for (int i = 0; i < 3; ++i) r.fillRect(x + 2, y + 6 + i * 4, w - 4, 1, true);
      drawVerticalTitle(r, cx, y + 22, y + h - 24, title, true);
      const int dy = y + h - 14;
      for (int i = 0; i < 5; ++i) r.fillRect(cx - i, dy - 4 + i, 2 * i + 1, 1, true);
      for (int i = 0; i < 4; ++i) r.fillRect(cx - i, dy + 4 - i, 2 * i + 1, 1, true);
      break;
    }
  }
  // Light catches the left edge of every spine; a dark seam separates neighbours.
  if (hash % 4 == 0) r.fillRect(x + 2, y + 2, 1, h - 4, false);
}

// --- opening animation -------------------------------------------------------------------------

Rect lerpRect(const Rect& a, const Rect& b, const int num, const int den) {
  return Rect{a.x + (b.x - a.x) * num / den, a.y + (b.y - a.y) * num / den, a.width + (b.width - a.width) * num / den,
              a.height + (b.height - a.height) * num / den};
}

void drawRuledPage(const GfxRenderer& r, const Rect& page, const bool rightHand) {
  r.fillRect(page.x, page.y, page.width, page.height, false);
  r.drawRect(page.x, page.y, page.width, page.height, 2, true);
  // Leaves underneath, peeking out at the outer edge and the foot.
  for (int i = 1; i <= 2; ++i) {
    const int edgeX = rightHand ? page.x + page.width - 1 + i * 3 : page.x - i * 3;
    r.drawLine(edgeX, page.y + i * 3, edgeX, page.y + page.height - 1 + i * 3, true);
    r.drawLine(page.x + (rightHand ? 0 : -i * 3), page.y + page.height - 1 + i * 3,
               page.x + page.width - 1 + (rightHand ? i * 3 : 0), page.y + page.height - 1 + i * 3, true);
  }
}

void drawTextLines(const GfxRenderer& r, const Rect& area, const uint32_t seed) {
  int n = 0;
  for (int y = area.y; y + 2 <= area.y + area.height; y += 14, ++n) {
    const bool paragraphEnd = ((seed >> (n % 29)) & 7) == 0;
    const int indent = n == 0 || (((seed >> ((n + 28) % 29)) & 7) == 0) ? 16 : 0;
    const int w = paragraphEnd ? area.width * (40 + (seed >> (n % 23)) % 40) / 100 : area.width - indent;
    r.fillRectDither(area.x + indent, y, std::max(8, w - indent), 3, Color::DarkGray);
  }
}

void drawClosedBook(const GfxRenderer& r, const Rect& b, const char* title, const bool folder, CoverPainter cover,
                    void* user) {
  // Page block showing at the fore-edge and foot.
  r.fillRect(b.x + 4, b.y + 4, b.width, b.height, false);
  r.drawRect(b.x + 4, b.y + 4, b.width, b.height, 2, true);
  for (int i = 8; i < b.height; i += 4) r.drawLine(b.x + b.width, b.y + i, b.x + b.width + 2, b.y + i, true);
  r.fillRect(b.x + 8, b.y + b.height + 4, b.width, 4, true);  // shadow
  if (!folder && cover && cover(r, b, user)) {
    r.drawRect(b.x, b.y, b.width, b.height, 2, true);
  } else if (folder) {
    drawFolderCard(r, b, title);
  } else {
    drawNoCoverCard(r, b, title, "");
  }
  r.fillRect(b.x, b.y, std::max(3, b.width / 24), b.height, true);  // hinge
}

// A board hinged at x=hingeX swung to `freeX` (left or right of the hinge); its free edge
// grows by `flare` at top and bottom as it comes towards you.
void drawSwingingBoard(const GfxRenderer& r, const Rect& b, const int hingeX, const int freeX, const int flare,
                       const bool outside) {
  const int xs[4] = {hingeX, freeX, freeX, hingeX};
  const int ys[4] = {b.y, b.y - flare, b.y + b.height + flare, b.y + b.height};
  r.fillPolygon(xs, ys, 4, true);
  const int inset = 5;
  const int dir = freeX > hingeX ? 1 : -1;
  const int xi[4] = {hingeX + dir * inset, freeX - dir * inset, freeX - dir * inset, hingeX + dir * inset};
  const int yi[4] = {b.y + inset, b.y - flare + inset, b.y + b.height + flare - inset, b.y + b.height - inset};
  r.fillPolygon(xi, yi, 4, false);
  if (outside) {
    // Outer face: dark board with a light frame.
    const int xo[4] = {xi[0] + dir * 3, xi[1] - dir * 3, xi[2] - dir * 3, xi[3] + dir * 3};
    const int yo[4] = {yi[0] + 3, yi[1] + 3, yi[2] - 3, yi[3] - 3};
    r.fillPolygon(xo, yo, 4, true);
  } else {
    // Endpaper: a sparse dot pattern inside the frame.
    const int left = std::min(xi[0], xi[1]) + 4, right = std::max(xi[0], xi[1]) - 4;
    for (int y = b.y + inset + 6; y < b.y + b.height - inset - 4; y += 10)
      for (int x = left + ((y / 10) % 2) * 5; x < right; x += 10) r.fillRect(x, y, 2, 2, true);
  }
}

void drawFlyleaf(const GfxRenderer& r, const Rect& page, const bool folder) {
  drawRuledPage(r, page, false);
  // Ex libris stamp: a framed folder or open book in the middle of the page.
  const int box = 72;
  const int bx = page.x + (page.width - box) / 2;
  const int by = page.y + page.height / 2 - box / 2 - 20;
  r.drawRect(bx, by, box, box, 2, true);
  r.drawRect(bx + 5, by + 5, box - 10, box - 10, 1, true);
  if (folder)
    drawPixelArt(r, kFolderArt, kFolderSize, kFolderSize, bx + (box - 32) / 2, by + (box - 32) / 2, 2, true);
  else
    drawPixelArt(r, kBookArt, kBookW, kBookH, bx + (box - 2 * kBookW) / 2 + 1, by + (box - 2 * kBookH) / 2, 2,
                 true);
  const char* exLibris = "EX LIBRIS";
  const int w = r.getTextWidth(SMALL_FONT_ID, exLibris);
  r.drawText(SMALL_FONT_ID, page.x + (page.width - w) / 2, by + box + 10, exLibris, true);
}

void drawTitlePage(const GfxRenderer& r, const Rect& page, const char* title, const bool folder) {
  drawRuledPage(r, page, true);
  const Rect inner{page.x + 18, page.y + 24, page.width - 36, page.height - 48};
  const int titleH = r.getLineHeight(UI_12_FONT_ID) * 3;
  UITheme::drawCenteredWrappedText(r, Rect{inner.x, inner.y, inner.width, titleH}, UI_12_FONT_ID,
                                   title ? title : "", 3);
  const int waveY = inner.y + titleH + 6;
  for (int x = inner.x; x < inner.x + inner.width; ++x)
    for (int row = 0; row < kWaveH; ++row)
      if (kWaveArt[row][(x - inner.x) % kWaveW] == '#') r.drawPixel(x, waveY + row, true);
  const Rect body{inner.x, waveY + kWaveH + 14, inner.width, inner.y + inner.height - (waveY + kWaveH + 14)};
  if (folder) {
    // A folder opens on its table of contents: dotted leader lines.
    for (int y = body.y; y + 4 < body.y + body.height; y += 22) {
      r.fillRectDither(body.x, y, body.width * 45 / 100, 3, Color::DarkGray);
      for (int x = body.x + body.width * 50 / 100; x < body.x + body.width - 14; x += 6) r.fillRect(x, y + 1, 2, 2, true);
      r.fillRect(body.x + body.width - 10, y, 10, 3, true);
    }
  } else {
    drawTextLines(r, body, titleHash(title));
  }
}

}  // namespace

Rect coverRect(const Rect& card) {
  return Rect{card.x + kBorder, card.y + kBorder, card.width - 2 * kBorder, card.height - 2 * kBorder};
}

int coverThumbHeight() { return InxCoverGeometry::thumbnailHeightForCropFill(kCardH - 2 * kBorder); }

void drawCardFrame(const GfxRenderer& renderer, const Rect& card) { drawFrame(renderer, card, true); }

void drawNoCoverCard(const GfxRenderer& renderer, const Rect& card, const char* title, const char* label) {
  const int top = drawTitledCard(renderer, card, title);
  const Rect inner = coverRect(card);
  const int pillH = renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const Rect pill{inner.x + 10, inner.y + inner.height - pillH - 8, inner.width - 20, pillH};
  // The open book with its rays, as large as fits between divider and pill.
  const int spaceH = pill.y - top - 8;
  const int scale = std::max(1, std::min((inner.width - 8) / kBookW, spaceH / kBookH));
  const int bookX = inner.x + (inner.width - kBookW * scale) / 2;
  const int bookY = top + 4 + (spaceH - kBookH * scale) / 2;
  for (int y = 0; y < kBookH; ++y) {
    for (int x = 0; x < kBookW; ++x) {
      if (kBookArt[y][x] == '#') renderer.fillRect(bookX + x * scale, bookY + y * scale, scale, scale, true);
    }
  }
  drawPill(renderer, pill, label);
}

void drawFolderCard(const GfxRenderer& renderer, const Rect& card, const char* name) {
  const int top = drawTitledCard(renderer, card, name);
  const Rect inner = coverRect(card);
  const int spaceH = inner.y + inner.height - top - 8;
  const int scale = std::max(1, std::min((inner.width - 16) / kFolderSize, spaceH / kFolderSize));
  const int artX = inner.x + (inner.width - kFolderSize * scale) / 2;
  const int artY = top + (spaceH - kFolderSize * scale) / 2;
  for (int y = 0; y < kFolderSize; ++y) {
    for (int x = 0; x < kFolderSize; ++x) {
      if (kFolderArt[y][x] == '#') renderer.fillRect(artX + x * scale, artY + y * scale, scale, scale, true);
    }
  }
}

void drawCardSelection(const GfxRenderer& renderer, const Rect& card) {
  renderer.drawRoundedRect(card.x - 7, card.y - 7, card.width + 14, card.height + 14, 3, 10, true);
}

// A wooden plank: dithered top face, framed front edge, hard shadow below.
void drawPlank(const GfxRenderer& renderer, const int x, const int width, const int y) {
  renderer.fillRectDither(x + 10, y, width - 20, 10, Color::DarkGray);
  renderer.drawLine(x + 10, y, x + width - 11, y, true);
  renderer.fillRect(x, y + 10, width, 20, false);
  renderer.drawRect(x, y + 10, width, 20, 2, true);
  renderer.fillRect(x + 4, y + 30, width - 4, 4, true);
}


int spineWidth(const GfxRenderer& renderer, const char* title, const bool folder) {
  const int glyph = spineGlyphWidth(renderer);
  if (folder) return std::max(48, glyph + 30);
  const uint32_t hash = titleHash(title);
  const size_t len = strlen(title ? title : "");
  return glyph + 14 + static_cast<int>((hash >> 8) % 4) * 4 + static_cast<int>(std::min<size_t>(len / 18, 2)) * 4;
}

int spineHeight(const char* title, const bool folder, const int maxHeight) {
  if (folder) return maxHeight;
  return maxHeight * (78 + static_cast<int>((titleHash(title) >> 16) % 23)) / 100;
}

void drawSpine(const GfxRenderer& renderer, const Rect& spine, const char* title, const bool folder) {
  if (folder)
    drawFileBox(renderer, spine, title);
  else
    drawBookSpine(renderer, spine, title);
}

void drawSpineMarker(const GfxRenderer& renderer, const Rect& spine) {
  const int cx = spine.x + spine.width / 2;
  const int tipY = spine.y - 5;
  for (int i = 0; i < 7; ++i) renderer.fillRect(cx - 7 + i, tipY - 7 + i, 2 * (7 - i) + 1, 1, true);
}

void drawBookend(const GfxRenderer& renderer, const int x, const int plankY) {
  const int base = plankY + kSpineSink;
  renderer.fillRect(x, base - 84, 8, 84, true);
  renderer.fillRect(x, base - 7, 28, 7, true);
  renderer.fillRect(x + 2, base - 80, 2, 70, false);
  for (int i = 0; i < 14; ++i) renderer.fillRect(x + 8, base - 7 - 14 + i, i, 1, true);  // brace
}

namespace {
constexpr int kPlantW = 15;
constexpr int kPlantH = 19;
inline constexpr const char* kPlantArt[kPlantH] = {
    "......##.......", ".....#..#..##..", "..##.#..#.#..#.", ".#..##..##...#.", ".#...#..#...#..",
    "..#..#.#...#...", "...#.#.#..#....", "....####.#.....", ".......###.....", ".......#.......",
    "..###########..", "..#.........#..", "..###########..", "...#.......#...", "...#.#.#.#.#...",
    "...#.......#...", "....#.#.#.#....", "....#.......#..", "....#########.."};
}  // namespace

int shelfDecorWidth(const int kind) { return kind == 0 ? 120 : kPlantW * 4; }

void drawShelfDecor(const GfxRenderer& renderer, const int kind, const int x, const int plankY) {
  const int base = plankY + kSpineSink;
  if (kind == 1) {
    drawPixelArt(renderer, kPlantArt, kPlantW, kPlantH, x, base - kPlantH * 4, 4, true);
    return;
  }
  // Three books lying flat, alternately dark and light, slightly staggered.
  constexpr int bookH = 16;
  const int widths[3] = {116, 102, 108};
  const int offsets[3] = {0, 8, 3};
  for (int i = 0; i < 3; ++i) {
    const int by = base - (i + 1) * bookH;
    const int bx = x + offsets[i];
    if (i % 2 == 0) {
      renderer.fillRect(bx, by, widths[i], bookH, true);
      renderer.fillRect(bx + 10, by + 3, 2, bookH - 6, false);
      renderer.fillRect(bx + widths[i] - 14, by + 3, 2, bookH - 6, false);
    } else {
      renderer.fillRect(bx, by, widths[i], bookH, false);
      renderer.drawRect(bx, by, widths[i], bookH, 2, true);
      renderer.fillRectDither(bx + 20, by + 5, widths[i] - 40, 6, Color::DarkGray);
    }
  }
}

void drawOpeningFrame(const GfxRenderer& renderer, const int frame, const Rect& spine, const char* title,
                      const bool folder, CoverPainter cover, void* user) {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  renderer.clearScreen();
  // Book proportions for the closed book; the spread is two of these side by side.
  const int pageW = std::min(sw / 2 - 26, 210);
  const int pageH = std::min(sh * 2 / 5 + 20, pageW * 3 / 2);
  const int hingeX = sw / 2;
  const int top = (sh - pageH) / 2 - 10;
  const Rect closed{hingeX, top, pageW, pageH};
  const Rect rightPage{hingeX, top, pageW, pageH};

  switch (frame) {
    case 0: {  // drawn out of the shelf, turning towards you
      const Rect b = lerpRect(spine, Rect{hingeX - pageW / 4, top + pageH / 6, pageW * 2 / 3, pageH * 2 / 3}, 1, 2);
      if (b.width < 50) {
        drawSpine(renderer, b, title, folder);
      } else {
        drawClosedBook(renderer, b, title, folder, cover, user);
      }
      break;
    }
    case 1:  // closed, cover facing you
      drawClosedBook(renderer, closed, title, folder, cover, user);
      break;
    case 2:  // cover lifting: still over the pages, foreshortened
      drawTitlePage(renderer, rightPage, title, folder);
      drawSwingingBoard(renderer, closed, hingeX, hingeX + pageW * 45 / 100, pageH / 18, true);
      break;
    case 3:  // cover past upright, falling open to the left
      drawTitlePage(renderer, rightPage, title, folder);
      drawSwingingBoard(renderer, closed, hingeX, hingeX - pageW * 55 / 100, pageH / 22, false);
      break;
    case 4: {  // open flat
      drawFlyleaf(renderer, Rect{hingeX - pageW, top, pageW, pageH}, folder);
      drawTitlePage(renderer, rightPage, title, folder);
      renderer.fillRectDither(hingeX - 4, top + 2, 8, pageH - 4, Color::LightGray);  // gutter shadow
      renderer.drawLine(hingeX, top, hingeX, top + pageH - 1, true);
      break;
    }
    default: {  // the first page comes up to meet you, handing over to the reader
      const Rect full{8, 8, sw - 16, sh - 16};
      drawTitlePage(renderer, lerpRect(rightPage, full, 2, 3), title, folder);
      break;
    }
  }
}

}  // namespace PixelShelf
