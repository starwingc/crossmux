#include "HalTiltSensor.h"

#include <BoardConfig.h>
#include <Logging.h>

#if FREEINK_CAP_IMU && !defined(SIMULATOR)
#include <Wire.h>
#define HAL_TILT_HAS_TAP 1
#else
#define HAL_TILT_HAS_TAP 0
#endif

HalTiltSensor halTiltSensor;  // Singleton instance

#if HAL_TILT_HAS_TAP
namespace {
// QMI8658 registers used by the tap engine (see the QMI8658A datasheet).
constexpr uint8_t QMI_WHO_AM_I = 0x00;
constexpr uint8_t QMI_WHO_AM_I_VALUE = 0x05;
constexpr uint8_t QMI_CTRL2 = 0x03;  // accel full scale + ODR
constexpr uint8_t QMI_CTRL3 = 0x04;  // gyro full scale + ODR
constexpr uint8_t QMI_CTRL7 = 0x08;  // sensor enables
constexpr uint8_t QMI_CTRL8 = 0x09;  // motion engines
constexpr uint8_t QMI_CTRL9 = 0x0A;  // host command
constexpr uint8_t QMI_CAL1_L = 0x0B;
constexpr uint8_t QMI_STATUSINT = 0x2D;
constexpr uint8_t QMI_TAP_STATUS = 0x59;
constexpr uint8_t QMI_RESET = 0x60;
constexpr uint8_t QMI_RESET_RESULT = 0x4D;
constexpr uint8_t RESET_CMD = 0xB0;
constexpr uint8_t RESET_DONE = 0x80;

constexpr uint8_t CTRL2_FS2G_ODR117 = 0x06;    // Imu::begin() defaults
constexpr uint8_t CTRL3_512DPS_ODR117 = 0x56;  // Imu::begin() defaults
constexpr uint8_t CTRL2_FS2G_ODR448 = 0x04;    // tap needs a fast accel (~448 Hz in 6DOF mode)
constexpr uint8_t CTRL3_512DPS_ODR448 = 0x54;
constexpr uint8_t CTRL7_ACC = 0x01;
constexpr uint8_t CTRL7_ACC_GYRO = 0x03;
constexpr uint8_t CTRL8_HANDSHAKE_STATUSINT = 0x80;
constexpr uint8_t CTRL8_TAP_EN = 0x01;
constexpr uint8_t CMD_ACK = 0x00;
constexpr uint8_t CMD_CONFIGURE_TAP = 0x0C;
constexpr uint8_t STATUSINT_CMD_DONE = 0x80;
constexpr uint8_t STATUS1_TAP = 0x02;
constexpr uint8_t TAP_TYPE_MASK = 0x03;
constexpr uint8_t TAP_TYPE_SINGLE = 0x01;
constexpr uint8_t TAP_TYPE_DOUBLE = 0x02;

// Tap tuning, in samples at ~448 Hz and linear-acceleration magnitude (g^2 x 1000).
constexpr uint8_t TAP_PRIORITY_Z_X_Y = 0x04;   // taps land on the screen/back: Z first
constexpr uint8_t TAP_PEAK_WINDOW = 20;        // ~45 ms for the spike to settle
constexpr uint16_t TAP_WINDOW = 50;            // ~110 ms quiet before a second tap
constexpr uint16_t TAP_DOUBLE_WINDOW = 200;    // ~450 ms for the second tap
constexpr uint16_t TAP_PEAK_THRESHOLD = 600;   // 0.6 g^2
constexpr uint16_t TAP_QUIET_THRESHOLD = 300;  // 0.3 g^2
constexpr uint8_t TAP_ALPHA = 8;               // 0.0625 * 128
constexpr uint8_t TAP_GAMMA = 32;              // 0.25 * 128

bool qmiWrite(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool qmiWriteBlock(uint8_t addr, uint8_t reg, const uint8_t* data, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

bool qmiRead(uint8_t addr, uint8_t reg, uint8_t* dst, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, len, static_cast<uint8_t>(true)) < len) return false;
  for (uint8_t i = 0; i < len; ++i) dst[i] = Wire.read();
  return true;
}

bool qmiWaitCmdDone(uint8_t addr, bool done) {
  const unsigned long start = millis();
  while (millis() - start < 100) {
    uint8_t status = 0;
    if (qmiRead(addr, QMI_STATUSINT, &status, 1) && ((status & STATUSINT_CMD_DONE) != 0) == done) return true;
    delay(1);
  }
  return false;
}

// CTRL9 protocol: issue the command, wait for CmdDone, acknowledge, wait for it to clear.
// A CmdDone left over from an interrupted handshake is acknowledged first.
bool qmiCommand(uint8_t addr, uint8_t cmd) {
  uint8_t status = 0;
  if (!qmiRead(addr, QMI_STATUSINT, &status, 1)) return false;
  if ((status & STATUSINT_CMD_DONE) && !(qmiWrite(addr, QMI_CTRL9, CMD_ACK) && qmiWaitCmdDone(addr, false))) {
    return false;
  }
  return qmiWrite(addr, QMI_CTRL9, cmd) && qmiWaitCmdDone(addr, true) && qmiWrite(addr, QMI_CTRL9, CMD_ACK) &&
         qmiWaitCmdDone(addr, false);
}

// Soft reset. The IMU keeps power across ESP resets, so engine state and a
// half-finished CTRL9 handshake from the previous run would otherwise survive.
bool qmiSoftReset(uint8_t addr) {
  if (!qmiWrite(addr, QMI_RESET, RESET_CMD)) return false;
  const unsigned long start = millis();
  while (millis() - start < 100) {
    delay(5);
    uint8_t result = 0;
    if (qmiRead(addr, QMI_RESET_RESULT, &result, 1) && result == RESET_DONE) return true;
  }
  return false;
}
}  // namespace
#endif

