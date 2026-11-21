#pragma once

#include <math.h>
#include <stdint.h>

namespace elementiq {

enum class Mode : uint8_t { off = 0, automatic, boost };

struct HeaterDecision {
  bool requestedOn;
  bool forceOff;
};

inline bool temperatureValid(float value, float minimum, float maximum) {
  return isfinite(value) && value >= minimum && value <= maximum;
}

inline bool updateSensorFault(bool faulted, bool sampleValid,
                              uint32_t invalidDurationMs, uint32_t faultDelayMs) {
  if (sampleValid) return false;
  return faulted || invalidDurationMs >= faultDelayMs;
}

inline Mode advanceBoostMode(Mode mode, bool sampleValid, float temperature,
                             float boostTemperature, uint32_t elapsedMs,
                             uint32_t safetyDurationMs) {
  if (mode != Mode::boost) return mode;
  if (elapsedMs >= safetyDurationMs ||
      (sampleValid && temperature >= boostTemperature))
    return Mode::automatic;
  return Mode::boost;
}

inline HeaterDecision evaluateThermostat(Mode mode, bool heaterOn,
                                         bool sampleValid, bool sensorFault,
                                         float temperature, float target,
                                         float hysteresis, bool scheduleAllowsHeat) {
  if (mode == Mode::off || !sampleValid || sensorFault ||
      (mode == Mode::automatic && !scheduleAllowsHeat))
    return {false, true};

  // Use an inclusive lower threshold so a sample exactly at the hysteresis
  // boundary still trips the heater on when the system is off.
  if (temperature <= target - hysteresis) return {true, false};
  if (temperature >= target) return {false, false};
  return {heaterOn, false};
}

}  // namespace elementiq
