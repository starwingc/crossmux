#include "PowerMenuActivity.h"

#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include "SilentRestart.h"
#include "activities/ActivityManager.h"

namespace {
enum PowerOption : int { OPT_SLEEP = 0, OPT_RESTART = 1, OPT_CANCEL = 2 };
}

void PowerMenuActivity::onEnter() {
  Activity::onEnter();
  const StrId options[] = {StrId::STR_SLEEP, StrId::STR_RESTART_DEVICE, StrId::STR_CANCEL};
  popup.show(StrId::STR_POWER_MENU, options, static_cast<int>(std::size(options)), OPT_SLEEP, [this](int idx) {
    finish();
    switch (idx) {
      case OPT_SLEEP:
        activityManager.requestSleep();
        break;
      case OPT_RESTART:
        LOG_INF("PWR", "Restart from power menu");
        silentRestart();
        break;
      default:
        break;
    }
  });
  requestUpdate();
}

void PowerMenuActivity::loop() {
  if (popup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  // Dismissed without a choice (Back, or a tap outside the dialog).
  finish();
}

void PowerMenuActivity::render(RenderLock&&) {
  // Drawn over the screen underneath, like the control center; no clear.
  if (popup.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
