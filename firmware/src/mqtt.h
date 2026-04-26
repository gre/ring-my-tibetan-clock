#pragma once

#include <Arduino.h>

struct PendingRing {
  uint8_t bell;
  uint8_t count;
  uint8_t intensity;
};

void mqttBegin();
void mqttTick();           // call frequently from loop()
bool mqttIsConnected();

// Atomically takes the pending ring command (if any) and clears it.
// Returns true if `out` was populated; false if nothing was pending.
bool mqttConsumePendingRing(PendingRing &out);

// Status updates for HA / debugging.
void mqttPublishStatus(const char *state);
