#pragma once

#include <Arduino.h>
#include "DebugUtils.h"
#include "StorageManager.h"
#include "RSSIHandler.h"
#include "DeviceManager.h"

// Перечисление для состояний устройства
enum DeviceState {
    NORMAL,         // Обычный режим
    MOVING_AWAY,    // Движение от компьютера
    LOCKED,         // Компьютер заблокирован
    APPROACHING     // Приближение к компьютеру
};

// Класс для управления состоянием блокировки
class LockStateManager {
public:
    LockStateManager(StorageManager& storage, RSSIHandler& rssiHandler, DeviceManager& deviceManager);
    
    // Инициализация и настройка
    void initialize();
    
    // Получение/установка текущего состояния
    DeviceState getCurrentState();
    void setCurrentState(DeviceState state);
    
    // Методы для изменения состояния
    bool lockComputer();
    bool unlockComputer();
    
    // Проверка и обновление состояния
    void updateState();
    
    // Проверка, можно ли изменить состояние
    bool canChangeState();
    
    // Сброс счетчиков последовательных измерений
    void resetConsecutiveSamples();
    
private:
    // Ссылки на зависимые объекты
    StorageManager& storage;
    RSSIHandler& rssiHandler;
    DeviceManager& deviceManager;
    
    // Текущее состояние устройства
    DeviceState currentState;
    
    // Время и счетчики для стабилизации изменений состояния
    unsigned long lastStateChangeTime;  // Время последнего изменения состояния
    unsigned long movementStartTime;    // Время начала движения
    int consecutiveUnlockSamples;       // Счетчик последовательных измерений для разблокировки
    int consecutiveLockSamples;         // Счетчик последовательных измерений для блокировки
    
    // Константы
    static const unsigned long STATE_CHANGE_DELAY = 20000;  // 20 секунд между изменениями состояния
    static const int CONSECUTIVE_SAMPLES_NEEDED = 3;        // Сколько последовательных измерений нужно для изменения состояния
    static const int MOVEMENT_TIME = 3000;                  // Время движения для блокировки (3 секунды)
}; 