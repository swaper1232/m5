#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <M5Unified.h>
#include <Arduino.h>

// Глобальный буфер для отрисовки, объявляем как extern
extern M5Canvas* Disbuff;

// Функция обновления экрана
void updateDisplay();

// Функция временного включения экрана (например, при нажатии кнопки)
void temporaryScreenOn();

// Функция проверки таймаута временного включения экрана
void checkTemporaryScreen();

#endif // DISPLAY_MANAGER_H 