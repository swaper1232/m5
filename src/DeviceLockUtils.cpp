#include "DeviceLockUtils.h"
#include "DeviceSettingsUtils.h" // Для функции cleanMacAddress
#include "NvsUtils.h" // Для доступа к nvsHandle
#include <nvs.h>

// Константа для префикса ключей блокировки устройств
static const char* KEY_LOCK_STATE_PREFIX = "locked_";

static void saveDeviceLockState(const String& deviceAddress, bool locked) {
    String sk = cleanMacAddress(deviceAddress.c_str());
    String key = String(KEY_LOCK_STATE_PREFIX) + sk;
    nvs_set_i8(nvsHandle, key.c_str(), locked ? 1 : 0);
    nvs_commit(nvsHandle);
}

static bool loadDeviceLockState(const String& deviceAddress) {
    String sk = cleanMacAddress(deviceAddress.c_str());
    String key = String(KEY_LOCK_STATE_PREFIX) + sk;
    int8_t flag = 0;
    if (nvs_get_i8(nvsHandle, key.c_str(), &flag) == ESP_OK) {
        return flag != 0;
    }
    return false;
} 