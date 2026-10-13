#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// Power options (sleep / restart / cancel) drawn over the current screen,
// opened by the Home-key hold "Power Menu" quick action.
class PowerMenuActivity final : public Activity {
  OptionPopup popup;

 public:
  PowerMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PowerMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
