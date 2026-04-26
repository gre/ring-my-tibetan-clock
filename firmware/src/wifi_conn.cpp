#include "wifi_conn.h"

#include <WiFi.h>

#include "leds.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

namespace {

constexpr const char *kHostname = "tibetan-clock";

// Backoff schedule mirrors the reference (10s, 30s, 60s, 120s, 300s, then 300s).
const uint32_t kBackoffMs[] = {10000, 30000, 60000, 120000, 300000};
constexpr size_t kBackoffMax = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]);

enum class State { Idle, Connecting, Connected, Backoff };

State g_state = State::Idle;
uint32_t g_phaseStart = 0;
uint32_t g_backoffMs = 0;
size_t g_attempt = 0;
bool g_lastReportedConnected = false;

void startConnect() {
  Serial.printf("[wifi] connecting to '%s'\n", WIFI_SSID);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kHostname);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  g_state = State::Connecting;
  g_phaseStart = millis();
  ledsSet(LedState::WifiConnecting);
}

}  // namespace

void wifiBegin() {
  if (sizeof(WIFI_SSID) <= 1) {
    Serial.println("[wifi] no SSID configured (build_flags missing)");
    ledsSet(LedState::Error);
    return;
  }
  startConnect();
}

void wifiTick() {
  uint32_t now = millis();
  bool connected = WiFi.status() == WL_CONNECTED;

  if (connected != g_lastReportedConnected) {
    g_lastReportedConnected = connected;
    if (connected) {
      Serial.printf("[wifi] connected, ip=%s rssi=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      g_attempt = 0;
      g_state = State::Connected;
      ledsSet(LedState::WifiConnected);
    } else {
      Serial.println("[wifi] link lost");
    }
  }

  switch (g_state) {
    case State::Idle:
      break;

    case State::Connecting:
      if (connected) break;  // handled above
      // Give the SDK 15 s for the initial connect attempt before forcing backoff.
      if (now - g_phaseStart > 15000) {
        g_backoffMs = kBackoffMs[g_attempt < kBackoffMax ? g_attempt
                                                        : kBackoffMax - 1];
        g_attempt++;
        Serial.printf("[wifi] connect failed, backoff %lus (attempt %u)\n",
                      g_backoffMs / 1000UL, (unsigned)g_attempt);
        WiFi.disconnect(true);
        g_state = State::Backoff;
        g_phaseStart = now;
      }
      break;

    case State::Connected:
      if (!connected) {
        Serial.println("[wifi] disconnected, will reconnect");
        g_state = State::Connecting;
        g_phaseStart = now;
        ledsSet(LedState::WifiConnecting);
        WiFi.reconnect();
      }
      break;

    case State::Backoff:
      if (now - g_phaseStart >= g_backoffMs) {
        startConnect();
      }
      break;
  }
}

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }
