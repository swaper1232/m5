#include "NvsUtils.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <Arduino.h> // Для Serial

// Определяем и инициализируем переменные здесь
// (Убираем static и анонимное пространство имен)
nvs_handle_t nvsHandle; // Определение будет найдено линковщиком
const char* NVS_NAMESPACE = "m5kb_v1";
const char* KEY_IS_LOCKED = "is_locked";

// Функция для инициализации NVS и установки начальных значений
void initializeNvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("Erasing NVS flash...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        Serial.printf("Failed to initialize NVS: %d\n", ret);
        // Не закрываем handle здесь, он может быть не инициализирован
    } else {
        Serial.println("NVS initialized successfully");
        ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
        if (ret != ESP_OK) {
            Serial.printf("Error opening NVS handle: %d\n", ret);
        } else {
            Serial.println("Storage initialized successfully");
            // Проверяем есть ли начальные значения, если нужно
            int8_t isLockedCheck;
            esp_err_t checkErr = nvs_get_i8(nvsHandle, KEY_IS_LOCKED, &isLockedCheck);
             if (checkErr == ESP_ERR_NVS_NOT_FOUND) {
                 // Устанавливаем начальное значение (разблокировано)
                 nvs_set_i8(nvsHandle, KEY_IS_LOCKED, 0);
                 nvs_commit(nvsHandle);
                 Serial.println("Initial lock state set to UNLOCKED");
             }
        }
    }
}

// Функция для сохранения глобального состояния блокировки
void saveGlobalLockState(bool locked) {
    esp_err_t err = nvs_set_i8(nvsHandle, KEY_IS_LOCKED, locked ? 1 : 0);
    if (err != ESP_OK) {
        Serial.printf("Error setting global lock state: %d\n", err);
        return;
    }
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error committing global lock state: %d\n", err);
    }
}

// Функция для загрузки глобального состояния блокировки
bool loadGlobalLockState() {
    int8_t locked = 0;
    esp_err_t err = nvs_get_i8(nvsHandle, KEY_IS_LOCKED, &locked);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error getting global lock state: %d\n", err);
    }
    // Если ключ не найден, считаем, что разблокировано
    return (err == ESP_OK && locked != 0);
} 