#include "StorageManager.h"
#include <nvs_flash.h>
#include <esp_err.h>

StorageManager::StorageManager() : is_initialized(false) {
}

StorageManager::~StorageManager() {
    if (is_initialized) {
        nvs_close(nvsHandle);
    }
}

bool StorageManager::initialize(const char* namespace_name) {
    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("Erasing NVS flash...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        Serial.printf("Failed to initialize NVS: %d\n", ret);
        return false;
    }
    
    // Открытие NVS
    ret = nvs_open(namespace_name, NVS_READWRITE, &nvsHandle);
    if (ret != ESP_OK) {
        Serial.printf("Error opening NVS handle: %d\n", ret);
        return false;
    }
    
    is_initialized = true;
    Serial.println("Storage initialized successfully");
    return true;
}

bool StorageManager::saveString(const char* key, const String& value) {
    if (!is_initialized) return false;
    
    // Сначала удаляем ключ, чтобы избежать проблем с разной длиной строк
    removeKey(key);
    
    esp_err_t err = nvs_set_str(nvsHandle, key, value.c_str());
    if (err != ESP_OK) {
        Serial.printf("Error saving string '%s': %d\n", key, err);
        return false;
    }
    
    return commit();
}

bool StorageManager::saveInt(const char* key, int32_t value) {
    if (!is_initialized) return false;
    
    esp_err_t err = nvs_set_i32(nvsHandle, key, value);
    if (err != ESP_OK) {
        Serial.printf("Error saving int '%s': %d\n", key, err);
        return false;
    }
    
    return commit();
}

bool StorageManager::saveUInt(const char* key, uint32_t value) {
    if (!is_initialized) return false;
    
    esp_err_t err = nvs_set_u32(nvsHandle, key, value);
    if (err != ESP_OK) {
        Serial.printf("Error saving uint '%s': %d\n", key, err);
        return false;
    }
    
    return commit();
}

bool StorageManager::saveBool(const char* key, bool value) {
    if (!is_initialized) return false;
    
    esp_err_t err = nvs_set_i8(nvsHandle, key, value ? 1 : 0);
    if (err != ESP_OK) {
        Serial.printf("Error saving bool '%s': %d\n", key, err);
        return false;
    }
    
    return commit();
}

bool StorageManager::saveBytes(const char* key, const void* value, size_t length) {
    if (!is_initialized) return false;
    
    // Сначала удаляем ключ, чтобы избежать проблем с разной длиной данных
    removeKey(key);
    
    esp_err_t err = nvs_set_blob(nvsHandle, key, value, length);
    if (err != ESP_OK) {
        Serial.printf("Error saving bytes '%s': %d\n", key, err);
        return false;
    }
    
    return commit();
}

String StorageManager::loadString(const char* key, const String& default_value) {
    if (!is_initialized) return default_value;
    
    char buffer[512] = {0}; // Максимальный размер строки
    size_t length = sizeof(buffer);
    
    esp_err_t err = nvs_get_str(nvsHandle, key, buffer, &length);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error loading string '%s': %d\n", key, err);
        return default_value;
    }
    
    return (err == ESP_OK) ? String(buffer) : default_value;
}

int32_t StorageManager::loadInt(const char* key, int32_t default_value) {
    if (!is_initialized) return default_value;
    
    int32_t value;
    esp_err_t err = nvs_get_i32(nvsHandle, key, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error loading int '%s': %d\n", key, err);
        return default_value;
    }
    
    return (err == ESP_OK) ? value : default_value;
}

uint32_t StorageManager::loadUInt(const char* key, uint32_t default_value) {
    if (!is_initialized) return default_value;
    
    uint32_t value;
    esp_err_t err = nvs_get_u32(nvsHandle, key, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error loading uint '%s': %d\n", key, err);
        return default_value;
    }
    
    return (err == ESP_OK) ? value : default_value;
}

bool StorageManager::loadBool(const char* key, bool default_value) {
    if (!is_initialized) return default_value;
    
    int8_t value;
    esp_err_t err = nvs_get_i8(nvsHandle, key, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error loading bool '%s': %d\n", key, err);
        return default_value;
    }
    
    return (err == ESP_OK) ? (value != 0) : default_value;
}

size_t StorageManager::loadBytes(const char* key, void* out_value, size_t length) {
    if (!is_initialized) return 0;
    
    size_t actual_length = length;
    esp_err_t err = nvs_get_blob(nvsHandle, key, out_value, &actual_length);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error loading bytes '%s': %d\n", key, err);
        return 0;
    }
    
    return (err == ESP_OK) ? actual_length : 0;
}

bool StorageManager::keyExists(const char* key) {
    if (!is_initialized) return false;
    
    esp_err_t err = nvs_get_i8(nvsHandle, key, nullptr);
    return (err != ESP_ERR_NVS_NOT_FOUND);
}

bool StorageManager::removeKey(const char* key) {
    if (!is_initialized) return false;
    
    esp_err_t err = nvs_erase_key(nvsHandle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error removing key '%s': %d\n", key, err);
        return false;
    }
    
    return commit();
}

bool StorageManager::clear() {
    if (!is_initialized) return false;
    
    esp_err_t err = nvs_erase_all(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error clearing storage: %d\n", err);
        return false;
    }
    
    return commit();
}

bool StorageManager::commit() {
    if (!is_initialized) return false;
    
    esp_err_t err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error committing NVS: %d\n", err);
        return false;
    }
    
    return true;
}

String StorageManager::makeKey(const String& prefix, const String& deviceAddress) {
    // Создаем короткий ключ из адреса устройства (последние 8 символов)
    String shortAddr = deviceAddress;
    if (shortAddr.length() > 8) {
        shortAddr = shortAddr.substring(shortAddr.length() - 8);
    }
    
    // Удаляем двоеточия из MAC-адреса
    shortAddr.replace(":", "");
    
    // Комбинируем префикс и короткий адрес
    return prefix + "_" + shortAddr;
} 