#include "DeviceLockState.h"
#include <nvs.h>

// Определения структур и внешних переменных, которые нам нужны
extern nvs_handle_t nvsHandle;
extern std::string connectedDeviceAddress;

// Определяем константы здесь, если они не определены в другом месте
const char* KEY_LAST_ADDR = "last_addr";
const char* KEY_IS_LOCKED = "is_locked";

// Определение типа для структуры DeviceSettings
struct DeviceSettings {
    int unlockRssi;
    int lockRssi;
    String password;
    bool isLocked;
};

// Объявления других функций, которые нам нужны
DeviceSettings getDeviceSettings(const String& deviceAddress);
void saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings);

// Реализация функций для работы с состоянием блокировки устройства
bool getDeviceLockState(const String& deviceAddress) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    return settings.isLocked;
}

bool getDeviceLockState() {
    if (connectedDeviceAddress.length() == 0) {
        Serial.println("No connected device to check lock state");
        return false;
    }
    
    // Проверяем глобальное состояние блокировки
    int8_t locked = 0;
    esp_err_t err = nvs_get_i8(nvsHandle, KEY_IS_LOCKED, &locked);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error getting lock state: %d\n", err);
    }
    
    // Если устройство глобально заблокировано, возвращаем true
    if (locked != 0) {
        return true;
    }
    
    // Иначе проверяем статус конкретного устройства
    return getDeviceLockState(connectedDeviceAddress.c_str());
}

void setDeviceLockState(const String& deviceAddress, bool locked) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    settings.isLocked = locked;
    saveDeviceSettings(deviceAddress, settings);
    
    if (locked) {
        // Если блокируем, то сохраняем адрес последнего заблокированного устройства
        nvs_set_str(nvsHandle, KEY_LAST_ADDR, deviceAddress.c_str());
        nvs_commit(nvsHandle);
    }
    
    Serial.printf("Lock state for device %s set to: %s\n", 
                  deviceAddress.c_str(), 
                  locked ? "LOCKED" : "UNLOCKED");
}

void setDeviceLockState(bool locked) {
    if (connectedDeviceAddress.length() == 0) {
        Serial.println("No connected device to set lock state");
        return;
    }
    
    // Сохраняем состояние блокировки в NVS
    esp_err_t err = nvs_set_i8(nvsHandle, KEY_IS_LOCKED, locked ? 1 : 0);
    if (err != ESP_OK) {
        Serial.printf("Error setting lock state: %d\n", err);
        return;
    }
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error committing lock state: %d\n", err);
    }
    
    // И также обновляем состояние блокировки для текущего устройства
    setDeviceLockState(connectedDeviceAddress.c_str(), locked);
} 