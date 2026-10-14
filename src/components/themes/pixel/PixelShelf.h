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

// --- Library as a real shelf: books stand spine-out, folders are file boxes. ---
// Spine size for an entry; both are stable for a given title so the shelf never reshuffles.
int spineWidth(const GfxRenderer& renderer, const char* title, bool folder);
int spineHeight(const char* title, bool folder, int maxHeight);
// A spine with its title stacked top to bottom (a folder draws as a file box).
void drawSpine(const GfxRenderer& renderer, const Rect& spine, const char* title, bool folder);
// Marker over the selected (pulled-out) spine.
void drawSpineMarker(const GfxRenderer& renderer, const Rect& spine);
// An L-shaped bookend standing on a plank whose top face is at plankY.
void drawBookend(const GfxRenderer& renderer, int x, int plankY);
// Spines stand this far into the plank's top face (its back edge).
constexpr int kSpineSink = 8;
// Odds and ends for spare plank space: a stack of books lying flat (kind 0) or a potted plant (kind 1).
int shelfDecorWidth(int kind);
void drawShelfDecor(const GfxRenderer& renderer, int kind, int x, int plankY);

// --- Opening a book: it leaves the shelf, turns its cover to you and opens. ---
// Paints a book cover into a rect; returns false when there is none to paint.
using CoverPainter = bool (*)(const GfxRenderer& renderer, const Rect& rect, void* user);
constexpr int kOpenFrames = 6;
// Full-screen frame `frame` (0..kOpenFrames-1) of the animation starting from `spine`.
void drawOpeningFrame(const GfxRenderer& renderer, int frame, const Rect& spine, const char* title, bool folder,
                      CoverPainter cover, void* user);

}  // namespace PixelShelf
