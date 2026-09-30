#pragma once

// Hold the power button for FORCE_RESTART_HOLD_MS to reboot, even while the
// main loop is stuck. Runs in its own high-priority task that only samples the
// power pin, so a hung activity, render or network call cannot block it.
namespace forceRestart {
// Call once after gpio.begin() has configured the power pin.
void start();
}  // namespace forceRestart
