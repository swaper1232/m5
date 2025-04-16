#ifndef DEVICE_UTILS_H
#define DEVICE_UTILS_H

#include <Arduino.h>

// Вспомогательная функция для получения короткого ключа из MAC-адреса.
// Функция возвращает очищенный MAC-адрес в виде строки.
String getShortKey(const char* macAddress);

#endif // DEVICE_UTILS_H 