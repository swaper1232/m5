#pragma once

#include <Arduino.h> // Для String

/**
 * @brief Очищает MAC-адрес, оставляя только последние 6 символов (3 октета).
 * 
 * @param macAddress MAC-адрес в виде C-строки (например, "AA:BB:CC:DD:EE:FF").
 * @return String Короткий ключ устройства (например, "DDEEFF").
 */
String cleanMacAddress(const char* macAddress);

/**
 * @brief Проверяет, является ли пароль зашифрованным в старом формате
 * 
 * @param encrypted Зашифрованный пароль
 * @return bool true если пароль в старом формате, false если в новом
 */
bool isLegacyPassword(const String& encrypted);

/**
 * @brief Конвертирует пароль из старого формата в новый
 * 
 * @param oldEncrypted Пароль в старом формате
 * @return String Пароль в новом формате
 */
String migratePassword(const String& oldEncrypted);

/**
 * @brief Шифрует строку пароля с использованием XOR.
 * 
 * @param password Исходный пароль.
 * @return String Зашифрованный пароль (в виде hex-строки).
 */
String encryptPassword(const String& password);

/**
 * @brief Дешифрует строку пароля, зашифрованную с использованием XOR.
 * 
 * @param encrypted Зашифрованный пароль (в виде hex-строки).
 * @return String Исходный пароль.
 */
String decryptPassword(const String& encrypted);

// Сюда будем добавлять объявления других функций по мере переноса 