bool HalTiltSensor::readGyro(float& gx, float& gy, float& gz) const {
  Imu::Sample sample;
  if (!_sdkImu.read(sample)) return false;
  gx = sample.gx;
  gy = sample.gy;
  gz = sample.gz;
  return true;
}

void HalTiltSensor::begin() {
  _available = _sdkImu.begin();
#if HAL_TILT_HAS_TAP
  if (_available && BoardConfig::ACTIVE.sensors.imuType == BoardConfig::ImuType::Qmi8658) {
    // Imu keeps its probed address private; repeat its 0x6B/0x6A probe.
    for (const uint8_t addr : {BoardConfig::ACTIVE.sensors.imuAddr, uint8_t{0x6A}, uint8_t{0x6B}}) {
      uint8_t who = 0;
      if (qmiRead(addr, QMI_WHO_AM_I, &who, 1) && who == QMI_WHO_AM_I_VALUE) {
        _qmiAddr = addr;
        break;
      }
    }
    if (_qmiAddr != 0) {
      if (!qmiSoftReset(_qmiAddr)) LOG_ERR("GYR", "IMU soft reset timed out");
      // The reset cleared Imu::begin()'s configuration; redo it.
      _available = _sdkImu.begin();
    }
  }
#endif
  if (_available) {
    _initMs = millis();
    _lastPollMs = millis();
    // begin() leaves the sensors sampling; stand them by until tilt page turn
    // actually wakes them, so a disabled IMU doesn't drain the battery.
    if (!_sdkImu.sleep()) {
      LOG_ERR("GYR", "IMU standby failed");
    }
    LOG_INF("GYR", "SDK IMU initialized");
    return;
  }
  LOG_ERR("GYR", "SDK IMU not found");
}

bool HalTiltSensor::wake() {
  if (!_available) {
    return false;
  }

  if (!_sdkImu.wake()) {
    LOG_ERR("GYR", "IMU wake failed");
    return false;
  }

  _lastPollMs = millis();
  _lastTiltMs = millis();
  _wakeMs = millis();
  _isAwake = true;
  return true;
}

bool HalTiltSensor::deepSleep() {
  if (!_available) {
    return false;
  }

  if (_tapMode != CrossPointTapPageTurn::TAP_OFF) {
    configureTap(false, false);
  }
  _tapSetupFailures = 0;

  if (!_sdkImu.sleep()) {
    LOG_ERR("GYR", "IMU sleep failed");
    return false;
  }

  clearPendingEvents();
  _inTilt = false;
  _isAwake = false;
  return true;
}

