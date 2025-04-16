#include "device_utils.h"
#include "DeviceSettingsUtils.h"

// Реализация функции getShortKey, которая возвращает очищенный MAC-адрес
String getShortKey(const char* macAddress) {
    return cleanMacAddress(macAddress);
} 