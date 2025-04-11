#include "DebugUtils.h"
#include <stdarg.h>

// Определение глобальной переменной
bool serialOutputEnabled = true;

// Функция отладочного вывода в стиле printf
void debug_printf(const char* format, ...) {
    if (!serialOutputEnabled) return;
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Serial.print(buffer);
}

// Функция отладочного вывода в стиле println
void debug_println(const char* text) {
    if (serialOutputEnabled) {
        Serial.println(text);
    }
}

// Функция отладочного вывода в стиле print
void debug_print(const char* text) {
    if (serialOutputEnabled) {
        Serial.print(text);
    }
}

// Функция для включения/выключения отладочного вывода
void set_debug_output(bool enabled) {
    serialOutputEnabled = enabled;
    
    if (serialOutputEnabled) {
        Serial.println("Debug output enabled");
    } else {
        Serial.println("Debug output disabled");
    }
} 