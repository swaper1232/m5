#include "RSSIHandler.h"
#include <NimBLEDevice.h>

// Конструктор класса
RSSIHandler::RSSIHandler() :
    rssiIndex(0),
    rssiHistoryIndex(0),
    validSamples(0),
    exponentialAverage(0),
    exponentialAverageInitialized(false),
    lastAverageRssi(0),
    lastRssi(0),
    rssiNearThreshold(-45),     // По умолчанию -45 dBm
    rssiFarThreshold(-60),      // По умолчанию -60 dBm
    rssiCriticalThreshold(-75)  // По умолчанию -75 dBm
{
    // Инициализация массивов
    for (int i = 0; i < RSSI_SAMPLES; i++) {
        rssiValues[i] = 0;
    }
    
    for (int i = 0; i < RSSI_HISTORY_SIZE; i++) {
        rssiHistory[i] = RssiMeasurement();
    }
}

// Добавление нового измерения RSSI
void RSSIHandler::addMeasurement(int rssi) {
    // Проверка на валидность измерения
    if (rssi < -100 || rssi > 0) return;  // Отбрасываем невалидные значения
    
    // Сохраняем значение в буфере
    processNewMeasurement(rssi);
}

// Добавление нового измерения из структуры RssiMeasurement
void RSSIHandler::addMeasurement(const RssiMeasurement& measurement) {
    if (!measurement.isValid) return;
    
    // Сохраняем в истории измерений
    rssiHistory[rssiHistoryIndex] = measurement;
    rssiHistoryIndex = (rssiHistoryIndex + 1) % RSSI_HISTORY_SIZE;
    
    // Обрабатываем значение RSSI
    processNewMeasurement(measurement.value);
    
    // Отладочный вывод, но не слишком часто
    static unsigned long lastRssiDebug = 0;
    if (millis() - lastRssiDebug >= 5000) {
        lastRssiDebug = millis();
        debug_printf("\nRSSI: %d dBm (filtered avg: %d)\n", 
            measurement.value, lastAverageRssi);
    }
}

// Внутренний метод для обработки нового значения RSSI
void RSSIHandler::processNewMeasurement(int rssi) {
    // Сохраняем последнее измеренное значение
    lastRssi = rssi;
    
    // Добавляем в буфер для фильтрации
    rssiValues[rssiIndex] = rssi;
    rssiIndex = (rssiIndex + 1) % RSSI_SAMPLES;
    if (validSamples < RSSI_SAMPLES) validSamples++;
    
    // Пересчитываем среднее значение
    lastAverageRssi = getAverageRssi();
}

// Получение сглаженного среднего значения RSSI
int RSSIHandler::getAverageRssi() {
    if (validSamples == 0) return 0;
    
    // Копируем значения в отдельный массив для сортировки
    int sortedValues[RSSI_SAMPLES];
    int validCount = 0;
    
    // Копируем только валидные значения
    for (int i = 0; i < RSSI_SAMPLES; i++) {
        if (rssiValues[i] != 0) {  // Пропускаем начальные нули
            sortedValues[validCount++] = rssiValues[i];
        }
    }
    
    // Если недостаточно измерений, возвращаем простое среднее
    if (validCount < 4) {
        int sum = 0;
        for (int i = 0; i < validCount; i++) {
            sum += sortedValues[i];
        }
        return validCount > 0 ? sum / validCount : 0;
    }
    
    // Сортируем значения
    for (int i = 0; i < validCount - 1; i++) {
        for (int j = i + 1; j < validCount; j++) {
            if (sortedValues[i] > sortedValues[j]) {
                int temp = sortedValues[i];
                sortedValues[i] = sortedValues[j];
                sortedValues[j] = temp;
            }
        }
    }
    
    // Отбрасываем 20% крайних значений (по 10% с каждой стороны)
    int skipCount = validCount / 10;
    int sum = 0;
    int count = 0;
    
    // Суммируем только средние значения
    for (int i = skipCount; i < validCount - skipCount; i++) {
        sum += sortedValues[i];
        count++;
    }
    
    // Вычисляем среднее с отброшенными выбросами
    int filteredAverage = count > 0 ? sum / count : 0;
    
    // Применяем экспоненциальное скользящее среднее для сглаживания
    if (!exponentialAverageInitialized) {
        exponentialAverage = filteredAverage;
        exponentialAverageInitialized = true;
    } else {
        exponentialAverage = ALPHA * filteredAverage + (1 - ALPHA) * exponentialAverage;
    }
    
    return (int)exponentialAverage;
}

// Получение последнего измеренного значения
int RSSIHandler::getLastRssi() {
    return lastRssi;
}

// Проверка стабильности сигнала
bool RSSIHandler::isRssiStable() {
    if (validSamples < RSSI_SAMPLES / 2) {
        return false;  // Недостаточно измерений
    }
    
    // Вычисляем стандартное отклонение
    int mean = getAverageRssi();
    float sumSquaredDiff = 0;
    int count = 0;
    
    for (int i = 0; i < RSSI_SAMPLES; i++) {
        if (rssiValues[i] != 0) {
            float diff = rssiValues[i] - mean;
            sumSquaredDiff += diff * diff;
            count++;
        }
    }
    
    if (count < 4) return false;  // Недостаточно измерений
    
    float variance = sumSquaredDiff / count;
    float stdDev = sqrt(variance);
    
    // Сигнал считается стабильным, если стандартное отклонение меньше порога
    // Увеличиваем порог стабильности, так как RSSI естественно колеблется даже при неподвижном устройстве
    const float STABILITY_THRESHOLD = 6.0;  // dBm (было 3.0)
    bool isStable = stdDev < STABILITY_THRESHOLD;
    
    // Отладочная информация, но не слишком часто
    static unsigned long lastStabilityCheck = 0;
    if (millis() - lastStabilityCheck >= 5000) {
        lastStabilityCheck = millis();
        debug_printf("RSSI stability: StdDev=%.2f, Stable=%s\n", 
            stdDev, isStable ? "YES" : "NO");
    }
    
    return isStable;
}

