#pragma once

#include <Arduino.h>
#include <Imu.h>

// TODO: Move enums into new header and share with CrossPointSettings.h
namespace CrossPointOrientation {
enum Value : uint8_t { PORTRAIT = 0, LANDSCAPE_CW = 1, INVERTED = 2, LANDSCAPE_CCW = 3 };
}

namespace CrossPointTiltPageTurn {
enum Value : uint8_t { TILT_OFF = 0, TILT_NORMAL = 1, TILT_INVERTED = 2 };
}

namespace CrossPointTapPageTurn {
// SINGLE_NEXT: single tap = next page, double tap = previous page.
// DOUBLE_NEXT: double tap = next page, single taps ignored (fewer accidental turns).
enum Value : uint8_t { TAP_OFF = 0, TAP_SINGLE_NEXT = 1, TAP_DOUBLE_NEXT = 2 };
}  // namespace CrossPointTapPageTurn

class HalTiltSensor;
extern HalTiltSensor halTiltSensor;  // Singleton

class HalTiltSensor {
  bool _available = false;
  mutable Imu _sdkImu;

  // Tilt gesture state machine
  bool _tiltForwardEvent = false;   // Consumed by wasTiltedForward()
  bool _tiltBackEvent = false;      // Consumed by wasTiltedBack()
  bool _tapNextEvent = false;       // Consumed by wasTappedNext()
  bool _tapPrevEvent = false;       // Consumed by wasTappedPrev()
  uint8_t _tapMode = 0;             // Tap mode the IMU is currently configured for
  bool _tapGyro = false;            // Gyro kept running alongside the tap engine (tilt also on)
  bool _tapLatched = false;         // STATUS1 tap bit seen on the previous poll
  bool _singleTapEvent = false;     // Consumed by wasSingleTap() (menu navigation)
  bool _doubleTapEvent = false;     // Consumed by wasDoubleTap() (menu navigation)
  unsigned long _lastButtonMs = 0;  // Last physical button edge; its jolt must not count as a tap
  bool _hadActivity = false;        // Non-consuming flag for sleep timer
  bool _inTilt = false;             // Currently tilted past threshold
  bool _isAwake = false;            // Tracks power state
  unsigned long _initMs = 0;        // Timestamp of sensor init
  unsigned long _lastTiltMs = 0;    // Debounce / cooldown
  unsigned long _wakeMs = 0;        // Timestamp of last wake() for stabilization

  // Tuning constants
  static constexpr float RATE_THRESHOLD_DPS = 270.0f;      // Deg/sec speed to trigger flick
  static constexpr float NEUTRAL_RATE_DPS = 50.0f;         // Must stop moving below this rate before next trigger
  static constexpr unsigned long COOLDOWN_MS = 600;        // Minimum ms between triggers
  static constexpr unsigned long POLL_INTERVAL_MS = 50;    // 20 Hz polling
  static constexpr unsigned long WAKE_STABILIZE_MS = 300;  // Ignore readings after wake
  // A button press shakes the case like a tap, and the engine reports a single
  // tap only after its double-tap window (~450 ms), so the guard spans both.
  static constexpr unsigned long BUTTON_TAP_GUARD_MS = 800;
  // Starting the engine reports one spurious tap ~0.5 s later; drop that window.
  static constexpr unsigned long TAP_SETTLE_MS = 1000;
  static constexpr uint8_t TAP_SETUP_MAX_ATTEMPTS = 3;
  static constexpr unsigned long TAP_SETUP_RETRY_MS = 2000;

  mutable unsigned long _lastPollMs = 0;

  bool readGyro(float& gx, float& gy, float& gz) const;

  // QMI8658 hardware tap engine (X3). Samples at ~448 Hz while enabled and
  // latches single/double taps in TAP_STATUS, so a busy main loop can't miss one.
  uint8_t _qmiAddr = 0;
  uint8_t _tapSetupFailures = 0;  // Consecutive failed tap-engine setups (reset on sleep)
  unsigned long _tapSetupFailMs = 0;
  unsigned long _tapArmedMs = 0;  // When the engine was last (re)programmed
  bool configureTap(bool enable, bool gyro);
  void pollTap(uint8_t tapMode);
  void dispatchTap(bool isDouble, uint8_t tapMode);

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // Enables tilt polling state
  bool wake();

  // Puts tilt polling state to sleep
  bool deepSleep();

  // True if an IMU is present on this device
  bool isAvailable() const { return _available; }

  // Poll the accelerometer and update tilt gesture state.
  // tapInMenus keeps the tap engine running outside the reader for menu navigation.
  void update(const uint8_t mode, const uint8_t orientation, const bool inReader, const uint8_t tapMode,
              const bool tapInMenus);

  // Call on every physical button edge so the press itself isn't read as a tap.
  void noteButtonActivity() { _lastButtonMs = millis(); }

  // True when the IMU supports hardware tap detection (QMI8658).
  bool supportsTap() const { return _qmiAddr != 0; }

  // Returns true once per tilt-forward gesture (next page direction).
  // Consumed on read — subsequent calls return false until next gesture.
  bool wasTiltedForward();

  // Returns true once per tilt-back gesture (previous page direction).
  // Consumed on read.
  bool wasTiltedBack();

  // Tap gestures mapped to page directions per the tap mode. Consumed on read.
  bool wasTappedNext();
  bool wasTappedPrev();

  // Raw tap types outside the reader (menu navigation). Consumed on read.
  bool wasSingleTap();
  bool wasDoubleTap();

  // Non-consuming: true if any tilt activity occurred since last call.
  // Used to reset the auto-sleep inactivity timer.
  bool hadActivity();

  // Discard any pending tilt events (call when leaving reader or disabling tilt).
  void clearPendingEvents();
};
