#ifndef PASSWORD_MANAGER_H
#define PASSWORD_MANAGER_H

#include <Arduino.h>
#include "nvs.h"
#include "esp_err.h"

// Функция для очистки старых паролей, основанная на проверке длины ключа
void clearOldPasswords();

// Функция для получения пароля для устройства по его адресу
String getPasswordForDevice(const String &deviceAddress);

// Функция для сохранения пароля для устройства
void savePasswordForDevice(const String &deviceAddress, const String &password);

inline String getPasswordForDevice(const char* deviceAddress) {
    return getPasswordForDevice(String(deviceAddress));
}

#endif // PASSWORD_MANAGER_H 