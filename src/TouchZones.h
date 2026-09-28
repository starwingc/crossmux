#pragma once

#include <I18n.h>

#include <algorithm>
#include <cstdint>

// Custom reader tap zones: the page is split into a 3x3 grid in the current
// orientation and each cell runs one action. Stored in
// CrossPointSettings::touchZoneActions (row-major) and used when
// touchReaderControls == TOUCH_READER_CUSTOM.
namespace touchZones {

// Persisted values: append only, never reorder.
enum class Action : uint8_t {
  None = 0,
  PrevPage,
  NextPage,
  Menu,
  ToggleBookmark,
  Bookmarks,
  Chapters,
  Highlight,
  Highlights,
  Dictionary,
  NightMode,
  FrontlightToggle,
  GoToPercent,
  Screenshot,
  GoHome,
  Count,
};

constexpr int kColumns = 3;
constexpr int kRows = 3;
constexpr int kZoneCount = kColumns * kRows;

// Matches the fixed Tap mode: outer columns turn pages, the centre cell opens
// the menu, the top/bottom centre cells do nothing.
constexpr uint8_t kDefaultActions[kZoneCount] = {
    static_cast<uint8_t>(Action::PrevPage), static_cast<uint8_t>(Action::None), static_cast<uint8_t>(Action::NextPage),
    static_cast<uint8_t>(Action::PrevPage), static_cast<uint8_t>(Action::Menu), static_cast<uint8_t>(Action::NextPage),
    static_cast<uint8_t>(Action::PrevPage), static_cast<uint8_t>(Action::None), static_cast<uint8_t>(Action::NextPage),
};

inline int zoneAt(const int x, const int y, const int width, const int height) {
  if (width <= 0 || height <= 0) return -1;
  const int col = std::clamp(x * kColumns / width, 0, kColumns - 1);
  const int row = std::clamp(y * kRows / height, 0, kRows - 1);
  return row * kColumns + col;
}

inline StrId label(const Action action) {
  switch (action) {
    case Action::PrevPage:
      return StrId::STR_ZONE_PREV_PAGE;
    case Action::NextPage:
      return StrId::STR_ZONE_NEXT_PAGE;
    case Action::Menu:
      return StrId::STR_ZONE_MENU;
    case Action::ToggleBookmark:
      return StrId::STR_TOGGLE_BOOKMARK;
    case Action::Bookmarks:
      return StrId::STR_BOOKMARKS;
    case Action::Chapters:
      return StrId::STR_SELECT_CHAPTER;
    case Action::Highlight:
      return StrId::STR_HIGHLIGHT;
    case Action::Highlights:
      return StrId::STR_HIGHLIGHTS;
    case Action::Dictionary:
      return StrId::STR_LOOKUP;
    case Action::NightMode:
      return StrId::STR_NIGHT_MODE;
    case Action::FrontlightToggle:
      return StrId::STR_FRONTLIGHT;
    case Action::GoToPercent:
      return StrId::STR_GO_TO_PERCENT;
    case Action::Screenshot:
      return StrId::STR_SCREENSHOT_BUTTON;
    case Action::GoHome:
      return StrId::STR_GO_HOME_BUTTON;
    case Action::None:
    case Action::Count:
      break;
  }
  return StrId::STR_ZONE_NONE;
}

}  // namespace touchZones
