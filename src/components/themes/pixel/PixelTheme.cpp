#include "PixelTheme.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "I18n.h"
#include "InxItemLayout.h"
#include "components/UITheme.h"
#include "components/themes/pixel/PixelAppIcons.h"
#include "fontIds.h"

namespace {
constexpr int kRowHeight = 78;
constexpr int kSidePadding = 30;
constexpr int kRuleThickness = 3;
constexpr int kShadow = 4;
constexpr int kMaxValueWidth = 200;

// Selection marker: a 2 px frame with clipped (pixel-rounded) corners.
void drawPixelFrame(const GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  renderer.drawRoundedRect(x, y, w, h, 2, 6, true);
}

// Panel with a hard drop shadow down-right, like a pixel-art window.
void drawShadowPanel(const GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  renderer.fillRect(x + kShadow, y + kShadow, w, h, true);
  renderer.fillRect(x, y, w, h, false);
  renderer.drawRect(x, y, w, h, 2, true);
}

// Circled chevron used as the back affordance in headers.
void drawBackButton(const GfxRenderer& renderer, const int cx, const int cy) {
  constexpr int r = 17;
  renderer.drawRoundedRect(cx - r, cy - r, r * 2, r * 2, 2, r, true);
  renderer.drawLine(cx + 3, cy - 8, cx - 5, cy, 2, true);
  renderer.drawLine(cx - 5, cy, cx + 3, cy + 8, 2, true);
}

// Scroll bar: hollow track with square end caps and a solid thumb.
void drawPixelScrollBar(const GfxRenderer& renderer, const Rect rect, const int itemCount, const int pageStart,
                        const int pageItems) {
  if (itemCount <= pageItems || pageItems <= 0) return;
  const int x = rect.x + rect.width - 12;
  const int top = rect.y + 4;
  const int height = std::max(16, rect.height - 8);
  renderer.drawRect(x, top, 8, height, true);
  renderer.fillRect(x - 1, top - 3, 10, 3, true);
  renderer.fillRect(x - 1, top + height, 10, 3, true);
  const int pages = (itemCount + pageItems - 1) / pageItems;
  const int thumbH = std::max(10, (height - 4) / pages);
  const int page = pageStart / pageItems;
  const int thumbY = top + 2 + (pages > 1 ? page * (height - 4 - thumbH) / (pages - 1) : 0);
  renderer.fillRect(x + 2, thumbY, 4, thumbH, true);
}

// 16x16 scene sprites ('#' = ink): the study-room object each page belongs to.
constexpr const char* kLampArt[16] = {
    "................", "....########....", "...##########...", "..############..", "..#..........#..",
    "...#.#.#.#.#....", ".......##.......", ".......##.......", "......##........", ".....##.........",
    "......##........", ".......##.......", ".......##.......", "....########....", "...##########...",
    "................"};
constexpr const char* kShelfArt[16] = {
    "################", "#..............#", "#.##.#.###.##..#", "#.##.#.###.##.##", "#.##.#.###.##..#",
    "################", "#..............#", "#.###.##.#.###.#", "#.###.##.#.###.#", "#.###.##.#.###.#",
    "################", "#..............#", "#.##.####..##..#", "#.##.####..##..#", "################",
    "#..............#"};
constexpr const char* kClockArt[16] = {
    ".....######.....", "...##......##...", "..#....#.....#..", ".#.....#......#.", ".#.....#......#.",
    "#......#.......#", "#......####....#", "#..............#", ".#............#.", ".#............#.",
    "..#..........#..", "...##......##...", ".....######.....", ".......##.......", "......####......",
    "......####......"};
constexpr const char* kCabinetArt[16] = {
    "................", "....##...####...", "....##...#..#...", "################", "#..............#",
    "#.....####.....#", "#..............#", "################", "#..............#", "#.....####.....#",
    "#..............#", "################", "#..............#", "#.....####.....#", "################",
    ".##..........##."};

constexpr const char* kRadioArt[16] = {
    "................", "............#...", "...........#....", "..........#.....", "..#############.",
    "..#...........#.", "..#.###..####.#.", "..#.#.#..#..#.#.", "..#.###..####.#.", "..#...........#.",
    "..#.#.#.#.#.#.#.", "..#...........#.", "..#############.", "...#.........#..", "................",
    "................"};

// Art for pages drawn with the generic header, picked by their title.
const char* const* headerArtFor(const char* title) {
  if (!title) return PixelAppIcons::kCatFace;
  const auto is = [title](const StrId id) { return std::strcmp(title, I18N.get(id)) == 0; };
  if (is(StrId::STR_FILE_TRANSFER)) return kRadioArt;
  if (is(StrId::STR_ROOM_SHELF)) return kShelfArt;
  if (is(StrId::STR_READING_STATS)) return kClockArt;
  if (is(StrId::STR_WEREAD_TITLE)) return PixelAppIcons::kOpenBook;
  if (is(StrId::STR_AO3_TITLE)) return PixelAppIcons::kHeartScroll;
  static constexpr struct {
    StrId title;
    UIIcon icon;
  } kAppPages[] = {
      {StrId::STR_SUDOKU_TITLE, UIIcon::Sudoku},        {StrId::STR_GOMOKU_TITLE, UIIcon::Gomoku},
      {StrId::STR_SOKOBAN_TITLE, UIIcon::Sokoban},      {StrId::STR_CHINESE_CHESS_TITLE, UIIcon::ChineseChess},
      {StrId::STR_MINESWEEPER_TITLE, UIIcon::Minesweeper}, {StrId::STR_2048_TITLE, UIIcon::Game2048},
      {StrId::STR_UGLY_AVATAR, UIIcon::Avatar},         {StrId::STR_PIXEL_SWITCH_TITLE, UIIcon::PixelSwitch},
      {StrId::STR_CALCULATOR_TITLE, UIIcon::Calculator}, {StrId::STR_WOODFISH_TITLE, UIIcon::Woodfish},
      {StrId::STR_STANDBY_TITLE, UIIcon::Standby},      {StrId::STR_AIRPAGE_TITLE, UIIcon::AirPage},
  };
  for (const auto& page : kAppPages)
    if (is(page.title)) return PixelAppIcons::forIcon(page.icon);
  return PixelAppIcons::kCatFace;
}

struct Scene {
  StrId title;
  const char* subtitle;
  const char* const* art;
};

Scene sceneFor(const MainTab tab) {
  switch (tab) {
    case MainTab::Library:
      return {StrId::STR_ROOM_LIBRARY, "LIBRARY", kShelfArt};
    case MainTab::Apps:
      return {StrId::STR_ROOM_APPS, "APPS", kCabinetArt};
    case MainTab::Settings:
      return {StrId::STR_ROOM_SETTINGS, "SETTINGS", kLampArt};
    case MainTab::Statistics:
      return {StrId::STR_ROOM_STATS, "STATS", kClockArt};
    default:
      return {StrId::STR_ROOM_RECENT, "RECENT", kShelfArt};
  }
}

void drawArt(const GfxRenderer& renderer, const char* const* rows, const int x, const int y, const int scale) {
  for (int r = 0; r < 16; ++r)
    for (int c = 0; c < 16; ++c)
      if (rows[r][c] == '#') renderer.fillRect(x + c * scale, y + r * scale, scale, scale, true);
}
}  // namespace

void PixelTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title,
                            const char* subtitle) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  const int midY = rect.y + rect.height / 2;
  drawBackButton(renderer, rect.x + kSidePadding + 17, midY);

  const int titleX = rect.x + kSidePadding + 17 * 2 + 14;
  int titleRight = rect.x + rect.width - kSidePadding;
  if (subtitle && *subtitle) {
    const int w = std::min(renderer.getTextWidth(UI_10_FONT_ID, subtitle), rect.width / 3);
    titleRight -= w + 10;
    const std::string shown = renderer.truncatedText(UI_10_FONT_ID, subtitle, w);
    renderer.drawText(UI_10_FONT_ID, titleRight + 10, midY - renderer.getLineHeight(UI_10_FONT_ID) / 2,
                      shown.c_str());
  }
  constexpr int kArtScale = 3;
  const int artSize = 16 * kArtScale;
  titleRight -= artSize + 12;
  drawArt(renderer, headerArtFor(title), rect.x + rect.width - kSidePadding - artSize, midY - artSize / 2, kArtScale);
  if (title && *title) {
    const std::string shown =
        renderer.truncatedText(UI_12_FONT_ID, title, std::max(1, titleRight - titleX), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, titleX, midY - renderer.getLineHeight(UI_12_FONT_ID) / 2, shown.c_str(), true,
                      EpdFontFamily::BOLD);
  }
  renderer.fillRect(rect.x + kSidePadding, rect.y + rect.height - kRuleThickness, rect.width - kSidePadding * 2,
                    kRuleThickness, true);
}

