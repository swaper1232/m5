#pragma once

#include <Arduino.h>
#include "StorageManager.h"

// Структура для хранения настроек устройства
struct DeviceSettings {
    int unlockRssi;      // Минимальный RSSI для разблокировки
    int lockRssi;        // RSSI для блокировки
    int criticalRssi;    // Критический RSSI для отключения
    String password;     // Пароль (зашифрованный)
    bool isLocked;       // Состояние блокировки
    
    // Конструктор по умолчанию
    DeviceSettings() : 
        unlockRssi(-45), 
        lockRssi(-60), 
        criticalRssi(-75),
        password(""),
        isLocked(false) {}
};

// Класс для работы с настройками устройств
class DeviceManager {
public:
    DeviceManager(StorageManager& storage);
    
    // Получение настроек устройства
    DeviceSettings getDeviceSettings(const String& deviceAddress);
    
    // Сохранение настроек устройства
    bool saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings);
    
    // Работа с паролем
    String getPassword(const String& deviceAddress);
    bool savePassword(const String& deviceAddress, const String& password);
    
    // Работа с состоянием блокировки
    bool getLockState(const String& deviceAddress);
    bool getLockState(); // Для текущего подключенного устройства
    bool setLockState(const String& deviceAddress, bool locked);
    bool setLockState(bool locked); // Для текущего подключенного устройства
    
    // Работа с RSSI порогами
    bool saveRssiThresholds(const String& deviceAddress, int unlockRssi, int lockRssi, int criticalRssi);
    
    // Шифрование пароля
    String encryptPassword(const String& password);
    String decryptPassword(const String& encrypted);
    
    // Управление списком устройств
    bool listStoredDevices();
    bool clearAllPasswords();
    bool clearAllSettings();
    
    // Установка текущего устройства
    void setCurrentDevice(const String& deviceAddress);
    String getCurrentDevice();
    
private:
    StorageManager& storage;
    String currentDeviceAddress;
    
    // Вспомогательные константы и методы
    static const char* KEY_PWD_PREFIX;
    static const char* KEY_UNLOCK_PREFIX;
    static const char* KEY_LOCK_PREFIX;
    static const char* KEY_CRITICAL_PREFIX;
    static const char* KEY_IS_LOCKED;
    static const char* KEY_LAST_ADDR;
    
    // Ключ шифрования
    static const uint8_t ENCRYPTION_KEY[32];
    
    // Получение короткого ключа из MAC-адреса
    String cleanMacAddress(const String& macAddress);
}; 