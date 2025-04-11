#pragma once

#include <Arduino.h>
#include <nvs.h>

struct DeviceSettings {
    int unlockRssi;     // Минимальный RSSI для разблокировки
    int lockRssi;       // RSSI для блокировки
    String password;    // Пароль (зашифрованный)
    bool isLocked;      // Статус блокировки конкретного устройства
};

class DeviceSettingsManager {
public:
    static DeviceSettingsManager& getInstance() {
        static DeviceSettingsManager instance;
        return instance;
    }

    DeviceSettings getDeviceSettings(const String& deviceAddress);
    void saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings);
    String getPasswordForDevice(const String& deviceAddress);
    void savePasswordForDevice(const String& deviceAddress, const String& password);
    void clearAllSettings();

    // Константы для настроек по умолчанию
    static const int DEFAULT_UNLOCK_RSSI = -45;
    static const int DEFAULT_LOCK_RSSI = -60;

private:
    DeviceSettingsManager() {} // Приватный конструктор для синглтона
    
    String encryptPassword(const String& password);
    String decryptPassword(const String& encrypted);
    String getShortKey(const String& macAddress);

    static const char* NVS_NAMESPACE;
    static const char* KEY_PWD_PREFIX;
    static const uint8_t ENCRYPTION_KEY[32];
}; 