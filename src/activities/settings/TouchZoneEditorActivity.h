#pragma once

#include <cstdint>

#include "TouchZones.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

// Full-screen editor for the reader's custom tap zones: the 3x3 grid is drawn
// over the whole screen (the same geometry the reader hit-tests), each cell
// labelled with its action. Tap a cell (or move with the buttons and press
// Confirm) to pick its action; Back saves. Saving any change switches the
// touch mode to Custom so the edit takes effect right away.
class TouchZoneEditorActivity final : public Activity {
 public:
  explicit TouchZoneEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TouchZoneEditor", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void openPicker();
  void saveAndFinish();

  uint8_t actions[touchZones::kZoneCount] = {};
  int selected = 4;  // centre cell
  bool changed = false;
  OptionPopup picker;
};