void HalTiltSensor::update(const uint8_t mode, const uint8_t orientation, const bool inReader, const uint8_t tapMode,
                           const bool tapInMenus) {
  if (!_available) {
    return;
  }

  const bool tiltOn = mode != CrossPointTiltPageTurn::TILT_OFF && inReader;
  const uint8_t tap = (inReader || tapInMenus) && supportsTap() ? tapMode : CrossPointTapPageTurn::TAP_OFF;
  const bool shouldBeAwake = tiltOn || tap != CrossPointTapPageTurn::TAP_OFF;
  if (shouldBeAwake && !_isAwake) {
    _isAwake = wake();
    return;
  }
  if (!shouldBeAwake && _isAwake) {
    _isAwake = !deepSleep();
    return;
  }

  if (!shouldBeAwake) {
    return;
  }

  // (Re)program the tap engine when tap turns on/off or tilt needs the gyro.
  const bool tapOn = tap != CrossPointTapPageTurn::TAP_OFF;
  const bool retryBlocked =
      tapOn && _tapSetupFailures > 0 &&
      (_tapSetupFailures >= TAP_SETUP_MAX_ATTEMPTS || millis() - _tapSetupFailMs < TAP_SETUP_RETRY_MS);
  if (!retryBlocked && (tapOn != (_tapMode != CrossPointTapPageTurn::TAP_OFF) || (tapOn && tiltOn != _tapGyro))) {
    configureTap(tapOn, tiltOn);
    _wakeMs = millis();
    return;
  }
  _tapMode = tap;

  const unsigned long now = millis();
  // Stabilization: discard readings during gyro startup transient
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }

  if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  if (tapOn) {
    pollTap(inReader ? tap : CrossPointTapPageTurn::TAP_OFF);
  }
  if (!tiltOn) {
    return;
  }

  float gx, gy, gz;
  if (!readGyro(gx, gy, gz)) {
    return;
  }

  // Map the gyro axis to left/right tilt based on reader orientation.
  // On the X3 PCB: X axis = left/right in portrait, Y axis = left/right in landscape.
  float tiltAxis;
  switch (orientation) {
    case CrossPointOrientation::PORTRAIT:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gx : gx;
      break;
    case CrossPointOrientation::INVERTED:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gx : -gx;
      break;
    case CrossPointOrientation::LANDSCAPE_CW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gy : -gy;
      break;
    case CrossPointOrientation::LANDSCAPE_CCW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gy : gy;
      break;
    default:
      tiltAxis = gx;
      break;
  }

  if (_inTilt) {
    // Wait for device to return to neutral before allowing next trigger
    if (fabsf(tiltAxis) < NEUTRAL_RATE_DPS) {
      _inTilt = false;
    }
  } else {
    // Check for new tilt gesture (with cooldown)
    if ((now - _lastTiltMs) >= COOLDOWN_MS) {
      if (tiltAxis > RATE_THRESHOLD_DPS) {
        _tiltForwardEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Forward Trigger=(%.1f) dps", tiltAxis);
      } else if (tiltAxis < -RATE_THRESHOLD_DPS) {
        _tiltBackEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Backward Trigger=(%.1f) dps", tiltAxis);
      }
    }
  }
}

bool HalTiltSensor::wasTiltedForward() {
  const bool val = _tiltForwardEvent;
  _tiltForwardEvent = false;
  return val;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool val = _tiltBackEvent;
  _tiltBackEvent = false;
  return val;
}

bool HalTiltSensor::configureTap(const bool enable, const bool gyro) {
#if HAL_TILT_HAS_TAP
  const uint8_t addr = _qmiAddr;
  if (addr == 0) return false;
  // The engine is programmed with the sensors stopped (CTRL7 = 0).
  const char* failed = nullptr;
  const auto step = [&failed](const char* name, const bool result) {
    if (!result && !failed) failed = name;
    return result;
  };
  bool ok = step("stop", qmiWrite(addr, QMI_CTRL7, 0) && qmiWrite(addr, QMI_CTRL8, CTRL8_HANDSHAKE_STATUSINT));
  if (enable) {
    const uint8_t page1[] = {TAP_PEAK_WINDOW,
                             TAP_PRIORITY_Z_X_Y,
                             static_cast<uint8_t>(TAP_WINDOW & 0xFF),
                             static_cast<uint8_t>(TAP_WINDOW >> 8),
                             static_cast<uint8_t>(TAP_DOUBLE_WINDOW & 0xFF),
                             static_cast<uint8_t>(TAP_DOUBLE_WINDOW >> 8),
                             0x00,
                             0x01};
    const uint8_t page2[] = {TAP_ALPHA,
                             TAP_GAMMA,
                             static_cast<uint8_t>(TAP_PEAK_THRESHOLD & 0xFF),
                             static_cast<uint8_t>(TAP_PEAK_THRESHOLD >> 8),
                             static_cast<uint8_t>(TAP_QUIET_THRESHOLD & 0xFF),
                             static_cast<uint8_t>(TAP_QUIET_THRESHOLD >> 8),
                             0x00,
                             0x02};
    ok = ok &&
         step("odr", qmiWrite(addr, QMI_CTRL2, CTRL2_FS2G_ODR448) && qmiWrite(addr, QMI_CTRL3, CTRL3_512DPS_ODR448)) &&
         step("page1", qmiWriteBlock(addr, QMI_CAL1_L, page1, sizeof(page1)) && qmiCommand(addr, CMD_CONFIGURE_TAP)) &&
         step("page2", qmiWriteBlock(addr, QMI_CAL1_L, page2, sizeof(page2)) && qmiCommand(addr, CMD_CONFIGURE_TAP)) &&
         step("enable", qmiWrite(addr, QMI_CTRL8, CTRL8_HANDSHAKE_STATUSINT | CTRL8_TAP_EN) &&
                            qmiWrite(addr, QMI_CTRL7, gyro ? CTRL7_ACC_GYRO : CTRL7_ACC));
  } else {
    ok = ok && step("restore", qmiWrite(addr, QMI_CTRL2, CTRL2_FS2G_ODR117) &&
                                   qmiWrite(addr, QMI_CTRL3, CTRL3_512DPS_ODR117) &&
                                   qmiWrite(addr, QMI_CTRL7, CTRL7_ACC_GYRO));
  }
  // A failed setup leaves the engine off; update() retries it a few times, spaced out.
  if (enable && !ok) {
    ++_tapSetupFailures;
    _tapSetupFailMs = millis();
  } else if (ok) {
    _tapSetupFailures = 0;
  }
  _tapArmedMs = millis();
  _tapMode = enable && ok ? CrossPointTapPageTurn::TAP_SINGLE_NEXT : CrossPointTapPageTurn::TAP_OFF;
  _tapGyro = gyro;
  _tapLatched = false;
  _tapNextEvent = false;
  _tapPrevEvent = false;
  _singleTapEvent = false;
  _doubleTapEvent = false;
  if (ok) {
    LOG_INF("GYR", "Tap engine %s (gyro %s): ok", enable ? "on" : "off", gyro ? "on" : "off");
  } else {
    LOG_ERR("GYR", "Tap engine %s failed at %s (attempt %u)", enable ? "setup" : "teardown", failed, _tapSetupFailures);
  }
  return ok;
#else
  (void)enable;
  (void)gyro;
  return false;
#endif
}

