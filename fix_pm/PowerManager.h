#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <M5Unified.h>
#include <NimBLEDevice.h> // Для esp_power_level_t и констант ESP_PWR_LVL_xxx

// Перечисление для уровней мощности BLE передатчика
// Эти константы используются в NimBLEDevice и соответствуют ESP-IDF
typedef esp_power_level_t PowerLevel;

// Константы для управления мощностью (из main.cpp)
extern const PowerLevel POWER_NEAR_PC;    // -12dBm минимальная мощность рядом с ПК
extern const PowerLevel POWER_LOCKED;     // Мощность в состоянии блокировки
extern const PowerLevel POWER_RETURNING;  // +9dBm максимальная для возвращения сигнала

// Константы для яркости экрана
extern const uint8_t BRIGHTNESS_MAX;      // Максимальная яркость
extern const uint8_t BRIGHTNESS_NORMAL;   // Нормальная яркость
extern const uint8_t BRIGHTNESS_LOW;      // Пониженная яркость
extern const uint8_t BRIGHTNESS_MIN;      // Выключенный экран
extern const uint8_t BRIGHTNESS_MEDIUM;   // Средняя яркость для временного включения экрана
extern const uint8_t BRIGHTNESS_USB;      // Яркость при USB питании
extern const uint8_t BRIGHTNESS_BATTERY;  // Яркость от батареи

// Пороги для определения типа питания
extern const float VOLTAGE_THRESHOLD;     // Допустимое падение напряжения
extern const float DISCONNECT_THRESHOLD;  // Порог для определения отключения

/**
 * @brief Класс для управления питанием устройства
 */
class PowerManager {
private:
    static const int VOLTAGE_HISTORY_SIZE = 10;
    float voltageHistory[VOLTAGE_HISTORY_SIZE] = {0};
    int historyIndex = 0;
    bool historyFilled = false;
    float maxAverageVoltage = 0;
    bool wasConnected = true;
    float batteryVoltage = 0;
    float batteryLevel = 0;
    bool serialOutputEnabled = true;
    
    // Временное включение экрана
    unsigned long screenOnTime = 0;
    bool screenTemporaryOn = false;
    
public:
    PowerManager(bool enableSerialOutput = true);
    
    /**
     * @brief Обновляет состояние питания устройства
     */
    void updatePowerStatus();
    
    /**
     * @brief Вычисляет среднее напряжение батареи
     * @return float Среднее напряжение
     */
    float calculateAverageVoltage();
    
    /**
     * @brief Проверяет, подключено ли устройство к USB
     * @return bool true, если подключено к USB
     */
    bool isUSBConnected();
    
    /**
     * @brief Регулирует яркость экрана в зависимости от мощности передатчика
     * @param txPower Мощность BLE передатчика
     */
    void adjustBrightness(PowerLevel txPower);
    
    /**
     * @brief Временно включает экран на среднюю яркость
     */
    void temporaryScreenOn();
    
    /**
     * @brief Проверяет и выключает временно включенный экран
     */
    void checkTemporaryScreen();
    
    /**
     * @brief Получает текущее напряжение батареи
     * @return float Напряжение батареи
     */
    float getBatteryVoltage() const { return batteryVoltage; }
    
    /**
     * @brief Получает текущий уровень заряда батареи
     * @return float Уровень заряда (0-100%)
     */
    float getBatteryLevel() const { return batteryLevel; }
};

#endif // POWER_MANAGER_H 