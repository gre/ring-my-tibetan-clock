#include "servo.h"

#include "config.h"

namespace {

constexpr uint32_t kCalibrationHoldMs = 10000;


bool g_initialized = false;
bool g_rail_powered = false;

// Timestamp at which the rail will be powered off, or 0 if no off is pending.
// Set by ringBell()'s tail; cleared by ensurePowered() when a new ring arrives
// inside the hold window, by servoTick() once the off fires, or by force-off.
uint32_t g_rail_off_at_ms = 0;

inline uint8_t bellPin(uint8_t bell) {
  return bell == 0 ? PIN_BELL_0_PWM : PIN_BELL_1_PWM;
}

void bellPower(bool on) {
#if BELL_POWER_ACTIVE_HIGH
  digitalWrite(PIN_BELL_POWER, on ? HIGH : LOW);
#else
  digitalWrite(PIN_BELL_POWER, on ? LOW : HIGH);
#endif
  if (on != g_rail_powered) {
    g_rail_powered = on;
    Serial.printf("[servo] MOSFET %s (GPIO%d) — blue LED should %s\n",
                  on ? "ON" : "OFF", PIN_BELL_POWER, on ? "light" : "go dark");
  }
}

void setPulseUs(uint8_t pin, uint32_t us) {
  const uint32_t max_duty = (1u << SERVO_RESOLUTION_BITS) - 1;
  uint32_t duty = (us * (max_duty + 1u)) / SERVO_PERIOD_US;
  if (duty > max_duty) duty = max_duty;
  ledcWrite(pin, duty);
}

void setAngle(uint8_t pin, int32_t angle_deg) {
  if (angle_deg < 0) angle_deg = 0;
  if (angle_deg > 180) angle_deg = 180;
  uint32_t us = SERVO_MIN_US +
                ((SERVO_MAX_US - SERVO_MIN_US) * (uint32_t)angle_deg) / 180u;
  setPulseUs(pin, us);
}

// Walk the servo from `from` to `to` one degree at a time, sleeping
// `step_ms` between each step. step_ms == 0 is a single-write snap.
void swingTo(uint8_t pin, int32_t from, int32_t to, uint8_t step_ms) {
  if (step_ms == 0 || from == to) {
    setAngle(pin, to);
    return;
  }
  int32_t direction = (to > from) ? 1 : -1;
  for (int32_t pos = from + direction;; pos += direction) {
    setAngle(pin, pos);
    delay(step_ms);
    if (pos == to) break;
  }
}

// Bring the rail up if it isn't already; cancel any pending power-off.
// Cold-start uses a staggered warmup so we don't brown out the ESP under
// the combined inrush of two MG90S servos seeking 90° at the same time.
//   1. Pre-arm only the targeted servo's PWM line at mid.
//   2. Energize the rail; the targeted servo wakes up and seeks 90°.
//   3. Hold for RAIL_WARMUP_MS so the cap fully charges with a single
//      servo loaded.
//   4. Bring the other servo online at mid — cap is stable now, smaller
//      current spike.
void ensurePowered(uint8_t target_bell) {
  if (g_rail_powered) {
    g_rail_off_at_ms = 0;
    return;
  }
  const auto &cfg = configGet();
  uint8_t target_pin = bellPin(target_bell);
  uint8_t other_bell = (target_bell == 0) ? 1 : 0;
  uint8_t other_pin = bellPin(other_bell);

  setAngle(target_pin, cfg.center_deg[target_bell]);
  delay(PWM_WARMUP_MS);

  bellPower(true);
  delay(RAIL_WARMUP_MS);

  setAngle(other_pin, cfg.center_deg[other_bell]);
  delay(500);

  g_rail_off_at_ms = 0;
}

void schedulePowerOff() {
  g_rail_off_at_ms = millis() + RAIL_IDLE_HOLD_MS;
}

// Power-off now, regardless of any scheduled time. For boot-centering and
// for diagnostic commands that need explicit control.
void forcePowerOff() {
  if (!g_rail_powered) {
    g_rail_off_at_ms = 0;
    return;
  }
  bellPower(false);
  g_rail_off_at_ms = 0;
  delay(DEFAULT_CAPACITOR_STABILIZATION_MS);
}

}  // namespace

ServoTimings servoDefaultTimings() {
  return ServoTimings{
      DEFAULT_CAPACITOR_STABILIZATION_MS,
      DEFAULT_RELEASE_PAUSE_MS,
  };
}

void servoInit() {
  if (g_initialized) return;

  pinMode(PIN_BELL_POWER, OUTPUT);
  bellPower(false);

  bool ok0 = ledcAttach(PIN_BELL_0_PWM, SERVO_FREQ_HZ, SERVO_RESOLUTION_BITS);
  bool ok1 = ledcAttach(PIN_BELL_1_PWM, SERVO_FREQ_HZ, SERVO_RESOLUTION_BITS);
  Serial.printf(
      "[servo] init: bell0(GPIO%d) ledc=%s, bell1(GPIO%d) ledc=%s, "
      "power=GPIO%d\n",
      PIN_BELL_0_PWM, ok0 ? "ok" : "FAIL", PIN_BELL_1_PWM, ok1 ? "ok" : "FAIL",
      PIN_BELL_POWER);

  g_initialized = true;
}

