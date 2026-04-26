#include <Arduino.h>

#include "config.h"
#include "leds.h"
#include "servo.h"

#if ENABLE_NETWORKING
#include "mqtt.h"
#include "wifi_conn.h"
#endif

namespace {

String serialBuf;

void printHelp() {
  Serial.println(F("=== Tibetan clock — serial commands ==="));
  Serial.println(F("  0                 ring bell A (intensity 20)"));
  Serial.println(F("  1                 ring bell B (intensity 20)"));
  Serial.println(F("  <bell> <int> [n]  e.g. '0 30 3' = bell A, 30deg, 3 strikes"));
  Serial.println(F("  p                 toggle MOSFET (blue LED on/off, no strike)"));
  Serial.println(F("  s 0  / s 1        sweep one servo 60->120->90 (no strike)"));
  Serial.println(F("  ?                 print this help"));
  Serial.println(F("======================================="));
}

void doRing(uint8_t bell, uint8_t count, uint8_t intensity) {
  ledsSet(LedState::Ringing);
#if ENABLE_NETWORKING
  mqttPublishStatus("ringing");
#endif
  ringBell(bell, count, intensity);
#if ENABLE_NETWORKING
  mqttPublishStatus("idle");
  if (mqttIsConnected()) ledsSet(LedState::MqttConnected);
  else if (wifiIsConnected()) ledsSet(LedState::WifiConnected);
  else ledsSet(LedState::WifiConnecting);
#else
  ledsSet(LedState::WifiConnected);  // visual: solid green when idle
#endif
}

void handleSerialLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  Serial.printf("[serial] got: '%s'\n", line.c_str());

  if (line == "?") {
    printHelp();
    return;
  }

  if (line == "p") {
    servoPowerToggle();
    return;
  }

  if (line.startsWith("s ") || line == "s") {
    int b = line.length() > 2 ? line.substring(2).toInt() : 0;
    servoSweep((uint8_t)b);
    return;
  }

  int values[3] = {-1, -1, -1};
  int slot = 0;
  int idx = 0;
  while (idx <= (int)line.length() && slot < 3) {
    int next = line.indexOf(' ', idx);
    String tok = (next < 0) ? line.substring(idx) : line.substring(idx, next);
    tok.trim();
    if (tok.length() > 0) {
      values[slot++] = tok.toInt();
    }
    if (next < 0) break;
    idx = next + 1;
  }

  int bell = values[0];
  if (bell < 0 || bell > 1) {
    Serial.printf("[err] invalid bell '%s' (expected 0 or 1)\n", line.c_str());
    return;
  }

  const auto &cfg = configGet();
  int intensity = values[1] >= 0 ? values[1] : cfg.intensity[bell];
  int count = values[2] >= 0 ? values[2] : cfg.count[bell];

  doRing((uint8_t)bell, (uint8_t)count, (uint8_t)intensity);
}

void readSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    // Treat both \r and \n as line terminators (miniterm sends \r on Enter).
    if (c == '\r' || c == '\n') {
      if (serialBuf.length() > 0) {
        handleSerialLine(serialBuf);
        serialBuf = "";
      }
    } else if (serialBuf.length() < 64) {
      serialBuf += c;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  // C3 USB-Serial/JTAG: give the host a moment to enumerate so the boot banner
  // isn't swallowed. Cap the wait so a missing host doesn't block boot.
  uint32_t deadline = millis() + 3000;
  while (!Serial && millis() < deadline) delay(50);
  delay(300);

  Serial.println();
  Serial.println(F("============================================="));
  Serial.println(F(" Tibetan clock firmware — M1 (servo focus)"));
  Serial.printf(" SDK: %s\n", ESP.getSdkVersion());
  Serial.printf(" Networking: %s\n",
                ENABLE_NETWORKING ? "ENABLED" : "DISABLED (M1 focus)");
  Serial.println(F("============================================="));

  ledsInit();
  ledsSet(LedState::Booting);
  servoInit();
  configBegin();


#if ENABLE_NETWORKING
  wifiBegin();
  mqttBegin();
#endif

  printHelp();
  ledsSet(LedState::WifiConnected);  // solid green = idle/ready
}

void loop() {
  ledsTick();
  servoTick();
#if ENABLE_NETWORKING
  wifiTick();
  mqttTick();
  PendingRing p;
  if (mqttConsumePendingRing(p)) {
    doRing(p.bell, p.count, p.intensity);
  }
#endif
  readSerial();
}
