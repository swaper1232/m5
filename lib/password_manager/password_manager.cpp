#include "password_manager.h"
#include "../../src/NvsUtils.h"
#include <string.h>
#include <Arduino.h>

struct DeviceSettings {
    int unlockRssi;
    int lockRssi;
    String password;
};

DeviceSettings getDeviceSettings(const String &deviceAddress);
void saveDeviceSettings(const String &deviceAddress, const DeviceSettings &settings);
String encryptPassword(const String &password);
String decryptPassword(const String &encrypted);
extern bool serialOutputEnabled;

// Функция для очистки старых паролей, основанная на проверке длины ключа
void clearOldPasswords() {
    Serial.println("\n=== Clearing old passwords ===");
    
    nvs_handle_t localNvsHandle;
    esp_err_t err = nvs_open("m5kb_v1", NVS_READWRITE, &localNvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error opening NVS: %d\n", err);
        return;
    }
    
    // Получаем список всех ключей
    nvs_iterator_t it = nvs_entry_find("nvs", "m5kb_v1", NVS_TYPE_STR);
    while (it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        String key = String(info.key);
        
        // Проверяем только ключи паролей
        if (key.startsWith("pwd_")) {
            String shortKey = key.substring(4); // Убираем "pwd_"
            
            // Если длина ключа больше 6 символов, это старый формат
            if (shortKey.length() > 6) {
                Serial.printf("Removing old key: %s\n", key.c_str());
                nvs_erase_key(localNvsHandle, key.c_str());
                nvs_erase_key(localNvsHandle, ("unlock_" + shortKey).c_str());
                nvs_erase_key(localNvsHandle, ("lock_" + shortKey).c_str());
            }
        }
        it = nvs_entry_next(it);
    }
    nvs_release_iterator(it);
    
    nvs_commit(localNvsHandle);
    nvs_close(localNvsHandle);
    
    Serial.println("=== Old passwords cleared ===\n");
}

// Функция для получения пароля по адресу устройства
String getPasswordForDevice(const String &deviceAddress) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    return settings.password.length() > 0 ? decryptPassword(settings.password) : "";
}

// Функция для сохранения пароля для устройства
void savePasswordForDevice(const String &deviceAddress, const String &password) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    settings.password = encryptPassword(password);
    saveDeviceSettings(deviceAddress, settings);
    
    if (serialOutputEnabled) {
        Serial.printf("Saving password for device: %s\n", deviceAddress.c_str());
        Serial.printf("Password length: %d\n", password.length());
        Serial.printf("Encrypted length: %d\n", settings.password.length());
    }
} 