#include "DeviceManager.h"
#include "DebugUtils.h"

// Определение статических констант
const char* DeviceManager::KEY_PWD_PREFIX = "pwd";
const char* DeviceManager::KEY_UNLOCK_PREFIX = "unlock";
const char* DeviceManager::KEY_LOCK_PREFIX = "lock";
const char* DeviceManager::KEY_CRITICAL_PREFIX = "critical";
const char* DeviceManager::KEY_IS_LOCKED = "is_locked";
const char* DeviceManager::KEY_LAST_ADDR = "last_addr";

// Ключ шифрования паролей
const uint8_t DeviceManager::ENCRYPTION_KEY[32] = {
    0x89, 0x4E, 0x1C, 0xE7, 0x3A, 0x5D, 0x2B, 0xF8,
    0x6C, 0x91, 0x0D, 0xB4, 0x7F, 0xE2, 0x9A, 0x3C,
    0x5E, 0x8D, 0x1B, 0xF4, 0x6A, 0x2C, 0x9E, 0x0B,
    0x7D, 0x4F, 0xA3, 0xE5, 0x8C, 0x1D, 0xB6, 0x3F
};

DeviceManager::DeviceManager(StorageManager& storage) : storage(storage) {
    // Загружаем адрес последнего устройства
    currentDeviceAddress = storage.loadString(KEY_LAST_ADDR, "");
}

DeviceSettings DeviceManager::getDeviceSettings(const String& deviceAddress) {
    DeviceSettings settings;
    String shortKey = cleanMacAddress(deviceAddress);
    
    // Загружаем пароль
    String pwdKey = storage.makeKey(KEY_PWD_PREFIX, shortKey);
    settings.password = storage.loadString(pwdKey.c_str(), "");
    
    // Загружаем пороги RSSI
    String unlockKey = storage.makeKey(KEY_UNLOCK_PREFIX, shortKey);
    settings.unlockRssi = storage.loadInt(unlockKey.c_str(), settings.unlockRssi);
    
    String lockKey = storage.makeKey(KEY_LOCK_PREFIX, shortKey);
    settings.lockRssi = storage.loadInt(lockKey.c_str(), settings.lockRssi);
    
    String criticalKey = storage.makeKey(KEY_CRITICAL_PREFIX, shortKey);
    settings.criticalRssi = storage.loadInt(criticalKey.c_str(), settings.criticalRssi);
    
    // Загружаем состояние блокировки
    settings.isLocked = storage.loadBool(KEY_IS_LOCKED, false);
    
    debug_printf("Loaded settings for device %s: unlock=%d, lock=%d, critical=%d\n", 
                deviceAddress.c_str(), settings.unlockRssi, settings.lockRssi, settings.criticalRssi);
    
    return settings;
}

bool DeviceManager::saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings) {
    debug_printf("\n=== Saving Device Settings ===\n");
    debug_printf("Device address: %s\n", deviceAddress.c_str());
    
    String shortKey = cleanMacAddress(deviceAddress);
    debug_printf("Short key: %s\n", shortKey.c_str());
    
    // Сохраняем пароль
    String pwdKey = storage.makeKey(KEY_PWD_PREFIX, shortKey);
    debug_printf("Password key: %s\n", pwdKey.c_str());
    if (!storage.saveString(pwdKey.c_str(), settings.password)) {
        return false;
    }
    
    // Сохраняем пороги RSSI
    String unlockKey = storage.makeKey(KEY_UNLOCK_PREFIX, shortKey);
    debug_printf("Unlock key: %s\n", unlockKey.c_str());
    if (!storage.saveInt(unlockKey.c_str(), settings.unlockRssi)) {
        return false;
    }
    
    String lockKey = storage.makeKey(KEY_LOCK_PREFIX, shortKey);
    debug_printf("Lock key: %s\n", lockKey.c_str());
    if (!storage.saveInt(lockKey.c_str(), settings.lockRssi)) {
        return false;
    }
    
    String criticalKey = storage.makeKey(KEY_CRITICAL_PREFIX, shortKey);
    debug_printf("Critical key: %s\n", criticalKey.c_str());
    if (!storage.saveInt(criticalKey.c_str(), settings.criticalRssi)) {
        return false;
    }
    
    // Обновляем адрес последнего устройства
    storage.saveString(KEY_LAST_ADDR, deviceAddress);
    
    debug_println("Settings saved successfully");
    debug_println("=== End Saving Settings ===\n");
    
    return true;
}

