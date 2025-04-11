#pragma once

#include <Arduino.h>

// Глобальная переменная для контроля отладочного вывода
extern bool serialOutputEnabled;

// Функция для отладочного вывода в стиле printf
void debug_printf(const char* format, ...);

// Функция для отладочного вывода в стиле println
void debug_println(const char* text);

// Функция для отладочного вывода в стиле print
void debug_print(const char* text);

// Функция для включения/выключения отладочного вывода
void set_debug_output(bool enabled); 