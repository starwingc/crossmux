// Theme-dependent game chrome; kept out of GameUi.cpp so the host tests of the
// pure layout helpers don't need the renderer or settings.
#include <GfxRenderer.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "GameUi.h"
#include "components/themes/pixel/PixelAppIcons.h"

int gameDrawTitleBarFrame(const GfxRenderer& renderer, const int barH, const UIIcon icon, const int textX) {
  const int w = renderer.getScreenWidth();
  if (SETTINGS.uiTheme != CrossPointSettings::UI_THEME::PIXEL) {
    renderer.drawLine(0, barH, w - 1, barH, true);
    return textX;
  }
  constexpr int kRule = 3;
  renderer.fillRect(0, barH, w, kRule, true);  // grows down into the gap above the board, clear of the text
  const auto art = PixelAppIcons::forIcon(icon);
  if (!art) return textX;
  constexpr int kScale = 2;
  constexpr int kArtSize = PixelAppIcons::kSize * kScale;
  const int artY = std::max(0, (barH - kArtSize) / 2);
  for (int r = 0; r < PixelAppIcons::kSize; ++r)
    for (int c = 0; c < PixelAppIcons::kSize; ++c)
      if (art[r][c] == '#') renderer.fillRect(textX + c * kScale, artY + r * kScale, kScale, kScale, true);
  return textX + kArtSize + 8;
}
