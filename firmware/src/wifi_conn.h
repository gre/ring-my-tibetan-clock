#pragma once

#include <Arduino.h>

void wifiBegin();
void wifiTick();           // call frequently from loop()
bool wifiIsConnected();
