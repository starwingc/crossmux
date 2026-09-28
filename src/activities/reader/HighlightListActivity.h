#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/HighlightFile.h"

// Lists a book's highlights (excerpt + position). Open jumps to the passage;
// a long-press (touch) or a held Confirm asks to delete it. Same interaction
// as the bookmark list.
//
// EPUB (epub != null): opening finishes with a ProgressChangeResult carrying
// the spine and visible-text offset. TXT: a TxtOffsetResult with the source
// byte offset. Deletions are saved here; the reader reloads its copy when the
// list returns.
class HighlightListActivity final : public UiListActivity {
  std::shared_ptr<Epub> epub;
  std::string bookPath;
  std::vector<HighlightEntry> highlights;
  std::vector<std::string> rowSubtitles;
  std::vector<freeink::ui::ListItem> rowItems;
  bool confirmingDelete = false;
  OptionPopup confirmPopup;

  void rebuildRowItems();
  void openSelected();
  void showDeleteConfirmation();
  void deleteSelected();

 public:
  HighlightListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                        std::string bookPath);
  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(highlights.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
};
