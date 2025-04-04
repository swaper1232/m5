#pragma once

#include <Arduino.h>
#include <nvs.h>

// Объявления функций для работы с состоянием блокировки устройств
bool getDeviceLockState(const String& deviceAddress);
bool getDeviceLockState();
void setDeviceLockState(const String& deviceAddress, bool locked);
void setDeviceLockState(bool locked); 