void servoTick() {
  if (g_rail_powered && g_rail_off_at_ms != 0 &&
      (int32_t)(millis() - g_rail_off_at_ms) >= 0) {
    Serial.println("[servo] rail idle hold expired — powering off");
    bellPower(false);
    g_rail_off_at_ms = 0;
  }
}

void servoEnterCalibration() {
  if (!g_initialized) servoInit();
  Serial.println("[servo] calibration mode — rail on, hold 10s after last interaction");
  ensurePowered(0);
  // ensurePowered already applied per-bell centers from config.
  g_rail_off_at_ms = millis() + kCalibrationHoldMs;
}

void servoApplyCenterIfPowered(uint8_t bell) {
  if (bell > 1) return;
  if (!g_rail_powered) return;
  const auto &cfg = configGet();
  setAngle(bellPin(bell), cfg.center_deg[bell]);
  g_rail_off_at_ms = millis() + kCalibrationHoldMs;
  Serial.printf("[servo] live center update bell %u -> %u deg\n", bell,
                cfg.center_deg[bell]);
}

void servoPowerToggle() {
  if (!g_initialized) servoInit();
  if (g_rail_powered) {
    forcePowerOff();
  } else {
    bellPower(true);
    g_rail_off_at_ms = 0;
  }
}

void servoSweep(uint8_t bell) {
  if (!g_initialized) servoInit();
  if (bell > 1) {
    Serial.printf("[servo] invalid bell %u for sweep\n", bell);
    return;
  }
  Serial.printf("[sweep] bell %u: powering and sweeping 60-120-90\n", bell);
  uint8_t pin = bellPin(bell);
  ensurePowered(bell);
  setAngle(pin, 60);
  delay(500);
  setAngle(pin, 120);
  delay(500);
  setAngle(pin, 90);
  delay(500);
  schedulePowerOff();
  Serial.printf("[sweep] bell %u done\n", bell);
}

void ringBell(uint8_t bell, uint8_t count, uint8_t intensity) {
  ringBell(bell, count, intensity, servoDefaultTimings());
}

void ringBell(uint8_t bell, uint8_t count, uint8_t intensity,
              const ServoTimings &t) {
  if (!g_initialized) {
    Serial.println("[servo] not initialized");
    return;
  }
  if (bell > 1) {
    Serial.printf("[servo] invalid bell %u\n", bell);
    return;
  }
  if (count == 0) count = 1;

  if (intensity < BELL_INTENSITY_MIN) intensity = BELL_INTENSITY_MIN;
  if (intensity > BELL_INTENSITY_MAX) intensity = BELL_INTENSITY_MAX;

  const auto &cfg = configGet();
  const uint8_t pin = bellPin(bell);
  const int32_t amplitude = intensity;
  const int32_t mid = cfg.center_deg[bell];
  const int32_t other_mid = cfg.center_deg[bell == 0 ? 1 : 0];

  Serial.printf("[ring] bell=%u count=%u intensity=%u (pin=GPIO%d)\n", bell,
                count, intensity, pin);

  // 1. Bring the rail up (staggered warmup, target servo first) if it's not
  //    already on. If a previous ring is still inside its idle-hold window,
  //    this is a cheap no-op and we skip the warmup entirely — back-to-back
  //    rings stay on a single power cycle.
  ensurePowered(bell);

  // 2. Re-issue mid in case the targeted servo drifted between rings.
  setAngle(pin, mid);
  delay(t.capacitor_stabilization_ms);

  // "Up"   = wind-up phase (arm rotates AWAY from the bowl, mid -> mid+amp).
  //          Slow by default — the bell is being "armed" before the release.
  // "Down" = release phase (arm snaps BACK toward neutral, mid+amp -> mid).
  //          Snap by default — this is the motion that strikes the bowl.
  const uint8_t up_step = cfg.swing_up_step_ms;
  const uint8_t down_step = cfg.swing_down_step_ms;

  for (uint8_t strike = 0; strike < count; strike++) {
    Serial.printf("[ring]   strike %u/%u: wind-up to %d deg\n", strike + 1,
                  count, (int)(mid + amplitude));
    swingTo(pin, mid, mid + amplitude, up_step);
    delay(t.release_pause_ms);

    Serial.printf("[ring]   release: %s back to %d deg\n",
                  down_step == 0 ? "snap" : "ramp", (int)mid);
    swingTo(pin, mid + amplitude, mid, down_step);

    if (strike + 1 < count) {
      uint32_t used = (uint32_t)t.release_pause_ms +
                      (uint32_t)amplitude * (up_step + down_step);
      if (cfg.cycle_ms > used) {
        delay(cfg.cycle_ms - used);
      }
    }
  }

  // 3. Re-center BOTH servos before scheduling power-off. The un-commanded
  //    bell may have drifted under brownout pressure during the strike.
  //    Each servo goes to its own per-bell center.
  setAngle(bellPin(0), cfg.center_deg[0]);
  setAngle(bellPin(1), cfg.center_deg[1]);
  delay(t.capacitor_stabilization_ms);

  // 4. Defer the actual MOSFET drop to servoTick(). Another ring arriving
  //    inside RAIL_IDLE_HOLD_MS will cancel this and reuse the warm rail.
  schedulePowerOff();

  Serial.printf("[ring] bell %u done (rail off in %u ms unless re-rung)\n",
                bell, RAIL_IDLE_HOLD_MS);
}
