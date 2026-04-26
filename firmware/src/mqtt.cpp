#include "mqtt.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "config.h"
#include "servo.h"
#include "wifi_conn.h"

#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID "tibetan_clock_esp32"
#endif

namespace {

constexpr const char *kAvailability =
    "homeassistant/binary_sensor/tibetan_clock/availability";
constexpr const char *kStatus =
    "homeassistant/binary_sensor/tibetan_clock/state";

// Buttons.
constexpr const char *kDiscoveryBellA =
    "homeassistant/button/tibetan_clock/ring_bell_a/config";
constexpr const char *kDiscoveryBellB =
    "homeassistant/button/tibetan_clock/ring_bell_b/config";
constexpr const char *kCommandBellA =
    "homeassistant/button/tibetan_clock/ring_bell_a/command";
constexpr const char *kCommandBellB =
    "homeassistant/button/tibetan_clock/ring_bell_b/command";

// Per-bell number sliders.
constexpr const char *kDiscoveryIntA =
    "homeassistant/number/tibetan_clock/bell_a_intensity/config";
constexpr const char *kStateIntA =
    "homeassistant/number/tibetan_clock/bell_a_intensity/state";
constexpr const char *kCommandIntA =
    "homeassistant/number/tibetan_clock/bell_a_intensity/set";

constexpr const char *kDiscoveryIntB =
    "homeassistant/number/tibetan_clock/bell_b_intensity/config";
constexpr const char *kStateIntB =
    "homeassistant/number/tibetan_clock/bell_b_intensity/state";
constexpr const char *kCommandIntB =
    "homeassistant/number/tibetan_clock/bell_b_intensity/set";

constexpr const char *kDiscoveryCountA =
    "homeassistant/number/tibetan_clock/bell_a_count/config";
constexpr const char *kStateCountA =
    "homeassistant/number/tibetan_clock/bell_a_count/state";
constexpr const char *kCommandCountA =
    "homeassistant/number/tibetan_clock/bell_a_count/set";

constexpr const char *kDiscoveryCountB =
    "homeassistant/number/tibetan_clock/bell_b_count/config";
constexpr const char *kStateCountB =
    "homeassistant/number/tibetan_clock/bell_b_count/state";
constexpr const char *kCommandCountB =
    "homeassistant/number/tibetan_clock/bell_b_count/set";

constexpr const char *kDiscoveryCenterA =
    "homeassistant/number/tibetan_clock/bell_a_center/config";
constexpr const char *kStateCenterA =
    "homeassistant/number/tibetan_clock/bell_a_center/state";
constexpr const char *kCommandCenterA =
    "homeassistant/number/tibetan_clock/bell_a_center/set";

constexpr const char *kDiscoveryCenterB =
    "homeassistant/number/tibetan_clock/bell_b_center/config";
constexpr const char *kStateCenterB =
    "homeassistant/number/tibetan_clock/bell_b_center/state";
constexpr const char *kCommandCenterB =
    "homeassistant/number/tibetan_clock/bell_b_center/set";

constexpr const char *kDiscoveryCalibrate =
    "homeassistant/button/tibetan_clock/calibrate/config";
constexpr const char *kCommandCalibrate =
    "homeassistant/button/tibetan_clock/calibrate/command";

constexpr const char *kDiscoverySwingUp =
    "homeassistant/number/tibetan_clock/swing_up/config";
constexpr const char *kStateSwingUp =
    "homeassistant/number/tibetan_clock/swing_up/state";
constexpr const char *kCommandSwingUp =
    "homeassistant/number/tibetan_clock/swing_up/set";

constexpr const char *kDiscoverySwingDown =
    "homeassistant/number/tibetan_clock/swing_down/config";
constexpr const char *kStateSwingDown =
    "homeassistant/number/tibetan_clock/swing_down/state";
constexpr const char *kCommandSwingDown =
    "homeassistant/number/tibetan_clock/swing_down/set";

constexpr const char *kDiscoveryCycle =
    "homeassistant/number/tibetan_clock/cycle/config";
constexpr const char *kStateCycle =
    "homeassistant/number/tibetan_clock/cycle/state";
constexpr const char *kCommandCycle =
    "homeassistant/number/tibetan_clock/cycle/set";

// Old (v1) topics — published empty/retain on connect to clean up HA's stale
// entities from the previous schema. Drop this list once everyone has flashed.
constexpr const char *kLegacyDiscovery[] = {
    "homeassistant/number/tibetan_clock/default_intensity/config",
    "homeassistant/number/tibetan_clock/default_count/config",
    "homeassistant/number/tibetan_clock/default_intensity/state",
    "homeassistant/number/tibetan_clock/default_count/state",
};

WiFiClient g_wifi;
PubSubClient g_mqtt(g_wifi);

PendingRing g_pending;
bool g_pendingValid = false;

uint32_t g_nextRetry = 0;
uint32_t g_retryDelayMs = 2000;

void addDeviceBlock(JsonDocument &doc) {
  auto dev = doc["device"].to<JsonObject>();
  auto ids = dev["identifiers"].to<JsonArray>();
  ids.add("tibetan_clock");
  dev["name"] = "Tibetan Clock";
  dev["manufacturer"] = "DIY";
  dev["model"] = "ESP32-C3";
}

void publishUInt(const char *topic, unsigned int v) {
  char buf[8];
  int n = snprintf(buf, sizeof(buf), "%u", v);
  g_mqtt.publish(topic, (const uint8_t *)buf, n, true);
}

void publishConfigState() {
  const auto &cfg = configGet();
  publishUInt(kStateIntA, cfg.intensity[0]);
  publishUInt(kStateIntB, cfg.intensity[1]);
  publishUInt(kStateCountA, cfg.count[0]);
  publishUInt(kStateCountB, cfg.count[1]);
  publishUInt(kStateCenterA, cfg.center_deg[0]);
  publishUInt(kStateCenterB, cfg.center_deg[1]);
  publishUInt(kStateSwingUp, cfg.swing_up_step_ms);
  publishUInt(kStateSwingDown, cfg.swing_down_step_ms);
  publishUInt(kStateCycle, cfg.cycle_ms);
}

unsigned int parseUInt(const uint8_t *payload, unsigned int length,
                       unsigned int fallback) {
  char buf[8];
  unsigned int copy = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, copy);
  buf[copy] = '\0';
  long v = atol(buf);
  if (v < 0) return fallback;
  if (v > 65535) return 65535;
  return (unsigned int)v;
}