String DeviceManager::getPassword(const String& deviceAddress) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    if (settings.password.length() > 0) {
        return decryptPassword(settings.password);
    }
    return "";
}

bool DeviceManager::savePassword(const String& deviceAddress, const String& password) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    settings.password = encryptPassword(password);
    return saveDeviceSettings(deviceAddress, settings);
}

bool DeviceManager::getLockState(const String& deviceAddress) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    return settings.isLocked;
}

bool DeviceManager::getLockState() {
    if (currentDeviceAddress.length() == 0) {
        debug_println("No connected device to check lock state");
        return false;
    }
    
    return storage.loadBool(KEY_IS_LOCKED, false);
}

bool DeviceManager::setLockState(const String& deviceAddress, bool locked) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    settings.isLocked = locked;
    
    if (saveDeviceSettings(deviceAddress, settings)) {
        // Обновляем глобальное состояние блокировки
        storage.saveBool(KEY_IS_LOCKED, locked);
        
        debug_printf("Lock state for device %s set to: %s\n", 
                  deviceAddress.c_str(), 
                  locked ? "LOCKED" : "UNLOCKED");
        return true;
    }
    
    return false;
}

bool DeviceManager::setLockState(bool locked) {
    if (currentDeviceAddress.length() == 0) {
        debug_println("No connected device to set lock state");
        return false;
    }
    
    return setLockState(currentDeviceAddress, locked);
}

bool DeviceManager::saveRssiThresholds(const String& deviceAddress, int unlockRssi, int lockRssi, int criticalRssi) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    settings.unlockRssi = unlockRssi;
    settings.lockRssi = lockRssi;
    settings.criticalRssi = criticalRssi;
    return saveDeviceSettings(deviceAddress, settings);
}

String DeviceManager::encryptPassword(const String& password) {
    // Простая XOR шифрация для демонстрации. 
    // В реальном приложении используйте более надежное шифрование.
    String encrypted = "";
    for (size_t i = 0; i < password.length(); i++) {
        encrypted += (char)(password[i] ^ ENCRYPTION_KEY[i % sizeof(ENCRYPTION_KEY)]);
    }
    return encrypted;
}

String DeviceManager::decryptPassword(const String& encrypted) {
    // XOR - симметричное шифрование, поэтому алгоритм дешифрования такой же
    return encryptPassword(encrypted);
}

bool DeviceManager::listStoredDevices() {
    debug_println("\nChecking saved devices:");
    
    // Пока мы не имеем прямого доступа к списку ключей в NVS
    // Это заглушка. В реальной системе нужно будет добавить дополнительную логику
    // для отслеживания сохраненных устройств.
    
    debug_println("Function not fully implemented yet");
    return false;
}

bool DeviceManager::clearAllPasswords() {
    debug_println("Clearing all passwords...");
    // Заглушка, для полной реализации потребуется добавить
    // механизм отслеживания всех сохраненных устройств
    
    debug_println("Function not fully implemented yet");
    return false;
}

bool DeviceManager::clearAllSettings() {
    return storage.clear();
}

void DeviceManager::setCurrentDevice(const String& deviceAddress) {
    currentDeviceAddress = deviceAddress;
    storage.saveString(KEY_LAST_ADDR, deviceAddress);
}

String DeviceManager::getCurrentDevice() {
    return currentDeviceAddress;
}

String DeviceManager::cleanMacAddress(const String& macAddress) {
    String shortKey = macAddress;
    shortKey.replace(":", "");
    
    // Берем последние 8 символов, если адрес длиннее
    if (shortKey.length() > 8) {
        shortKey = shortKey.substring(shortKey.length() - 8);
    }
    
    return shortKey;
} 