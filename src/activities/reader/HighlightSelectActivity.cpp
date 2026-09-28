#include "HighlightSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long REPEAT_START_MS = 500;
constexpr unsigned long REPEAT_INTERVAL_MS = 250;
constexpr int TOUCH_SLOP = 4;
// Saved highlights: a 2px rule just under the glyphs. The live selection is
// thicker so it reads as "not saved yet".
constexpr int SAVED_THICKNESS = 2;
constexpr int SELECTION_THICKNESS = 4;
constexpr size_t kMaxExcerptBytes = 160;
}  // namespace

HighlightSelectActivity::HighlightSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::vector<HighlightUnit> units, std::function<void()> drawPage,
                                                 const int startUnit,
                                                 std::vector<std::pair<uint32_t, uint32_t>> savedRanges)
    : Activity("HighlightSelect", renderer, mappedInput),
      units(std::move(units)),
      drawPage(std::move(drawPage)),
      savedRanges(std::move(savedRanges)) {
  if (startUnit >= 0 && startUnit < static_cast<int>(this->units.size())) {
    anchor = cursor = startUnit;
    removeRange = savedRangeAt(startUnit);
    mode = removeRange >= 0 ? Mode::ConfirmRemove : Mode::PickEnd;
  }
}

void HighlightSelectActivity::drawSavedUnderline(const GfxRenderer& renderer, const int x, const int y, const int width,
                                                 const int lineHeight) {
  if (width <= 0) return;
  renderer.fillRect(x, y + lineHeight - SAVED_THICKNESS, width, SAVED_THICKNESS, true);
}

void HighlightSelectActivity::onEnter() {
  Activity::onEnter();
  rowCount = 0;
  for (const auto& unit : units) rowCount = std::max<uint16_t>(rowCount, static_cast<uint16_t>(unit.row + 1));
  if (mode == Mode::PickStart && !units.empty()) {
    // Button flow: start mid-page so any word is at most half a page away.
    const int initial = closestInRow(static_cast<uint16_t>(rowCount / 2), renderer.getScreenWidth() / 2);
    anchor = cursor = initial >= 0 ? initial : 0;
  }
  requestUpdate();
}

int HighlightSelectActivity::savedRangeAt(const int unit) const {
  if (unit < 0 || unit >= static_cast<int>(units.size())) return -1;
  const uint32_t offset = units[unit].start;
  for (size_t i = 0; i < savedRanges.size(); ++i) {
    if (offset >= savedRanges[i].first && offset < savedRanges[i].second) return static_cast<int>(i);
  }
  return -1;
}

int HighlightSelectActivity::unitAt(const int x, const int y) const {
  for (int i = 0; i < static_cast<int>(units.size()); i++) {
    const auto& u = units[i];
    if (x >= u.x - TOUCH_SLOP && x < u.x + u.width + TOUCH_SLOP && y >= u.y - TOUCH_SLOP &&
        y < u.y + u.height + TOUCH_SLOP) {
      return i;
    }
  }
  return -1;
}

int HighlightSelectActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(units.size()); i++) {
    if (units[i].row != row) continue;
    const int distance = std::abs(units[i].x + units[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

void HighlightSelectActivity::moveVertical(const int direction) {
  const auto& current = units[cursor];
  const int targetRow = static_cast<int>(current.row) + direction;
  if (targetRow < 0 || targetRow >= static_cast<int>(rowCount)) return;
  const int best = closestInRow(static_cast<uint16_t>(targetRow), current.x + current.width / 2);
  if (best >= 0 && best != cursor) {
    cursor = best;
    if (mode == Mode::PickStart) anchor = cursor;
    requestUpdate();
  }
}

void HighlightSelectActivity::moveHorizontal(const int direction) {
  const int next = cursor + direction;
  if (next < 0 || next >= static_cast<int>(units.size())) return;
  cursor = next;
  if (mode == Mode::PickStart) anchor = cursor;
  lastHorizontalMoveTime = millis();
  requestUpdate();
}

void HighlightSelectActivity::commitAdd() {
  const int first = std::min(anchor, cursor);
  const int last = std::max(anchor, cursor);
  HighlightResult result;
  result.start = units[first].start;
  result.end = units[last].end;
  for (int i = first; i <= last && result.text.size() < kMaxExcerptBytes; ++i) {
    if (i > first && units[i].spaceBefore) result.text += ' ';
    result.text += units[i].text;
  }
  setResult(std::move(result));
  finish();
}

void HighlightSelectActivity::commitRemove() {
  HighlightResult result;
  result.remove = true;
  result.start = savedRanges[removeRange].first;
  result.end = savedRanges[removeRange].second;
  setResult(std::move(result));
  finish();
}

void HighlightSelectActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  if (units.empty()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (mode) {
      case Mode::PickStart:
        anchor = cursor;
        mode = Mode::PickEnd;
        requestUpdate();
        return;
      case Mode::PickEnd:
        commitAdd();
        return;
      case Mode::ConfirmRemove:
        commitRemove();
        return;
    }
  }

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int hit = unitAt(tx, ty);
    if (mode == Mode::ConfirmRemove) {
      if (hit >= 0 && savedRangeAt(hit) == removeRange) {
        commitRemove();
      } else {
        ActivityResult cancelled;
        cancelled.isCancelled = true;
        setResult(std::move(cancelled));
        finish();
      }
      return;
    }
    if (hit < 0) return;
    if (mode == Mode::PickStart) {
      anchor = cursor = hit;
      mode = Mode::PickEnd;
    } else if (hit == cursor) {
      commitAdd();  // second tap on the end word confirms
      return;
    } else {
      cursor = hit;
    }
    requestUpdate();
    return;
  }

  if (mode == Mode::ConfirmRemove) return;

  const unsigned long now = millis();
  const bool repeat =
      mappedInput.getHeldTime() >= REPEAT_START_MS && now - lastHorizontalMoveTime >= REPEAT_INTERVAL_MS;
  if (mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) ||
      (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenLeft))) {
    moveHorizontal(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) ||
             (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenRight))) {
    moveHorizontal(1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
    moveVertical(1);
  }
}