void onMessage(char *topic, uint8_t *payload, unsigned int length) {
  Serial.printf("[mqtt] rx %s (%u B)\n", topic, length);

  if (strcmp(topic, kCommandBellA) == 0 || strcmp(topic, kCommandBellB) == 0) {
    uint8_t bell = (strcmp(topic, kCommandBellB) == 0) ? 1 : 0;
    const auto &cfg = configGet();
    uint8_t intensity = cfg.intensity[bell];
    uint8_t count = cfg.count[bell];

    // Body is optional. HA's button press sends "PRESS" by default which
    // isn't valid JSON — that's expected; we just fall back to defaults.
    if (length > 0 && payload[0] == '{') {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (!err) {
        intensity = doc["intensity"] | intensity;
        count = doc["count"] | count;
      } else {
        Serial.printf("[mqtt] payload parse error: %s\n", err.c_str());
      }
    }

    Serial.printf("[mqtt] cmd ring bell=%u intensity=%u count=%u\n", bell,
                  intensity, count);
    g_pending = {bell, count, intensity};
    g_pendingValid = true;
    return;
  }

  if (strcmp(topic, kCommandIntA) == 0) {
    uint8_t v = (uint8_t)parseUInt(payload, length, configGet().intensity[0]);
    if (configSetIntensity(0, v)) publishUInt(kStateIntA, configGet().intensity[0]);
    return;
  }
  if (strcmp(topic, kCommandIntB) == 0) {
    uint8_t v = (uint8_t)parseUInt(payload, length, configGet().intensity[1]);
    if (configSetIntensity(1, v)) publishUInt(kStateIntB, configGet().intensity[1]);
    return;
  }
  if (strcmp(topic, kCommandCountA) == 0) {
    uint8_t v = (uint8_t)parseUInt(payload, length, configGet().count[0]);
    if (configSetCount(0, v)) publishUInt(kStateCountA, configGet().count[0]);
    return;
  }
  if (strcmp(topic, kCommandCountB) == 0) {
    uint8_t v = (uint8_t)parseUInt(payload, length, configGet().count[1]);
    if (configSetCount(1, v)) publishUInt(kStateCountB, configGet().count[1]);
    return;
  }

  if (strcmp(topic, kCommandCenterA) == 0) {
    uint8_t v = (uint8_t)parseUInt(payload, length, configGet().center_deg[0]);
    if (configSetCenterDeg(0, v)) {
      publishUInt(kStateCenterA, configGet().center_deg[0]);
      servoApplyCenterIfPowered(0);
    }
    return;
  }
  if (strcmp(topic, kCommandCenterB) == 0) {
    uint8_t v = (uint8_t)parseUInt(payload, length, configGet().center_deg[1]);
    if (configSetCenterDeg(1, v)) {
      publishUInt(kStateCenterB, configGet().center_deg[1]);
      servoApplyCenterIfPowered(1);
    }
    return;
  }

  if (strcmp(topic, kCommandCalibrate) == 0) {
    Serial.println("[mqtt] calibrate pressed");
    servoEnterCalibration();
    return;
  }

  if (strcmp(topic, kCommandSwingUp) == 0) {
    uint8_t v = (uint8_t)parseUInt(payload, length, configGet().swing_up_step_ms);
    if (configSetSwingUpMs(v))
      publishUInt(kStateSwingUp, configGet().swing_up_step_ms);
    return;
  }
  if (strcmp(topic, kCommandSwingDown) == 0) {
    uint8_t v = (uint8_t)parseUInt(payload, length, configGet().swing_down_step_ms);
    if (configSetSwingDownMs(v))
      publishUInt(kStateSwingDown, configGet().swing_down_step_ms);
    return;
  }
  if (strcmp(topic, kCommandCycle) == 0) {
    uint16_t v = (uint16_t)parseUInt(payload, length, configGet().cycle_ms);
    if (configSetCycleMs(v))
      publishUInt(kStateCycle, configGet().cycle_ms);
    return;
  }

  Serial.printf("[mqtt] unhandled topic %s\n", topic);
}

