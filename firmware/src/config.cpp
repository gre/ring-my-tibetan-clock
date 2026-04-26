#include "config.h"

#include <Preferences.h>

#include "servo.h"

namespace {
RuntimeConfig g_config;
Preferences g_prefs;

// Bumped to 8 — swing semantics fixed (Up = wind-up Phase A, Down = release
// Phase B that rings the bowl), per-bell defaults tuned to the user's
// physical setup, ranges retuned (Up extended to 30 ms for slower wind-up,
// Down capped at 10 ms since it's mostly used at 0).
constexpr uint16_t kSchemaVersion = 8;

constexpr uint8_t kCountMin = 1;
constexpr uint8_t kCountMax = 9;
constexpr uint8_t kCenterMin = 60;
constexpr uint8_t kCenterMax = 120;
constexpr uint8_t kSwingUpMin = 0;
constexpr uint8_t kSwingUpMax = 30;
constexpr uint8_t kSwingDownMin = 0;
constexpr uint8_t kSwingDownMax = 10;
constexpr uint16_t kCycleMin = 500;
constexpr uint16_t kCycleMax = 10000;

constexpr uint8_t kSwingUpDefault = 20;    // slow wind-up (mid -> mid+amp)
constexpr uint8_t kSwingDownDefault = 0;   // BOOM snap-back rings the bowl
constexpr uint16_t kCycleDefault = 5000;

// Per-bell defaults (index 0 = bell A, 1 = bell B), tuned by greweb on the
// physical device.
constexpr uint8_t kIntensityDefault[2] = {28, 20};
constexpr uint8_t kCountDefault[2] = {1, 3};
constexpr uint8_t kCenterDefault[2] = {90, 85};

const char *intensityKey(uint8_t bell) {
  return bell == 0 ? "int_a" : "int_b";
}
const char *countKey(uint8_t bell) {
  return bell == 0 ? "cnt_a" : "cnt_b";
}
const char *centerKey(uint8_t bell) {
  return bell == 0 ? "ctr_a" : "ctr_b";
}
}  // namespace

void configBegin() {
  g_prefs.begin("tibetan", false);
  uint16_t v = g_prefs.getUShort("ver", 0);

  if (v != kSchemaVersion) {
    for (uint8_t b = 0; b < 2; b++) {
      g_config.intensity[b] = kIntensityDefault[b];
      g_config.count[b] = kCountDefault[b];
      g_config.center_deg[b] = kCenterDefault[b];
      g_prefs.putUChar(intensityKey(b), g_config.intensity[b]);
      g_prefs.putUChar(countKey(b), g_config.count[b]);
      g_prefs.putUChar(centerKey(b), g_config.center_deg[b]);
    }
    g_config.swing_up_step_ms = kSwingUpDefault;
    g_config.swing_down_step_ms = kSwingDownDefault;
    g_config.cycle_ms = kCycleDefault;
    g_prefs.putUChar("sup", g_config.swing_up_step_ms);
    g_prefs.putUChar("sdn", g_config.swing_down_step_ms);
    g_prefs.putUShort("cyc", g_config.cycle_ms);
    g_prefs.putUShort("ver", kSchemaVersion);
    Serial.printf(
        "[config] fresh NVS — A(int=%u count=%u ctr=%u) B(int=%u count=%u ctr=%u) swing=%u/%u cycle=%u\n",
        g_config.intensity[0], g_config.count[0], g_config.center_deg[0],
        g_config.intensity[1], g_config.count[1], g_config.center_deg[1],
        g_config.swing_up_step_ms, g_config.swing_down_step_ms,
        g_config.cycle_ms);
  } else {
    for (uint8_t b = 0; b < 2; b++) {
      uint8_t i_loaded = g_prefs.getUChar(intensityKey(b), kIntensityDefault[b]);
      uint8_t c_loaded = g_prefs.getUChar(countKey(b), kCountDefault[b]);
      uint8_t ctr_loaded = g_prefs.getUChar(centerKey(b), kCenterDefault[b]);
      uint8_t i = i_loaded, c = c_loaded, ctr = ctr_loaded;
      // Clamp on load — bounds may have shrunk since the value was saved.
      if (i < BELL_INTENSITY_MIN) i = BELL_INTENSITY_MIN;
      if (i > BELL_INTENSITY_MAX) i = BELL_INTENSITY_MAX;
      if (c < kCountMin) c = kCountMin;
      if (c > kCountMax) c = kCountMax;
      if (ctr < kCenterMin) ctr = kCenterMin;
      if (ctr > kCenterMax) ctr = kCenterMax;
      g_config.intensity[b] = i;
      g_config.count[b] = c;
      g_config.center_deg[b] = ctr;
      // Only re-write when clamping actually shrunk the stored value —
      // avoids a flash erase/write cycle on every boot.
      if (i != i_loaded) g_prefs.putUChar(intensityKey(b), i);
      if (c != c_loaded) g_prefs.putUChar(countKey(b), c);
      if (ctr != ctr_loaded) g_prefs.putUChar(centerKey(b), ctr);
    }
    uint8_t sup_loaded = g_prefs.getUChar("sup", kSwingUpDefault);
    uint8_t sdn_loaded = g_prefs.getUChar("sdn", kSwingDownDefault);
    uint16_t cyc_loaded = g_prefs.getUShort("cyc", kCycleDefault);
    uint8_t sup = sup_loaded, sdn = sdn_loaded;
    uint16_t cyc = cyc_loaded;
    if (sup > kSwingUpMax) sup = kSwingUpMax;
    if (sdn > kSwingDownMax) sdn = kSwingDownMax;
    if (cyc < kCycleMin) cyc = kCycleMin;
    if (cyc > kCycleMax) cyc = kCycleMax;
    g_config.swing_up_step_ms = sup;
    g_config.swing_down_step_ms = sdn;
    g_config.cycle_ms = cyc;
    if (sup != sup_loaded) g_prefs.putUChar("sup", sup);
    if (sdn != sdn_loaded) g_prefs.putUChar("sdn", sdn);
    if (cyc != cyc_loaded) g_prefs.putUShort("cyc", cyc);
    Serial.printf(
        "[config] loaded: A(int=%u count=%u ctr=%u) B(int=%u count=%u ctr=%u) swing=%u/%u cycle=%u\n",
        g_config.intensity[0], g_config.count[0], g_config.center_deg[0],
        g_config.intensity[1], g_config.count[1], g_config.center_deg[1],
        g_config.swing_up_step_ms, g_config.swing_down_step_ms,
        g_config.cycle_ms);
  }
}