// Same drop-shadowed tile as the Apps grid; the shadow stays inside rect. Active = inverted.
void PixelTheme::drawActionButton(const GfxRenderer& renderer, const Rect rect, const char* label,
                                  const bool active) const {
  constexpr int kShadow = 4;
  constexpr int kRadius = 8;
  if (rect.width <= kShadow * 3 || rect.height <= kShadow * 3 || !label || !*label) return;
  const Rect face{rect.x, rect.y, rect.width - kShadow, rect.height - kShadow};
  renderer.fillRect(face.x + kShadow, face.y + kShadow, face.width, face.height, true);
  renderer.fillRoundedRect(face.x, face.y, face.width, face.height, kRadius, active ? Color::Black : Color::White);
  renderer.drawRoundedRect(face.x, face.y, face.width, face.height, 2, kRadius, true);
  const std::string shown = renderer.truncatedText(UI_10_FONT_ID, label, std::max(1, face.width - 12), EpdFontFamily::BOLD);
  const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, shown.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, face.x + (face.width - textWidth) / 2,
                    face.y + (face.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2, shown.c_str(), !active,
                    EpdFontFamily::BOLD);
}

int PixelTheme::getListRowStep(const bool) const { return kRowHeight; }

int PixelTheme::getListPageItems(const int contentHeight, const bool) const {
  return std::max(1, contentHeight / kRowHeight);
}

void PixelTheme::drawList(const GfxRenderer& renderer, const Rect rect, const int itemCount, const int selectedIndex,
                          const std::function<std::string(int index)>& rowTitle,
                          const std::function<std::string(int index)>& rowSubtitle,
                          const std::function<UIIcon(int index)>& rowIcon,
                          const std::function<std::string(int index)>& rowValue, const bool,
                          const std::function<bool(int index)>& rowDimmed, const bool showSelection,
                          const std::function<bool(int index)>& rowHeading) const {
  if (itemCount <= 0 || rect.height <= 0) return;

  const int pageItems = getListPageItems(rect.height, rowSubtitle != nullptr);
  const int pageStart = selectedIndex >= 0 ? selectedIndex / pageItems * pageItems : 0;
  const int pageEnd = std::min(itemCount, pageStart + pageItems);
  const bool hasScrollBar = itemCount > pageItems;
  const int left = rect.x + kSidePadding;
  const int right = rect.x + rect.width - kSidePadding - (hasScrollBar ? 12 : 0);
  const bool selectionVisible = showSelection && UITheme::getInstance().showSelectionCursor();

  for (int index = pageStart; index < pageEnd; ++index) {
    const int rowY = rect.y + (index - pageStart) * kRowHeight;
    if (selectionVisible && index == selectedIndex) drawPixelFrame(renderer, left - 8, rowY + 6, right - left + 16, kRowHeight - 12);

    int textX = left;
    if (rowIcon) {
      constexpr int iconSize = 32;
      if (const uint8_t* bitmap = iconForName(rowIcon(index), iconSize)) {
        renderer.drawIcon(bitmap, textX, rowY + (kRowHeight - iconSize) / 2, iconSize);
        textX += iconSize + 12;
      }
    }

    std::string value;
    int valueWidth = 0;
    if (rowValue) {
      value = renderer.truncatedText(UI_12_FONT_ID, rowValue(index).c_str(), kMaxValueWidth);
      valueWidth = value.empty() ? 0 : renderer.getTextWidth(UI_12_FONT_ID, value.c_str()) + 12;
    }
    const bool heading = rowHeading && rowHeading(index);
    const auto style = heading ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textWidth = std::max(1, right - textX - valueWidth);
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, rowTitle(index).c_str(), textWidth, style);
    const int lh = renderer.getLineHeight(UI_12_FONT_ID);
    const int titleY = rowY + (rowSubtitle ? 10 : (kRowHeight - lh) / 2);
    renderer.drawText(UI_12_FONT_ID, textX, titleY, title.c_str(), true, style);
    if (rowSubtitle) {
      const std::string sub = renderer.truncatedText(SMALL_FONT_ID, rowSubtitle(index).c_str(), textWidth);
      renderer.drawText(SMALL_FONT_ID, textX, titleY + lh + 2, sub.c_str());
    }
    if (!value.empty()) {
      renderer.drawText(UI_12_FONT_ID, right - renderer.getTextWidth(UI_12_FONT_ID, value.c_str()),
                        rowY + (kRowHeight - lh) / 2, value.c_str());
    }
    if (rowDimmed && rowDimmed(index)) {
      for (int y = rowY + 4; y < rowY + kRowHeight - 4; y += 2)
        for (int x = textX + ((y / 2) & 1); x < right; x += 2) renderer.drawPixel(x, y, false);
    }
    renderer.drawLine(left, rowY + kRowHeight - 1, right, rowY + kRowHeight - 1, true);
  }

  drawPixelScrollBar(renderer, rect, itemCount, pageStart, pageItems);
}

Rect PixelTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message);
  const int lh = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = std::min(renderer.getScreenWidth() - 40, textWidth + 64);
  const int h = lh + 40;
  const int x = (renderer.getScreenWidth() - w) / 2;
  const int y = 60;
  renderer.fillRect(x, y, w, h, false);
  renderer.drawRoundedRect(x, y, w, h, 2, 12, true);
  renderer.drawText(UI_12_FONT_ID, x + (w - textWidth) / 2, y + 20, message);
  renderer.displayBuffer();
  return Rect{x, y, w, h};
}