void publishOneDiscovery(const char *topic, const JsonDocument &doc) {
  char buf[640];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  bool ok = g_mqtt.publish(topic, (const uint8_t *)buf, n, true);
  Serial.printf("[mqtt] discovery %s -> %s (%uB)\n",
                ok ? "published" : "FAILED", topic, (unsigned)n);
}

void publishButton(const char *topic, const char *cmd, const char *name,
                   const char *uniq, const char *icon) {
  JsonDocument doc;
  doc["name"] = name;
  doc["command_topic"] = cmd;
  doc["availability_topic"] = kAvailability;
  doc["unique_id"] = uniq;
  doc["icon"] = icon;
  addDeviceBlock(doc);
  publishOneDiscovery(topic, doc);
}

void publishNumber(const char *cfg_topic, const char *cmd, const char *state,
                   const char *name, const char *uniq, int min, int max,
                   const char *unit, const char *icon) {
  JsonDocument doc;
  doc["name"] = name;
  doc["command_topic"] = cmd;
  doc["state_topic"] = state;
  doc["availability_topic"] = kAvailability;
  doc["unique_id"] = uniq;
  doc["min"] = min;
  doc["max"] = max;
  doc["step"] = 1;
  doc["mode"] = "slider";
  doc["unit_of_measurement"] = unit;
  doc["icon"] = icon;
  doc["entity_category"] = "config";
  addDeviceBlock(doc);
  publishOneDiscovery(cfg_topic, doc);
}

void purgeLegacyTopics() {
  for (auto t : kLegacyDiscovery) {
    g_mqtt.publish(t, (const uint8_t *)"", 0, true);
    Serial.printf("[mqtt] purged legacy retained topic %s\n", t);
  }
}

void publishDiscovery() {
  publishButton(kDiscoveryBellA, kCommandBellA, "Ring Bell A",
                "tibetan_clock_bell_a", "mdi:bell-ring");
  publishButton(kDiscoveryBellB, kCommandBellB, "Ring Bell B",
                "tibetan_clock_bell_b", "mdi:bell-ring-outline");

  publishNumber(kDiscoveryIntA, kCommandIntA, kStateIntA, "Bell A Intensity",
                "tibetan_clock_bell_a_intensity", BELL_INTENSITY_MIN,
                BELL_INTENSITY_MAX, "deg", "mdi:angle-acute");
  publishNumber(kDiscoveryIntB, kCommandIntB, kStateIntB, "Bell B Intensity",
                "tibetan_clock_bell_b_intensity", BELL_INTENSITY_MIN,
                BELL_INTENSITY_MAX, "deg", "mdi:angle-acute");
  publishNumber(kDiscoveryCountA, kCommandCountA, kStateCountA, "Bell A Count",
                "tibetan_clock_bell_a_count", 1, 9, "strikes", "mdi:counter");
  publishNumber(kDiscoveryCountB, kCommandCountB, kStateCountB, "Bell B Count",
                "tibetan_clock_bell_b_count", 1, 9, "strikes", "mdi:counter");
  publishNumber(kDiscoveryCenterA, kCommandCenterA, kStateCenterA,
                "Bell A Center", "tibetan_clock_bell_a_center", 60, 120, "deg",
                "mdi:angle-acute");
  publishNumber(kDiscoveryCenterB, kCommandCenterB, kStateCenterB,
                "Bell B Center", "tibetan_clock_bell_b_center", 60, 120, "deg",
                "mdi:angle-acute");

  publishButton(kDiscoveryCalibrate, kCommandCalibrate, "Calibrate",
                "tibetan_clock_calibrate", "mdi:tune-vertical");

  publishNumber(kDiscoverySwingUp, kCommandSwingUp, kStateSwingUp,
                "Swing Up Step", "tibetan_clock_swing_up", 0, 30, "ms",
                "mdi:arrow-up-bold");
  publishNumber(kDiscoverySwingDown, kCommandSwingDown, kStateSwingDown,
                "Swing Down Step", "tibetan_clock_swing_down", 0, 10, "ms",
                "mdi:arrow-down-bold");
  publishNumber(kDiscoveryCycle, kCommandCycle, kStateCycle, "Strike Cycle",
                "tibetan_clock_cycle", 500, 10000, "ms", "mdi:metronome");
}

bool tryConnect() {
  Serial.printf("[mqtt] connecting to %s:%d as '%s' ...\n", MQTT_HOST,
                MQTT_PORT, MQTT_CLIENT_ID);

  // LWT: broker auto-publishes "offline" if we drop without saying goodbye.
  bool ok = g_mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                           kAvailability, 1, true, "offline");
  if (!ok) {
    Serial.printf("[mqtt] connect failed, state=%d\n", g_mqtt.state());
    return false;
  }

  Serial.println("[mqtt] connected");
  g_mqtt.publish(kAvailability, "online", true);
  g_mqtt.subscribe(kCommandBellA, 1);
  g_mqtt.subscribe(kCommandBellB, 1);
  g_mqtt.subscribe(kCommandIntA, 1);
  g_mqtt.subscribe(kCommandIntB, 1);
  g_mqtt.subscribe(kCommandCountA, 1);
  g_mqtt.subscribe(kCommandCountB, 1);
  g_mqtt.subscribe(kCommandCenterA, 1);
  g_mqtt.subscribe(kCommandCenterB, 1);
  g_mqtt.subscribe(kCommandCalibrate, 1);
  g_mqtt.subscribe(kCommandSwingUp, 1);
  g_mqtt.subscribe(kCommandSwingDown, 1);
  purgeLegacyTopics();
  publishDiscovery();
  publishConfigState();
  return true;
}

}  // namespace

void mqttBegin() {
  if (sizeof(MQTT_HOST) <= 1) {
    Serial.println("[mqtt] no broker configured (build_flags missing)");
    return;
  }

  // setBufferSize must precede connect — discovery payloads exceed the 256 B
  // default ([PubSubClient #764]).
  g_mqtt.setBufferSize(1024);
  g_mqtt.setKeepAlive(60);  // longer than worst-case ring (~12 s)
  g_mqtt.setServer(MQTT_HOST, MQTT_PORT);
  g_mqtt.setCallback(onMessage);
}

void mqttTick() {
  if (!wifiIsConnected()) return;

  if (!g_mqtt.connected()) {
    uint32_t now = millis();
    if (now < g_nextRetry) return;
    if (tryConnect()) {
      g_retryDelayMs = 2000;
    } else {
      g_nextRetry = now + g_retryDelayMs;
      g_retryDelayMs = g_retryDelayMs < 60000 ? g_retryDelayMs * 2 : 60000;
    }
    return;
  }

  g_mqtt.loop();
}

bool mqttIsConnected() { return g_mqtt.connected(); }

bool mqttConsumePendingRing(PendingRing &out) {
  if (!g_pendingValid) return false;
  out = g_pending;
  g_pendingValid = false;
  return true;
}

void mqttPublishStatus(const char *state) {
  if (g_mqtt.connected()) {
    g_mqtt.publish(kStatus, state, false);
  }
}
