#pragma once

#include <cstddef>
#include <cstdint>

#include "components/themes/BaseTheme.h"

class GfxRenderer;
class MappedInputManager;

inline int gameCenterY(int boxH, int textH) { return (boxH - textH) / 2; }
inline int gameCenteredBlockY(int top, int bottom, int blockH) { return top + (bottom - top - blockH) / 2; }

void gameFormatElapsed(uint32_t ms, char* out, size_t outLen);

// In-game status bar chrome: the rule under the bar and, in the Pixel theme, a thick rule
// plus the game's pixel icon in front of the title. Returns the x where the title starts.
int gameDrawTitleBarFrame(const GfxRenderer& renderer, int barH, UIIcon icon, int textX);

bool gameGridCellFromPoint(const Rect& grid, int rows, int columns, int x, int y, int& row, int& column);
bool gameIntersectionFromPoint(int originX, int originY, int pitch, int rows, int columns, int x, int y, int& row,
                               int& column);
Rect gameTouchActionRect(int screenWidth, int screenHeight, int sidePadding, int gap, int height, int index, int count);
