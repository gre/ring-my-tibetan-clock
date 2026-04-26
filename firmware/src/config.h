#pragma once

#include <Arduino.h>

struct RuntimeConfig {
  uint8_t intensity[2];        // per-bell, indices 0 and 1
  uint8_t count[2];
  uint8_t center_deg[2];       // per-bell, 60..120, default 90 (mechanical mounting trim)
  uint8_t swing_up_step_ms;    // global. 0..30, wind-up phase (mid -> mid+amp).
  uint8_t swing_down_step_ms;  // global. 0..10, release phase (mid+amp -> mid). 0 = snap = ringing strike.
  uint16_t cycle_ms;           // global. 500..10000, total time per strike cycle in a multi-strike ring
};

void configBegin();

const RuntimeConfig &configGet();

// Setters clamp, persist to NVS, and return true if the value changed.
// `bell` must be 0 or 1.
bool configSetIntensity(uint8_t bell, uint8_t v);
bool configSetCount(uint8_t bell, uint8_t v);
bool configSetCenterDeg(uint8_t bell, uint8_t v);
bool configSetSwingUpMs(uint8_t v);
bool configSetSwingDownMs(uint8_t v);
bool configSetCycleMs(uint16_t v);
