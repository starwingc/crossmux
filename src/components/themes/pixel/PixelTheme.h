#pragma once

#include "components/themes/inx/InxTheme.h"

// "Pixel" look on top of the Inx main-tab shell: thick rules, framed (not
// inverted) selections, drop-shadowed popups and a pixel-art scroll bar, after
// CrossLink Pixel's reading-focused UI. Drawing is original; only the layout
// language follows Pixel.
namespace PixelMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics metrics = InxMetrics::values;
  metrics.headerHeight = 96;
  metrics.listRowHeight = 78;
  metrics.listWithSubtitleRowHeight = 78;
  metrics.menuRowHeight = 78;
  metrics.contentSidePadding = 30;
  metrics.listSidePadding = 30;
  metrics.listSelectionStyle = 4;  // freeink::ui::SelectionStyle::Frame
  metrics.listSeparatorStyle = 1;  // Solid
  metrics.listRowRadius = 8;
  metrics.headerUnderlineSize = 3;
  return metrics;
}

inline constexpr ThemeMetrics values = makeValues();
}  // namespace PixelMetrics

class PixelTheme final : public InxTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  int getListRowStep(bool hasSubtitle) const override;
  int getListPageItems(int contentHeight, bool hasSubtitle) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowDimmed = nullptr,
                bool showSelection = true, const std::function<bool(int index)>& rowHeading = nullptr) const override;
  Rect drawPopup(const GfxRenderer& renderer, const char* message) const override;
  void drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                       int selectedIndex) const override;
  void drawMainTabBar(const GfxRenderer& renderer, Rect rect, MainTab selected) const override;
  bool showsFileIcons() const override { return true; }
};
