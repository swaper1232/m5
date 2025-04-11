#include "DeviceSettingsUtils.h"
#include <Arduino.h> // Для String, toupper

// Анонимное пространство имен для внутренних констант и переменных
namespace {
    // Ключ шифрования (32 байта)
    const uint8_t ENCRYPTION_KEY[] = {
        0x89, 0x4E, 0x1C, 0xE7, 0x3A, 0x5D, 0x2B, 0xF8,
        0x6C, 0x91, 0x0D, 0xB4, 0x7F, 0xE2, 0x9A, 0x3C,
        0x5E, 0x8D, 0x1B, 0xF4, 0x6A, 0x2C, 0x9E, 0x0B,
        0x7D, 0x4F, 0xA3, 0xE5, 0x8C, 0x1D, 0xB6, 0x3F
    };
} // namespace

// Реализация функции cleanMacAddress
String cleanMacAddress(const char* macAddress) {
    if (!macAddress || strlen(macAddress) == 0) {
        return "";
    }

    // Убираем двоеточия и преобразуем в верхний регистр
    String result = "";
    for (size_t i = 0; macAddress[i] != '\0'; i++) {
        if (macAddress[i] != ':') {
            result += (char)toupper(macAddress[i]);
        }
    }
    
    // Берем последние 6 символов
    if (result.length() > 6) {
        result = result.substring(result.length() - 6);
    }
    
    return result;
}

// Проверка формата пароля
bool isLegacyPassword(const String& encrypted) {
    if (encrypted.length() == 0) return false;
    
    // В старом формате каждый символ - это один байт
    // В новом формате каждый байт представлен двумя hex-цифрами
    for (size_t i = 0; i < encrypted.length(); i++) {
        if ((uint8_t)encrypted[i] > 127 || !isprint(encrypted[i])) {
            return true; // Найден бинарный символ - старый формат
        }
    }
    return false;
}

// Конвертация старого формата в новый
String migratePassword(const String& oldEncrypted) {
    String newEncrypted = "";
    for (size_t i = 0; i < oldEncrypted.length(); i++) {
        uint8_t c = (uint8_t)oldEncrypted[i];
        char hex[3];
        sprintf(hex, "%02X", c);
        newEncrypted += hex;
    }
    return newEncrypted;
}

// Функция шифрования пароля
String encryptPassword(const String& password) {
    String encrypted = "";
    for (size_t i = 0; i < password.length(); i++) {
        uint8_t c = (uint8_t)password[i];
        // Простой XOR с ключом для обратной совместимости
        c = c ^ ENCRYPTION_KEY[i % sizeof(ENCRYPTION_KEY)];
        char hex[3];
        sprintf(hex, "%02X", c);
        encrypted += hex;
    }
    return encrypted;
}

// Функция дешифрования пароля
String decryptPassword(const String& encrypted) {
    // Если пароль в старом формате, просто применяем XOR
    if (isLegacyPassword(encrypted)) {
        String decrypted = "";
        for (size_t i = 0; i < encrypted.length(); i++) {
            uint8_t c = (uint8_t)encrypted[i];
            c = c ^ ENCRYPTION_KEY[i % sizeof(ENCRYPTION_KEY)];
            decrypted += (char)c;
        }
        return decrypted;
    }
    
    // Для нового формата (hex)
    String decrypted = "";
    if (encrypted.length() % 2 != 0) {
        return ""; // Некорректная длина
    }
    
    for (size_t i = 0; i < encrypted.length(); i += 2) {
        // Проверяем корректность hex
        if (!isxdigit(encrypted[i]) || !isxdigit(encrypted[i+1])) {
            return "";
        }
        
        // Преобразуем из hex в байт
        char hex[3] = {encrypted[i], encrypted[i+1], 0};
        uint8_t c = strtol(hex, NULL, 16);
        
        // Применяем XOR с ключом
        size_t pos = i/2; // Позиция в исходной строке
        c = c ^ ENCRYPTION_KEY[pos % sizeof(ENCRYPTION_KEY)];
        
        decrypted += (char)c;
    }
    return decrypted;
}

// Сюда будем добавлять реализации других функций 
// Сюда будем добавлять реализации других функций 