const RuntimeConfig &configGet() { return g_config; }

bool configSetIntensity(uint8_t bell, uint8_t v) {
  if (bell > 1) return false;
  if (v < BELL_INTENSITY_MIN) v = BELL_INTENSITY_MIN;
  if (v > BELL_INTENSITY_MAX) v = BELL_INTENSITY_MAX;
  if (g_config.intensity[bell] == v) return false;
  g_config.intensity[bell] = v;
  g_prefs.putUChar(intensityKey(bell), v);
  Serial.printf("[config] bell %u intensity = %u (saved)\n", bell, v);
  return true;
}

bool configSetCount(uint8_t bell, uint8_t v) {
  if (bell > 1) return false;
  if (v < kCountMin) v = kCountMin;
  if (v > kCountMax) v = kCountMax;
  if (g_config.count[bell] == v) return false;
  g_config.count[bell] = v;
  g_prefs.putUChar(countKey(bell), v);
  Serial.printf("[config] bell %u count = %u (saved)\n", bell, v);
  return true;
}

bool configSetCenterDeg(uint8_t bell, uint8_t v) {
  if (bell > 1) return false;
  if (v < kCenterMin) v = kCenterMin;
  if (v > kCenterMax) v = kCenterMax;
  if (g_config.center_deg[bell] == v) return false;
  g_config.center_deg[bell] = v;
  g_prefs.putUChar(centerKey(bell), v);
  Serial.printf("[config] bell %u center = %u (saved)\n", bell, v);
  return true;
}

bool configSetSwingUpMs(uint8_t v) {
  if (v > kSwingUpMax) v = kSwingUpMax;
  if (g_config.swing_up_step_ms == v) return false;
  g_config.swing_up_step_ms = v;
  g_prefs.putUChar("sup", v);
  Serial.printf("[config] swing_up_step = %u ms (saved)\n", v);
  return true;
}

bool configSetSwingDownMs(uint8_t v) {
  if (v > kSwingDownMax) v = kSwingDownMax;
  if (g_config.swing_down_step_ms == v) return false;
  g_config.swing_down_step_ms = v;
  g_prefs.putUChar("sdn", v);
  Serial.printf("[config] swing_down_step = %u ms (saved)\n", v);
  return true;
}

bool configSetCycleMs(uint16_t v) {
  if (v < kCycleMin) v = kCycleMin;
  if (v > kCycleMax) v = kCycleMax;
  if (g_config.cycle_ms == v) return false;
  g_config.cycle_ms = v;
  g_prefs.putUShort("cyc", v);
  Serial.printf("[config] cycle = %u ms (saved)\n", v);
  return true;
}
