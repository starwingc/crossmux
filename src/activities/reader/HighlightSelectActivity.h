#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"

// One selectable piece of text on the current reader page: a word for Latin
// text, a single character for CJK. Coordinates are screen pixels; start/end
// are the reader's anchor offsets (see HighlightEntry).
struct HighlightUnit {
  int16_t x = 0;
  int16_t y = 0;  // top of the text line
  int16_t width = 0;
  int16_t height = 0;
  uint16_t row = 0;  // visual line index, for Up/Down
  uint32_t start = 0;
  uint32_t end = 0;
  std::string text;
  bool spaceBefore = false;  // join with a space when building the excerpt
};

// Text selection over the current page, shared by the EPUB and TXT readers.
// The reader passes the page's units, a callback that draws the page, and the
// saved highlight ranges on it; this activity only adds the selection marks.
//
// Flows:
//   Touch: the reader starts it from a long-press with `startUnit` set and the
//     contact still down. Dragging extends the underline and lifting saves it
//     (a lift without a drag underlines the pressed word). A long-press on an
//     existing underline removes it at once.
//   Buttons: started from the reader menu with startUnit = -1. Move the
//     cursor, Confirm sets the start, move again, Confirm underlines.
// Back always cancels. The result is a HighlightResult.
class HighlightSelectActivity final : public Activity {
 public:
  HighlightSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<HighlightUnit> units,
                          std::function<void()> drawPage, int startUnit,
                          std::vector<std::pair<uint32_t, uint32_t>> savedRanges);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Draws the saved-highlight underline under [x, x + width) of a text line
  // whose top is `y` (shared with the readers so both look the same).
  static void drawSavedUnderline(const GfxRenderer& renderer, int x, int y, int width, int lineHeight);

 private:
  enum class Mode : uint8_t { PickStart, PickEnd, Drag, Remove };

  int unitAt(int x, int y) const;
  // unitAt(), falling back to the closest unit on the nearest row (between lines).
  int nearestUnit(int x, int y) const;
  void loopDrag();
  int closestInRow(uint16_t row, int centerX) const;
  void moveVertical(int direction);
  void moveHorizontal(int direction);
  int savedRangeAt(int unit) const;
  void commitAdd();
  void commitRemove();
  void drawSelection() const;

  std::vector<HighlightUnit> units;
  std::function<void()> drawPage;
  std::vector<std::pair<uint32_t, uint32_t>> savedRanges;
  Mode mode = Mode::PickStart;
  int anchor = 0;
  int cursor = 0;
  int removeRange = -1;  // index into savedRanges in Remove
  bool dragDirty = false;
  unsigned long lastDragRenderMs = 0;
  uint16_t rowCount = 0;
  unsigned long lastHorizontalMoveTime = 0;
};
