#pragma once

#include <Arduino.h>

// Pinout — matches reference-implementation/.../servo_controller.h.
// LED pin constants live in leds.h.
#define PIN_BELL_0_PWM        3
#define PIN_BELL_1_PWM        4
#define PIN_BELL_POWER        6

// Set to 0 if the power gate is active-low (P-channel high-side, etc.)
#define BELL_POWER_ACTIVE_HIGH 1

// PWM
#define SERVO_FREQ_HZ         50
#define SERVO_RESOLUTION_BITS 14
#define SERVO_PERIOD_US       20000
#define SERVO_MIN_US          600
#define SERVO_MAX_US          2400
#define SERVO_MID_DEG         90

// Intensity is the angular amplitude in degrees. Capped at 50 to keep the
// servo well within its safe range and avoid mechanical stress.
#define BELL_INTENSITY_MIN    5
#define BELL_INTENSITY_MAX    50

// Production defaults from reference-implementation config_manager_get_defaults.
#define DEFAULT_CAPACITOR_STABILIZATION_MS 222
#define DEFAULT_RELEASE_PAUSE_MS           500
#define DEFAULT_STEP_RETURN_DELAY_MS       5
#define DEFAULT_TOTAL_RING_DELAY_MS        4000
#define DEFAULT_BELL_INTENSITY             20
#define DEFAULT_BELL_COUNT                 1

// Time the PWM signal runs into a powered-down servo line, just to settle
// the pulse train at mid before we energize the rail. Without this, the
// servo can read a partial pulse on power-on and jump.
#define PWM_WARMUP_MS                      100

// Time we hold mid PWM with the rail powered before the first servo motion
// — lets the rail capacitor charge fully and both servos physically reach
// 90°. Avoids brownouts caused by inrush + immediate strike load.
#define RAIL_WARMUP_MS                     1000

// Time we keep the rail powered after the last ring before scheduling a
// MOSFET-off. Multiple rings within this window share a single warm-up.
#define RAIL_IDLE_HOLD_MS                  1500

struct ServoTimings {
  uint16_t capacitor_stabilization_ms;
  uint16_t release_pause_ms;
  uint16_t step_return_delay_ms;
  uint16_t total_ring_delay_ms;
};

ServoTimings servoDefaultTimings();

void servoInit();

// Pump the deferred MOSFET-off scheduled by the last ringBell. Call from loop().
void servoTick();

// Ring `bell` (0 or 1) `count` times with angular amplitude `intensity` (5-60).
// Blocks for several seconds. Uses default timings.
void ringBell(uint8_t bell, uint8_t count, uint8_t intensity);

void ringBell(uint8_t bell, uint8_t count, uint8_t intensity, const ServoTimings &t);

// Diagnostics — for M1 validation only.
void servoPowerToggle();   // Toggle MOSFET / blue LED without ringing.
void servoSweep(uint8_t bell);  // Sweep one servo through 60-120-90 without striking.

// Calibration: power the rail and hold both servos at their configured
// center positions for 10 s, so the user can verify alignment and tweak
// the per-bell center sliders live. Each interaction (button press, center
// slider change) resets the 10 s window.
void servoEnterCalibration();

// If the rail is currently powered, snap `bell` to its newly configured
// center and extend the calibration timer. No-op otherwise.
void servoApplyCenterIfPowered(uint8_t bell);

