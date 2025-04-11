#include "DeviceSettings.h"
#include "../NvsUtils.h"

const char* DeviceSettingsManager::NVS_NAMESPACE = "m5kb_v1";
const char* DeviceSettingsManager::KEY_PWD_PREFIX = "pwd_";
const uint8_t DeviceSettingsManager::ENCRYPTION_KEY[32] = {
    0x89, 0x4E, 0x1C, 0xE7, 0x3A, 0x5D, 0x2B, 0xF8,
    0x6C, 0x91, 0x0D, 0xB4, 0x7F, 0xE2, 0x9A, 0x3C,
    0x5E, 0x8D, 0x1B, 0xF4, 0x6A, 0x2C, 0x9E, 0x0B,
    0x7D, 0x4F, 0xA3, 0xE5, 0x8C, 0x1D, 0xB6, 0x3F
};

DeviceSettings DeviceSettingsManager::getDeviceSettings(const String& deviceAddress) {
    DeviceSettings settings;
    settings.unlockRssi = DEFAULT_UNLOCK_RSSI;
    settings.lockRssi = DEFAULT_LOCK_RSSI;
    settings.isLocked = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return settings;

    // Загружаем настройки RSSI
    String shortKey = getShortKey(deviceAddress);
    int32_t unlockRssi, lockRssi;
    err = nvs_get_i32(handle, (shortKey + "_unlock").c_str(), &unlockRssi);
    if (err == ESP_OK) settings.unlockRssi = unlockRssi;
    
    err = nvs_get_i32(handle, (shortKey + "_lock").c_str(), &lockRssi);
    if (err == ESP_OK) settings.lockRssi = lockRssi;

    // Загружаем пароль
    settings.password = getPasswordForDevice(deviceAddress);

    // Загружаем состояние блокировки
    uint8_t locked = 0;
    err = nvs_get_u8(handle, (shortKey + "_locked").c_str(), &locked);
    if (err == ESP_OK) settings.isLocked = (locked != 0);

    nvs_close(handle);
    return settings;
}

void DeviceSettingsManager::saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return;

    String shortKey = getShortKey(deviceAddress);
    
    // Сохраняем настройки RSSI
    nvs_set_i32(handle, (shortKey + "_unlock").c_str(), settings.unlockRssi);
    nvs_set_i32(handle, (shortKey + "_lock").c_str(), settings.lockRssi);
    
    // Сохраняем пароль
    if (!settings.password.isEmpty()) {
        savePasswordForDevice(deviceAddress, settings.password);
    }
    
    // Сохраняем состояние блокировки
    nvs_set_u8(handle, (shortKey + "_locked").c_str(), settings.isLocked ? 1 : 0);
    
    nvs_commit(handle);
    nvs_close(handle);
}

String DeviceSettingsManager::getPasswordForDevice(const String& deviceAddress) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return "";

    String key = KEY_PWD_PREFIX + getShortKey(deviceAddress);
    size_t required_size;
    err = nvs_get_str(handle, key.c_str(), nullptr, &required_size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return "";
    }

    char* encrypted = new char[required_size];
    err = nvs_get_str(handle, key.c_str(), encrypted, &required_size);
    nvs_close(handle);

    if (err != ESP_OK) {
        delete[] encrypted;
        return "";
    }

    String result = decryptPassword(String(encrypted));
    delete[] encrypted;
    return result;
}

void DeviceSettingsManager::savePasswordForDevice(const String& deviceAddress, const String& password) {
    if (password.isEmpty()) return;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return;

    String key = KEY_PWD_PREFIX + getShortKey(deviceAddress);
    String encrypted = encryptPassword(password);
    
    nvs_set_str(handle, key.c_str(), encrypted.c_str());
    nvs_commit(handle);
    nvs_close(handle);
}

void DeviceSettingsManager::clearAllSettings() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return;
    
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
}

String DeviceSettingsManager::encryptPassword(const String& password) {
    // Простое XOR шифрование с циклическим ключом
    String result;
    for (size_t i = 0; i < password.length(); i++) {
        result += char(password[i] ^ ENCRYPTION_KEY[i % sizeof(ENCRYPTION_KEY)]);
    }
    return result;
}

String DeviceSettingsManager::decryptPassword(const String& encrypted) {
    // XOR шифрование симметрично, поэтому используем тот же алгоритм
    return encryptPassword(encrypted);
}

String DeviceSettingsManager::getShortKey(const String& macAddress) {
    // Преобразуем MAC-адрес в короткий ключ, убирая двоеточия
    String result = macAddress;
    result.replace(":", "");
    return result;
} 