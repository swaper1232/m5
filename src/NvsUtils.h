#pragma once

#include <Arduino.h> // Для bool
#include <nvs.h>       // Добавляем для nvs_handle_t

// Объявляем переменные как extern, чтобы они были видны в main.cpp
extern nvs_handle_t nvsHandle;
extern const char* NVS_NAMESPACE;
extern const char* KEY_IS_LOCKED;

/**
 * @brief Инициализирует NVS (Non-Volatile Storage).
 * Открывает пространство имен NVS_NAMESPACE и устанавливает начальное
 * состояние блокировки в false (разблокировано), если оно еще не установлено.
 */
void initializeNvs();

/**
 * @brief Сохраняет глобальное состояние блокировки в NVS.
 * @param locked true, если заблокировано, false, если разблокировано.
 */
void saveGlobalLockState(bool locked);

/**
 * @brief Загружает глобальное состояние блокировки из NVS.
 * @return true, если заблокировано, false, если разблокировано или ключ не найден.
 */
bool loadGlobalLockState(); 