#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Pixel-theme bookshelf pieces shared by the recent-books shelf and the library:
// framed book cards (PixelLab art, 9-sliced to any size) standing on planks.
namespace PixelShelf {

// Card size used by both shelves, so cover thumbnails are generated once.
constexpr int kCardW = 128;
constexpr int kCardH = 188;
constexpr int kPlankH = 34;

// Where a cover goes inside a card frame.
Rect coverRect(const Rect& card);
// Height to request cover thumbnails at for a kCardW x kCardH card.
int coverThumbHeight();

// The frame alone, drawn over a cover already painted into coverRect().
void drawCardFrame(const GfxRenderer& renderer, const Rect& card);
// A card with no cover: title, wavy divider, open book, and a pill holding `label` (may be empty).
void drawNoCoverCard(const GfxRenderer& renderer, const Rect& card, const char* title, const char* label);
// A folder card: the folder name over a pixel folder.
void drawFolderCard(const GfxRenderer& renderer, const Rect& card, const char* name);
// Selection ring around a card.
void drawCardSelection(const GfxRenderer& renderer, const Rect& card);
// A plank whose top face is at y, spanning x..x+width.
void drawPlank(const GfxRenderer& renderer, int x, int width, int y);

}  // namespace PixelShelf