void HighlightSelectActivity::drawSelection() const {
  if (units.empty()) return;
  int first = std::min(anchor, cursor);
  int last = std::max(anchor, cursor);
  if (mode == Mode::ConfirmRemove) {
    // Mark the whole saved range that would be removed.
    first = INT_MAX;
    last = -1;
    for (int i = 0; i < static_cast<int>(units.size()); ++i) {
      if (savedRangeAt(i) != removeRange) continue;
      first = std::min(first, i);
      last = std::max(last, i);
    }
    if (last < 0) return;
  }
  // One bar per visual row, spanning the selected units on it.
  for (int i = first; i <= last;) {
    const uint16_t row = units[i].row;
    int left = units[i].x;
    int right = units[i].x + units[i].width;
    int j = i + 1;
    while (j <= last && units[j].row == row) {
      left = std::min<int>(left, units[j].x);
      right = std::max<int>(right, units[j].x + units[j].width);
      ++j;
    }
    renderer.fillRect(left, units[i].y + units[i].height - SELECTION_THICKNESS, right - left, SELECTION_THICKNESS,
                      true);
    i = j;
  }
  // Box around the unit the buttons/taps move.
  if (mode != Mode::ConfirmRemove) {
    const auto& c = units[cursor];
    renderer.drawRect(c.x - 2, c.y - 2, c.width + 4, c.height + 4, 1, true);
  }
}

void HighlightSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (drawPage) drawPage();
  drawSelection();

  // Instruction banner: over the top margin, or at the bottom (above the
  // button hints) while the words being picked sit under the top one.
  const char* banner = nullptr;
  switch (mode) {
    case Mode::PickStart:
      banner = tr(STR_HIGHLIGHT_PICK_START);
      break;
    case Mode::PickEnd:
      banner = mappedInput.hasTouch() ? tr(STR_HIGHLIGHT_PICK_END_TOUCH) : tr(STR_HIGHLIGHT_PICK_END);
      break;
    case Mode::ConfirmRemove:
      banner = tr(STR_HIGHLIGHT_CONFIRM_REMOVE);
      break;
  }
  if (units.empty()) banner = tr(STR_HIGHLIGHT_NO_TEXT);
  const int bannerHeight = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int screenWidth = renderer.getScreenWidth();
  bool atBottom = false;
  if (!units.empty()) {
    const int topMost = std::min<int>(units[cursor].y, units[anchor].y);
    atBottom = topMost - 2 < bannerHeight;
  }
  if (atBottom) {
    const int y = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight - bannerHeight;
    renderer.fillRect(0, y, screenWidth, bannerHeight, false);
    renderer.drawLine(0, y, screenWidth - 1, y, true);
    renderer.drawCenteredText(UI_10_FONT_ID, y + 3, banner);
  } else {
    renderer.fillRect(0, 0, screenWidth, bannerHeight, false);
    renderer.drawLine(0, bannerHeight, screenWidth - 1, bannerHeight, true);
    renderer.drawCenteredText(UI_10_FONT_ID, 3, banner);
  }

  const char* confirm = "";
  if (!units.empty()) {
    confirm = mode == Mode::PickStart       ? tr(STR_HIGHLIGHT_SET_START)
              : mode == Mode::ConfirmRemove ? tr(STR_DELETE)
                                            : tr(STR_HIGHLIGHT_UNDERLINE);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), confirm, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
