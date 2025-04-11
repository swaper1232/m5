#pragma once

#include <Arduino.h>

struct RssiMeasurement {
    int value;
    uint32_t timestamp;
    bool isValid;
};

class RSSIManager {
public:
    static RSSIManager& getInstance() {
        static RSSIManager instance;
        return instance;
    }

    void addMeasurement(int rssi);
    int getAverageRssi() const;
    bool isStable() const;
    int getLastDifference() const;
    String getMovementIndicator() const;
    bool isSignalWeak() const;
    void reset();

    // Константы для настройки
    static const int RSSI_HISTORY_SIZE = 10;
    static const int RSSI_SAMPLES = 10;
    static const int SIGNAL_LOSS_THRESHOLD = -75;
    static const int MOVEMENT_THRESHOLD = 5;
    static const int RSSI_NEAR_THRESHOLD = -50;
    static const int RSSI_FAR_THRESHOLD = -65;

private:
    RSSIManager() {} // Приватный конструктор для синглтона
    
    RssiMeasurement rssiHistory[RSSI_HISTORY_SIZE];
    int rssiValues[RSSI_SAMPLES];
    int rssiHistoryIndex = 0;
    int rssiIndex = 0;
    int validSamples = 0;
    int lastAverageRssi = 0;
    int previousRssi = 0;

    void filterOutliers();
    int calculateAverage() const;
}; 