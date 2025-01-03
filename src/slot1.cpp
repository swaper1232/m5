/*
 * Slot 1 - Рабочая версия с полной инициализацией
 * 
 * Особенности:
 * - Полная инициализация BLE стека при переключении режимов
 * - Корректная работа HID и сканера
 * - Стабильное переподключение
 * - Правильная обработка безопасности
 * 
 * Дата: 2024-01-17
 */

#include <M5StickCPlus2.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "esp_task_wdt.h"
#include "esp_gap_ble_api.h"
#include "NimBLEClient.h"

static NimBLEAdvertisementData advData;
// ... весь остальной код из main.cpp ... 