#pragma once

#include <Arduino.h>
#include <nvs.h>

// Класс для работы с энергонезависимой памятью (NVS)
class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    // Инициализация хранилища
    bool initialize(const char* namespace_name = "m5kb_v1");
    
    // Сохранение различных типов данных
    bool saveString(const char* key, const String& value);
    bool saveInt(const char* key, int32_t value);
    bool saveUInt(const char* key, uint32_t value);
    bool saveBool(const char* key, bool value);
    bool saveBytes(const char* key, const void* value, size_t length);
    
    // Загрузка различных типов данных
    String loadString(const char* key, const String& default_value = "");
    int32_t loadInt(const char* key, int32_t default_value = 0);
    uint32_t loadUInt(const char* key, uint32_t default_value = 0);
    bool loadBool(const char* key, bool default_value = false);
    size_t loadBytes(const char* key, void* out_value, size_t length);
    
    // Проверка существования ключа
    bool keyExists(const char* key);
    
    // Удаление ключа
    bool removeKey(const char* key);
    
    // Очистка всего хранилища
    bool clear();
    
    // Вспомогательные функции
    String makeKey(const String& prefix, const String& deviceAddress);
    
private:
    nvs_handle_t nvsHandle;
    bool is_initialized;
    
    // Фиксация изменений в NVS
    bool commit();
}; 