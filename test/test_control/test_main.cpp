#include <Arduino.h>
#include <unity.h>
#include "control_logic.h"

using elementiq::HeaterDecision;
using elementiq::Mode;

void test_invalid_temperature_is_rejected() {
  TEST_ASSERT_FALSE(elementiq::temperatureValid(NAN, 5.0f, 110.0f));
  TEST_ASSERT_FALSE(elementiq::temperatureValid(111.0f, 5.0f, 110.0f));
  TEST_ASSERT_TRUE(elementiq::temperatureValid(55.0f, 5.0f, 110.0f));
}

void test_sensor_fault_latches_and_recovers() {
  TEST_ASSERT_FALSE(elementiq::updateSensorFault(false, false, 179999, 180000));
  TEST_ASSERT_TRUE(elementiq::updateSensorFault(false, false, 180000, 180000));
  TEST_ASSERT_TRUE(elementiq::updateSensorFault(true, false, 0, 180000));
  TEST_ASSERT_FALSE(elementiq::updateSensorFault(true, true, 0, 180000));
}

void test_thermostat_hysteresis_transitions() {
  HeaterDecision below = elementiq::evaluateThermostat(
      Mode::automatic, false, true, false, 49.0f, 55.0f, 5.0f, true);
  TEST_ASSERT_TRUE(below.requestedOn);
  TEST_ASSERT_FALSE(below.forceOff);

  HeaterDecision atThreshold = elementiq::evaluateThermostat(
      Mode::automatic, false, true, false, 50.0f, 55.0f, 5.0f, true);
  TEST_ASSERT_TRUE(atThreshold.requestedOn);
  TEST_ASSERT_FALSE(atThreshold.forceOff);

  HeaterDecision withinBand = elementiq::evaluateThermostat(
      Mode::automatic, true, true, false, 52.0f, 55.0f, 5.0f, true);
  TEST_ASSERT_TRUE(withinBand.requestedOn);

  HeaterDecision atTarget = elementiq::evaluateThermostat(
      Mode::automatic, true, true, false, 55.0f, 55.0f, 5.0f, true);
  TEST_ASSERT_FALSE(atTarget.requestedOn);
}

void test_fault_and_schedule_force_heater_off() {
  HeaterDecision invalid = elementiq::evaluateThermostat(
      Mode::boost, true, false, false, NAN, 65.0f, 5.0f, true);
  TEST_ASSERT_TRUE(invalid.forceOff);
  TEST_ASSERT_FALSE(invalid.requestedOn);

  HeaterDecision faulted = elementiq::evaluateThermostat(
      Mode::automatic, true, true, true, 40.0f, 55.0f, 5.0f, true);
  TEST_ASSERT_TRUE(faulted.forceOff);

  HeaterDecision outsideSchedule = elementiq::evaluateThermostat(
      Mode::automatic, true, true, false, 40.0f, 55.0f, 5.0f, false);
  TEST_ASSERT_TRUE(outsideSchedule.forceOff);

  HeaterDecision boostOutsideSchedule = elementiq::evaluateThermostat(
      Mode::boost, false, true, false, 40.0f, 65.0f, 5.0f, false);
  TEST_ASSERT_TRUE(boostOutsideSchedule.requestedOn);
}

void test_boost_returns_to_auto_at_target_or_timeout() {
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Mode::automatic),
      static_cast<uint8_t>(elementiq::advanceBoostMode(
          Mode::boost, true, 65.0f, 65.0f, 1000, 5400000)));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Mode::automatic),
      static_cast<uint8_t>(elementiq::advanceBoostMode(
          Mode::boost, false, NAN, 65.0f, 5400000, 5400000)));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Mode::boost),
      static_cast<uint8_t>(elementiq::advanceBoostMode(
          Mode::boost, true, 60.0f, 65.0f, 1000, 5400000)));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_invalid_temperature_is_rejected);
  RUN_TEST(test_sensor_fault_latches_and_recovers);
  RUN_TEST(test_thermostat_hysteresis_transitions);
  RUN_TEST(test_fault_and_schedule_force_heater_off);
  RUN_TEST(test_boost_returns_to_auto_at_target_or_timeout);
  UNITY_END();
}

void loop() {}
