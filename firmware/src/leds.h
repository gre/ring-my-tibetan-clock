#pragma once

#include <Arduino.h>

#define PIN_LED_GREEN  0
#define PIN_LED_RED    1

// Centralized status LEDs so wifi/mqtt layers don't fight for control of the
// green/red pins. Safe to call from anywhere; ledsTick() owns the actual
// digitalWrite calls and runs from the main loop.

enum class LedState {
  Booting,        // both LEDs blink fast
  WifiConnecting, // red blinks slow, green off
  WifiConnected,  // green solid, red off (M2 success)
  MqttConnected,  // green solid, red off (same visual as WifiConnected)
  Error,          // red solid
  Ringing,        // green fast blink (visual feedback during a strike)
};

void ledsInit();
void ledsSet(LedState s);
void ledsTick();  // call frequently from loop()
