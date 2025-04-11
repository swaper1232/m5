#include "PowerManager.h"

// Константы для управления мощностью
const PowerLevel POWER_NEAR_PC = ESP_PWR_LVL_N12;    // -12dBm минимальная
const PowerLevel POWER_LOCKED = ESP_PWR_LVL_N12;     // Меняем на минимальную в блоке
const PowerLevel POWER_RETURNING = ESP_PWR_LVL_P9;   // +9dBm максимальная

// Константы для яркости экрана
const uint8_t BRIGHTNESS_MAX = 100;     // Максимальная яркость
const uint8_t BRIGHTNESS_NORMAL = 50;   // Нормальная яркость
const uint8_t BRIGHTNESS_LOW = 20;      // Пониженная яркость
const uint8_t BRIGHTNESS_MIN = 0;       // Выключенный экран
const uint8_t BRIGHTNESS_MEDIUM = 70;   // Средняя яркость для временного включения экрана
const uint8_t BRIGHTNESS_USB = 100;     // Яркость при USB питании
const uint8_t BRIGHTNESS_BATTERY = 30;  // Яркость от батареи

// Пороги для определения типа питания
const float VOLTAGE_THRESHOLD = -15.0;     // Допустимое падение напряжения
const float DISCONNECT_THRESHOLD = -25.0;   // Порог для определения отключения

// Константы для временного включения экрана
static const unsigned long SCREEN_TIMEOUT = 5000;  // 5 секунд для временного включения

PowerManager::PowerManager(bool enableSerialOutput) : serialOutputEnabled(enableSerialOutput) {
    // Инициализация переменных
    batteryVoltage = M5.Power.getBatteryVoltage();
    batteryLevel = M5.Power.getBatteryLevel();
    
    // Инициализируем историю измерений текущим значением
    for (int i = 0; i < VOLTAGE_HISTORY_SIZE; i++) {
        voltageHistory[i] = batteryVoltage;
    }
    
    maxAverageVoltage = batteryVoltage;
    
    // Устанавливаем начальную яркость в зависимости от типа питания
    bool isUSB = M5.Power.isCharging();
    M5.Display.setBrightness(isUSB ? BRIGHTNESS_USB : BRIGHTNESS_BATTERY);
    
    if (serialOutputEnabled) {
        Serial.printf("PowerManager initialized. Battery: %.2fV, Level: %.1f%%, USB: %s\n",
            batteryVoltage, batteryLevel, isUSB ? "connected" : "disconnected");
    }
}

void PowerManager::updatePowerStatus() {
    // Обновляем базовые параметры
    batteryVoltage = M5.Power.getBatteryVoltage();
    batteryLevel = M5.Power.getBatteryLevel();
    
    // Обновляем историю измерений напряжения
    voltageHistory[historyIndex] = batteryVoltage;
    historyIndex = (historyIndex + 1) % VOLTAGE_HISTORY_SIZE;
    if (historyIndex == 0) historyFilled = true;
    
    static bool wasUSB = true;
    bool isUSB = M5.Power.isCharging();
    
    // Добавим защиту от частых изменений
    static unsigned long lastChange = 0;
    if (millis() - lastChange < 1000) {  // Минимум 1 секунда между изменениями
        return;
    }
    
    if (wasUSB != isUSB) {
        lastChange = millis();
        wasUSB = isUSB;
        
        // Устанавливаем яркость с задержкой
        delay(10);
        M5.Display.setBrightness(isUSB ? BRIGHTNESS_USB : BRIGHTNESS_BATTERY);
        delay(10);
        
        if (serialOutputEnabled) {
            Serial.printf("Power source changed: %s, brightness: %d\n",
                isUSB ? "USB" : "Battery",
                isUSB ? BRIGHTNESS_USB : BRIGHTNESS_BATTERY);
        }
    }
}

float PowerManager::calculateAverageVoltage() {
    float sum = 0;
    int count = historyFilled ? VOLTAGE_HISTORY_SIZE : historyIndex;
    if (count == 0) return 0;
    
    for (int i = 0; i < count; i++) {
        sum += voltageHistory[i];
    }
    return sum / count;
}

bool PowerManager::isUSBConnected() {
    if (!historyFilled && historyIndex < 3) return true;
    
    float avgVoltage = calculateAverageVoltage();
    if (avgVoltage > maxAverageVoltage) {
        maxAverageVoltage = avgVoltage;
    }
    
    float voltageDrop = avgVoltage - maxAverageVoltage;
    
    if (wasConnected) {
        if (voltageDrop < DISCONNECT_THRESHOLD) {
            wasConnected = false;
            return false;
        }
        return true;
    } else {
        if (voltageDrop > VOLTAGE_THRESHOLD) {
            wasConnected = true;
            return true;
        }
        return false;
    }
}

void PowerManager::adjustBrightness(PowerLevel txPower) {
    static uint8_t currentBrightness = BRIGHTNESS_NORMAL;
    uint8_t newBrightness;
    
    // Если подключен USB - не выключаем экран
    if (isUSBConnected()) {
        switch (txPower) {
            case ESP_PWR_LVL_N12:  // Минимальная мощность
                newBrightness = BRIGHTNESS_LOW;  // Минимальная, но не выключаем
                break;
            case ESP_PWR_LVL_N9:
            case ESP_PWR_LVL_N6:
                newBrightness = BRIGHTNESS_LOW;
                break;
            default:
                newBrightness = BRIGHTNESS_NORMAL;
        }
    } else {
        // Обычная логика для работы от батареи
        switch (txPower) {
            case ESP_PWR_LVL_N12:  // Минимальная мощность
                newBrightness = BRIGHTNESS_MIN;  // Выключаем экран
                break;
            case ESP_PWR_LVL_N9:
            case ESP_PWR_LVL_N6:
                newBrightness = BRIGHTNESS_LOW;
                break;
            default:
                newBrightness = BRIGHTNESS_NORMAL;
        }
    }

    if (newBrightness != currentBrightness) {
        if (serialOutputEnabled) {
            Serial.printf("Adjusting brightness: %d -> %d (TX Power: %d, USB: %d)\n", 
                currentBrightness, newBrightness, txPower, isUSBConnected());
        }
        M5.Display.setBrightness(newBrightness);
        currentBrightness = newBrightness;
    }
}

void PowerManager::temporaryScreenOn() {
    screenOnTime = millis();
    screenTemporaryOn = true;
    M5.Display.setBrightness(BRIGHTNESS_MEDIUM);
    
    if (serialOutputEnabled) {
        Serial.println("Temporary screen on activated");
    }
}

void PowerManager::checkTemporaryScreen() {
    if (screenTemporaryOn && (millis() - screenOnTime > SCREEN_TIMEOUT)) {
        screenTemporaryOn = false;
        
        // Возвращаем прежнюю яркость в зависимости от питания
        PowerLevel currentPower = (PowerLevel)NimBLEDevice::getPower();
        
        // Принудительно устанавливаем яркость в зависимости от питания
        if (isUSBConnected()) {
            M5.Display.setBrightness(BRIGHTNESS_USB);
            if (serialOutputEnabled) {
                Serial.printf("Temporary screen timeout - returning to USB level: %d\n", BRIGHTNESS_USB);
            }
        } else {
            // Используем adjustBrightness для установки яркости в зависимости от мощности передатчика
            adjustBrightness(currentPower);
        }
        
        if (serialOutputEnabled) {
            Serial.println("Temporary screen timeout - returning to normal brightness");
        }
    }
} 