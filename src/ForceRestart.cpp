#include "ForceRestart.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <Logging.h>

namespace forceRestart {
#ifndef SIMULATOR
namespace {
// Well past the normal hold-to-sleep, so the regular power gestures never reach it.
constexpr unsigned long FORCE_RESTART_HOLD_MS = 10000;
constexpr TickType_t POLL_TICKS = pdMS_TO_TICKS(100);

bool powerPressed(const int8_t pin, const bool activeHigh) { return (digitalRead(pin) == HIGH) == activeHigh; }

void watchTask(void*) {
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  // The button that woke the device may still be down; count only presses that
  // start after a release, so a long wake hold can't loop into restarts.
  bool armed = false;
  TickType_t pressedAt = 0;
  bool pressed = false;
  for (;;) {
    vTaskDelay(POLL_TICKS);
    const bool down = powerPressed(pin, activeHigh);
    if (!down) {
      armed = true;
      pressed = false;
      continue;
    }
    if (!armed) continue;
    const TickType_t now = xTaskGetTickCount();
    if (!pressed) {
      pressed = true;
      pressedAt = now;
      continue;
    }
    if (now - pressedAt >= pdMS_TO_TICKS(FORCE_RESTART_HOLD_MS)) {
      LOG_ERR("MAIN", "Power held %lu ms: forcing restart", FORCE_RESTART_HOLD_MS);
      delay(50);  // let the log line drain
      esp_restart();
    }
  }
}
}  // namespace
#endif

void start() {
#ifndef SIMULATOR
  if (BoardConfig::ACTIVE.input.power < 0) return;
  // Highest priority so a busy lower-priority task (the main loop included)
  // can't starve it; it sleeps between samples and costs almost nothing.
  xTaskCreate(watchTask, "forceRestart", 2048, nullptr, configMAX_PRIORITIES - 1, nullptr);
#endif
}
}  // namespace forceRestart
