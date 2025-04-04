#pragma once

#include <Arduino.h>
#include "DebugUtils.h"

// Структура для хранения измерений RSSI
struct RssiMeasurement {
    int value;                // Значение RSSI
    uint32_t timestamp;       // Временная метка измерения
    bool isValid;             // Флаг валидности измерения
    
    // Конструктор по умолчанию
    RssiMeasurement() : value(0), timestamp(0), isValid(false) {}
    
    // Конструктор с параметрами
    RssiMeasurement(int val, uint32_t time) : 
        value(val), timestamp(time), isValid(true) {}
};

// Класс для обработки RSSI
class RSSIHandler {
public:
    RSSIHandler();
    
    // Добавление и получение измерений
    void addMeasurement(int rssi);
    void addMeasurement(const RssiMeasurement& measurement);
    int getAverageRssi();
    int getLastRssi();
    
    // Анализ стабильности и движения
    bool isRssiStable();
    bool isMovingAway();
    bool isApproaching();
    bool isSignalWeak();
    
    // Получить метаданные об измерениях
    int getValidSamplesCount() { return validSamples; }
    
    // Функции для получения измерений из различных источников
    RssiMeasurement getMeasuredRssi();
    
    // Установка пороговых значений
    void setRssiThresholds(int nearThreshold, int farThreshold, int criticalThreshold);
    
private:
    // Константы для измерения RSSI
    static const int RSSI_SAMPLES = 10;         // Размер буфера измерений
    static const int RSSI_HISTORY_SIZE = 10;    // Размер буфера истории
    
    // Буферы для хранения значений RSSI
    int rssiValues[RSSI_SAMPLES];               // Буфер для фильтрации
    RssiMeasurement rssiHistory[RSSI_HISTORY_SIZE]; // История измерений
    
    // Индексы и счетчики
    int rssiIndex;                              // Текущий индекс в буфере
    int rssiHistoryIndex;                       // Текущий индекс в истории
    int validSamples;                           // Количество валидных измерений
    
    // Усредненные значения
    float exponentialAverage;                   // Экспоненциальное среднее
    bool exponentialAverageInitialized;         // Флаг инициализации экспоненциального среднего
    int lastAverageRssi;                        // Последнее среднее значение
    int lastRssi;                               // Последнее измеренное значение
    
    // Пороговые значения RSSI
    int rssiNearThreshold;                      // Порог близкого расстояния
    int rssiFarThreshold;                       // Порог дальнего расстояния
    int rssiCriticalThreshold;                  // Критический порог
    
    // Параметры фильтрации
    static constexpr float ALPHA = 0.3;         // Коэффициент сглаживания (0.3 = 30% веса для нового значения)
    
    // Вспомогательные методы
    void processNewMeasurement(int rssi);
}; 