// Проверка, удаляется ли пользователь
bool RSSIHandler::isMovingAway() {
    if (validSamples < RSSI_SAMPLES / 2) return false;
    
    // Вычисляем среднее по последним измерениям и сравниваем с предыдущими
    int currentAvg = 0;
    int previousAvg = 0;
    int count = 0;
    
    // Смотрим последние 3 измерения
    for (int i = 0; i < 3; i++) {
        int idx = (rssiHistoryIndex - 1 - i + RSSI_HISTORY_SIZE) % RSSI_HISTORY_SIZE;
        if (rssiHistory[idx].isValid) {
            currentAvg += rssiHistory[idx].value;
            count++;
        }
    }
    
    if (count == 0) return false;
    currentAvg /= count;
    
    // Смотрим предыдущие 3 измерения
    count = 0;
    for (int i = 3; i < 6; i++) {
        int idx = (rssiHistoryIndex - 1 - i + RSSI_HISTORY_SIZE) % RSSI_HISTORY_SIZE;
        if (rssiHistory[idx].isValid) {
            previousAvg += rssiHistory[idx].value;
            count++;
        }
    }
    
    if (count == 0) return false;
    previousAvg /= count;
    
    // Считаем, что пользователь удаляется, если текущий RSSI ниже предыдущего
    // и разница больше порога
    const int MOVEMENT_THRESHOLD = 5;  // dBm
    return (currentAvg < previousAvg - MOVEMENT_THRESHOLD);
}

// Проверка, приближается ли пользователь
bool RSSIHandler::isApproaching() {
    if (validSamples < RSSI_SAMPLES / 2) return false;
    
    // Вычисляем среднее по последним измерениям и сравниваем с предыдущими
    int currentAvg = 0;
    int previousAvg = 0;
    int count = 0;
    
    // Смотрим последние 3 измерения
    for (int i = 0; i < 3; i++) {
        int idx = (rssiHistoryIndex - 1 - i + RSSI_HISTORY_SIZE) % RSSI_HISTORY_SIZE;
        if (rssiHistory[idx].isValid) {
            currentAvg += rssiHistory[idx].value;
            count++;
        }
    }
    
    if (count == 0) return false;
    currentAvg /= count;
    
    // Смотрим предыдущие 3 измерения
    count = 0;
    for (int i = 3; i < 6; i++) {
        int idx = (rssiHistoryIndex - 1 - i + RSSI_HISTORY_SIZE) % RSSI_HISTORY_SIZE;
        if (rssiHistory[idx].isValid) {
            previousAvg += rssiHistory[idx].value;
            count++;
        }
    }
    
    if (count == 0) return false;
    previousAvg /= count;
    
    // Считаем, что пользователь приближается, если текущий RSSI выше предыдущего
    // и разница больше порога
    const int MOVEMENT_THRESHOLD = 5;  // dBm
    return (currentAvg > previousAvg + MOVEMENT_THRESHOLD);
}

// Проверка, слабый ли сигнал
bool RSSIHandler::isSignalWeak() {
    return (lastAverageRssi < rssiCriticalThreshold);
}

// Установка пороговых значений RSSI
void RSSIHandler::setRssiThresholds(int nearThreshold, int farThreshold, int criticalThreshold) {
    rssiNearThreshold = nearThreshold;
    rssiFarThreshold = farThreshold;
    rssiCriticalThreshold = criticalThreshold;
    
    debug_printf("RSSI thresholds set: near=%d, far=%d, critical=%d\n", 
        rssiNearThreshold, rssiFarThreshold, rssiCriticalThreshold);
}

// Получение измерения RSSI из различных источников
RssiMeasurement RSSIHandler::getMeasuredRssi() {
    RssiMeasurement measurement;
    measurement.timestamp = millis();
    
    debug_println("\n=== RSSI Measurement Start ===");
    
    // Пытаемся получить RSSI через NimBLE API
    NimBLEServer* bleServer = NimBLEDevice::getServer();
    if (bleServer && bleServer->getConnectedCount() > 0) {
        debug_println("Trying to get RSSI from connected client...");
        
        // Метод 1: Проверяем callback значение RSSI
        if (lastAverageRssi != 0 && lastAverageRssi < 0) {
            measurement.value = lastAverageRssi;
            measurement.isValid = true;
            debug_println("✓ Using callback RSSI value");
            debug_printf("RSSI: %d dBm\n", measurement.value);
            return measurement;
        }
        
        // Метод 2: Пытаемся создать клиент и запросить RSSI
        NimBLEClient* pClient = NimBLEDevice::createClient();
        if (pClient) {
            int rssi = pClient->getRssi();
            debug_printf("Client RSSI: %d\n", rssi);
            
            if (rssi != 0 && rssi < 0) {
                measurement.value = rssi;
                measurement.isValid = true;
                debug_println("✓ Valid RSSI from client");
            }
            
            NimBLEDevice::deleteClient(pClient);
            
            if (measurement.isValid) {
                return measurement;
            }
        }
        
        debug_println("✗ Failed to get valid RSSI");
    }
    
    debug_println("=== RSSI Measurement End ===");
    return measurement;
} 