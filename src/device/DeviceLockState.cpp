#include "DeviceLockState.h"
#include "../NvsUtils.h"

bool DeviceLockState::getLockState(const String& deviceAddress) {
    if (deviceAddress != currentDevice) {
        // Загружаем состояние из NVS
        nvs_handle_t handle;
        esp_err_t err = nvs_open("m5kb_v1", NVS_READONLY, &handle);
        if (err != ESP_OK) return false;

        uint8_t locked = 0;
        String key = "locked_" + deviceAddress;
        err = nvs_get_u8(handle, key.c_str(), &locked);
        nvs_close(handle);

        if (err != ESP_OK) return false;
        currentLockState = (locked != 0);
        currentDevice = deviceAddress;
    }
    return currentLockState;
}

bool DeviceLockState::getLockState() {
    return currentLockState;
}

void DeviceLockState::setLockState(const String& deviceAddress, bool locked) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("m5kb_v1", NVS_READWRITE, &handle);
    if (err != ESP_OK) return;

    String key = "locked_" + deviceAddress;
    err = nvs_set_u8(handle, key.c_str(), locked ? 1 : 0);
    if (err == ESP_OK) {
        nvs_commit(handle);
        currentLockState = locked;
        currentDevice = deviceAddress;
    }
    nvs_close(handle);
}

void DeviceLockState::setLockState(bool locked) {
    if (!currentDevice.isEmpty()) {
        setLockState(currentDevice, locked);
    }
} 