#include "PixelShelf.h"

#include <GfxRenderer.h>

#include <algorithm>
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

}  // namespace PixelShelf