void HalTiltSensor::pollTap(const uint8_t tapMode) {
#if HAL_TILT_HAS_TAP
  // STATUSINT, STATUS0, STATUS1 in one burst; reading them releases the latch.
  uint8_t status[3] = {};
  if (!qmiRead(_qmiAddr, QMI_STATUSINT, status, sizeof(status))) return;
  const bool tapNow = (status[2] & STATUS1_TAP) != 0;
  const bool rising = tapNow && !_tapLatched;
  _tapLatched = tapNow;
  if (!rising) return;

  uint8_t tapStatus = 0;
  if (!qmiRead(_qmiAddr, QMI_TAP_STATUS, &tapStatus, 1)) return;
  const uint8_t type = tapStatus & TAP_TYPE_MASK;
  LOG_INF("GYR", "Tap status=0x%02X (%s)", tapStatus,
          type == TAP_TYPE_SINGLE   ? "single"
          : type == TAP_TYPE_DOUBLE ? "double"
                                    : "none");
  if (millis() - _tapArmedMs < TAP_SETTLE_MS) {
    LOG_INF("GYR", "Tap ignored: engine just started");
    return;
  }
  if (millis() - _lastButtonMs < BUTTON_TAP_GUARD_MS) {
    LOG_INF("GYR", "Tap ignored: follows a button press");
    return;
  }
  if (type != TAP_TYPE_SINGLE && type != TAP_TYPE_DOUBLE) return;
  dispatchTap(type == TAP_TYPE_DOUBLE, tapMode);
#else
  (void)tapMode;
#endif
}

void HalTiltSensor::dispatchTap(const bool isDouble, const uint8_t tapMode) {
  _hadActivity = true;
  // Outside the reader (TAP_OFF here) taps drive menu navigation instead of pages.
  if (tapMode == CrossPointTapPageTurn::TAP_OFF) {
    _singleTapEvent = _singleTapEvent || !isDouble;
    _doubleTapEvent = _doubleTapEvent || isDouble;
    return;
  }
  bool next = false;
  bool prev = false;
  if (tapMode == CrossPointTapPageTurn::TAP_SINGLE_NEXT) {
    next = !isDouble;
    prev = isDouble;
  } else if (tapMode == CrossPointTapPageTurn::TAP_DOUBLE_NEXT) {
    next = isDouble;
  }
  _tapNextEvent = _tapNextEvent || next;
  _tapPrevEvent = _tapPrevEvent || prev;
}

bool HalTiltSensor::wasTappedNext() {
  const bool val = _tapNextEvent;
  _tapNextEvent = false;
  return val;
}

bool HalTiltSensor::wasTappedPrev() {
  const bool val = _tapPrevEvent;
  _tapPrevEvent = false;
  return val;
}

bool HalTiltSensor::wasSingleTap() {
  const bool val = _singleTapEvent;
  _singleTapEvent = false;
  return val;
}

bool HalTiltSensor::wasDoubleTap() {
  const bool val = _doubleTapEvent;
  _doubleTapEvent = false;
  return val;
}

bool HalTiltSensor::hadActivity() {
  const bool val = _hadActivity;
  _hadActivity = false;
  return val;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _tapNextEvent = false;
  _tapPrevEvent = false;
  _singleTapEvent = false;
  _doubleTapEvent = false;
  _hadActivity = false;
  // Intentionally preserve _inTilt so a held tilt doesn't retrigger on next poll
}