void PixelTheme::drawOptionPopup(const GfxRenderer& renderer, const char* title,
                                 const std::vector<std::string>& options, const int selectedIndex) const {
  if (options.empty()) return;
  // Same panel geometry as Inx: OptionPopup hit-tests against InxOptionGeometry.
  const int optionCount = static_cast<int>(options.size());
  const int selected = std::clamp(selectedIndex, 0, optionCount - 1);
  const int visibleRows = InxOptionGeometry::visibleRows(optionCount);
  const int start = InxOptionGeometry::start(selected, optionCount);
  const int panelWidth = std::max(1, std::min(renderer.getScreenWidth() - 24, 360));
  const int panelHeight = InxOptionGeometry::headerHeight + visibleRows * InxOptionGeometry::rowHeight;
  const int panelX = (renderer.getScreenWidth() - panelWidth) / 2;
  const int panelY = std::max(0, (renderer.getScreenHeight() - panelHeight) / 2);
  const bool selectionVisible = UITheme::getInstance().showSelectionCursor();

  drawShadowPanel(renderer, panelX, panelY, panelWidth, panelHeight);
  const int headerMid = panelY + InxOptionGeometry::headerHeight / 2;
  const std::string shownTitle = renderer.truncatedText(UI_12_FONT_ID, title, panelWidth - 80, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, panelX + 16, headerMid - renderer.getLineHeight(UI_12_FONT_ID) / 2,
                    shownTitle.c_str(), true, EpdFontFamily::BOLD);
  // Close mark (the popup closes on Back or a tap outside an option).
  const int cx = panelX + panelWidth - 30;
  renderer.drawRoundedRect(cx - 13, headerMid - 13, 26, 26, 2, 13, true);
  renderer.drawLine(cx - 5, headerMid - 5, cx + 5, headerMid + 5, 2, true);
  renderer.drawLine(cx - 5, headerMid + 5, cx + 5, headerMid - 5, 2, true);

  for (int slot = 0; slot < visibleRows; ++slot) {
    const int optionIndex = start + slot;
    const int rowY = panelY + InxOptionGeometry::headerHeight + slot * InxOptionGeometry::rowHeight;
    if (selectionVisible && optionIndex == selected) {
      drawPixelFrame(renderer, panelX + 10, rowY + 4, panelWidth - 30, InxOptionGeometry::rowHeight - 8);
    }
    const std::string option = renderer.truncatedText(UI_12_FONT_ID, options[optionIndex].c_str(), panelWidth - 60);
    renderer.drawText(UI_12_FONT_ID, panelX + 26,
                      rowY + (InxOptionGeometry::rowHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                      option.c_str());
  }
  if (optionCount > visibleRows) {
    drawPixelScrollBar(renderer,
                       Rect{panelX, panelY + InxOptionGeometry::headerHeight, panelWidth - 4,
                            visibleRows * InxOptionGeometry::rowHeight},
                       optionCount, start, visibleRows);
  }
}

// Pixel replaces the Inx tab strip with a scene header: back-to-room button,
// the page title with a small caps subtitle, and the room object it belongs to.
void PixelTheme::drawMainTabBar(const GfxRenderer& renderer, const Rect rect, const MainTab selected) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  const Scene scene = sceneFor(selected);
  const int midY = rect.y + rect.height / 2;
  drawBackButton(renderer, rect.x + kSidePadding + 17, midY);

  const int titleX = rect.x + kSidePadding + 17 * 2 + 16;
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  const int subH = renderer.getLineHeight(SMALL_FONT_ID);
  const int titleY = midY - (titleH + subH + 4) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, titleY, I18N.get(scene.title), true, EpdFontFamily::BOLD);
  const int subY = titleY + titleH + 4;
  const int subW = renderer.getTextWidth(SMALL_FONT_ID, scene.subtitle);
  for (int x = titleX; x < titleX + subW; x += 3) renderer.drawPixel(x, subY - 2, true);  // dotted rule
  renderer.drawText(SMALL_FONT_ID, titleX, subY, scene.subtitle);

  constexpr int kArtScale = 4;
  const int artSize = 16 * kArtScale;
  drawArt(renderer, scene.art, rect.x + rect.width - kSidePadding - artSize, rect.y + (rect.height - artSize) / 2 - 2,
          kArtScale);

  renderer.fillRect(rect.x + kSidePadding, rect.y + rect.height - kRuleThickness, rect.width - kSidePadding * 2,
                    kRuleThickness, true);
}
