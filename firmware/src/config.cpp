#include "config.h"

#include <Preferences.h>

#include "servo.h"

namespace {
RuntimeConfig g_config;
Preferences g_prefs;

// Bumped to 3 when per-bell center trim was added.
constexpr uint16_t kSchemaVersion = 3;

constexpr uint8_t kCountMin = 1;
constexpr uint8_t kCountMax = 9;
constexpr uint8_t kCenterMin = 60;
constexpr uint8_t kCenterMax = 120;

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
      g_config.intensity[b] = DEFAULT_BELL_INTENSITY;
      g_config.count[b] = DEFAULT_BELL_COUNT;
      g_config.center_deg[b] = SERVO_MID_DEG;
      g_prefs.putUChar(intensityKey(b), g_config.intensity[b]);
      g_prefs.putUChar(countKey(b), g_config.count[b]);
      g_prefs.putUChar(centerKey(b), g_config.center_deg[b]);
    }
    g_prefs.putUShort("ver", kSchemaVersion);
    Serial.printf(
        "[config] fresh NVS — defaults intensity=%u count=%u center=%u (both bells)\n",
        DEFAULT_BELL_INTENSITY, DEFAULT_BELL_COUNT, SERVO_MID_DEG);
  } else {
    for (uint8_t b = 0; b < 2; b++) {
      uint8_t i_loaded = g_prefs.getUChar(intensityKey(b), DEFAULT_BELL_INTENSITY);
      uint8_t c_loaded = g_prefs.getUChar(countKey(b), DEFAULT_BELL_COUNT);
      uint8_t ctr_loaded = g_prefs.getUChar(centerKey(b), SERVO_MID_DEG);
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
    Serial.printf(
        "[config] loaded: A(int=%u count=%u ctr=%u) B(int=%u count=%u ctr=%u)\n",
        g_config.intensity[0], g_config.count[0], g_config.center_deg[0],
        g_config.intensity[1], g_config.count[1], g_config.center_deg[1]);
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
