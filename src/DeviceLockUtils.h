#pragma once

#include <Arduino.h> // Для String и bool

/**
 * @brief Сохраняет состояние блокировки для указанного устройства
 * 
 * @param deviceAddress MAC-адрес устройства
 * @param locked true - устройство заблокировано, false - разблокировано
 */
static void saveDeviceLockState(const String& deviceAddress, bool locked);

/**
 * @brief Загружает состояние блокировки для указанного устройства
 * 
 * @param deviceAddress MAC-адрес устройства
 * @return bool true - устройство было заблокировано, false - разблокировано или ключ не найден
 */
static bool loadDeviceLockState(const String& deviceAddress); 