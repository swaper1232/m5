#include "RSSIManager.h"
#include <algorithm>

void RSSIManager::addMeasurement(int rssi) {
    // Проверка на валидность значения
    if (rssi < -100 || rssi > 0) return;

    // Добавление в историю
    rssiHistory[rssiHistoryIndex] = {
        .value = rssi,
        .timestamp = millis(),
        .isValid = true
    };
    rssiHistoryIndex = (rssiHistoryIndex + 1) % RSSI_HISTORY_SIZE;

    // Добавление в массив для усреднения
    rssiValues[rssiIndex] = rssi;
    rssiIndex = (rssiIndex + 1) % RSSI_SAMPLES;
    if (validSamples < RSSI_SAMPLES) validSamples++;

    // Обновление среднего значения
    previousRssi = lastAverageRssi;
    filterOutliers();
    lastAverageRssi = calculateAverage();
}

void RSSIManager::filterOutliers() {
    if (validSamples < 3) return; // Нужно минимум 3 значения для фильтрации

    // Копируем валидные значения во временный массив
    int tempValues[RSSI_SAMPLES];
    int count = 0;
    for (int i = 0; i < RSSI_SAMPLES; i++) {
        if (rssiValues[i] != 0) {
            tempValues[count++] = rssiValues[i];
        }
    }

    // Сортируем значения
    std::sort(tempValues, tempValues + count);

    // Вычисляем Q1 и Q3
    int q1 = tempValues[count / 4];
    int q3 = tempValues[3 * count / 4];
    int iqr = q3 - q1;
    int lowerBound = q1 - 1.5 * iqr;
    int upperBound = q3 + 1.5 * iqr;

    // Удаляем выбросы
    for (int i = 0; i < RSSI_SAMPLES; i++) {
        if (rssiValues[i] < lowerBound || rssiValues[i] > upperBound) {
            rssiValues[i] = 0;
            validSamples--;
        }
    }
}

int RSSIManager::calculateAverage() const {
    if (validSamples == 0) return lastAverageRssi;

    int sum = 0;
    int count = 0;
    for (int i = 0; i < RSSI_SAMPLES; i++) {
        if (rssiValues[i] != 0) {
            sum += rssiValues[i];
            count++;
        }
    }
    return count > 0 ? sum / count : lastAverageRssi;
}

bool RSSIManager::isStable() const {
    if (validSamples < RSSI_SAMPLES) return false;

    int maxDiff = 0;
    for (int i = 1; i < RSSI_SAMPLES; i++) {
        if (rssiValues[i] != 0 && rssiValues[i-1] != 0) {
            maxDiff = max(maxDiff, abs(rssiValues[i] - rssiValues[i-1]));
        }
    }
    return maxDiff <= MOVEMENT_THRESHOLD;
}

int RSSIManager::getLastDifference() const {
    return lastAverageRssi - previousRssi;
}

String RSSIManager::getMovementIndicator() const {
    int diff = getLastDifference();
    if (diff < -MOVEMENT_THRESHOLD) return "<<";
    if (diff > MOVEMENT_THRESHOLD) return ">>";
    return "==";
}

bool RSSIManager::isSignalWeak() const {
    return lastAverageRssi < SIGNAL_LOSS_THRESHOLD;
}

void RSSIManager::reset() {
    rssiHistoryIndex = 0;
    rssiIndex = 0;
    validSamples = 0;
    lastAverageRssi = 0;
    previousRssi = 0;
    
    for (int i = 0; i < RSSI_HISTORY_SIZE; i++) {
        rssiHistory[i] = {0, 0, false};
    }
    
    for (int i = 0; i < RSSI_SAMPLES; i++) {
        rssiValues[i] = 0;
    }
} 