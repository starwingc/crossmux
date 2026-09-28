#pragma once

#include <Txt.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class TxtReaderChapterSelectionActivity final : public Activity {
  const Txt& txt;
  HalFile chapterFile;
  ButtonNavigator buttonNavigator;
  uint32_t chapterCount = 0;
  uint32_t currentOffset = 0;
  int selectorIndex = 0;
  // Row 0 opens the book's underline list (the TXT reader has no menu of its
  // own); chapters follow. Chosen, it finishes with a MenuResult.
  bool showHighlights = false;
  int extraRows() const { return showHighlights ? 1 : 0; }
  int totalRows() const { return static_cast<int>(chapterCount) + extraRows(); }

  int getPageItems() const;
  std::string chapterTitle(int index);
  void selectChapter();

 public:
  explicit TxtReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Txt& txt,
                                             uint32_t currentOffset, bool showHighlights = false)
      : Activity("TxtReaderChapterSelection", renderer, mappedInput),
        txt(txt),
        currentOffset(currentOffset),
        showHighlights(showHighlights) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
