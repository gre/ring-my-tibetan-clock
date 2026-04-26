#include "leds.h"

namespace {
LedState g_state = LedState::Booting;
uint32_t g_lastChange = 0;
int g_lastGreen = -1;
int g_lastRed = -1;

void writePins(bool green, bool red) {
  // ledsTick() runs at full loop speed — skip the GPIO write when nothing
  // changed so we don't hammer the IO mux thousands of times per second.
  if ((int)green != g_lastGreen) {
    digitalWrite(PIN_LED_GREEN, green ? HIGH : LOW);
    g_lastGreen = green;
  }
  if ((int)red != g_lastRed) {
    digitalWrite(PIN_LED_RED, red ? HIGH : LOW);
    g_lastRed = red;
  }
}
}  // namespace

void ledsInit() {
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  writePins(false, false);
}

void ledsSet(LedState s) {
  if (s != g_state) {
    g_state = s;
    g_lastChange = millis();
  }
}

void ledsTick() {
  uint32_t now = millis();
  uint32_t phase = now % 1000;  // 1 Hz cycle
  bool slow = phase < 500;
  bool fast = (now % 250) < 125;

  switch (g_state) {
    case LedState::Booting:
      writePins(fast, fast);
      break;
    case LedState::WifiConnecting:
      writePins(false, slow);
      break;
    case LedState::WifiConnected:
    case LedState::MqttConnected:
      writePins(true, false);
      break;
    case LedState::Error:
      writePins(false, true);
      break;
    case LedState::Ringing:
      writePins(fast, false);
      break;
  }
}
