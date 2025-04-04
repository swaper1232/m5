#include "LockStateManager.h"
#include <NimBLEDevice.h>

// Конструктор класса
LockStateManager::LockStateManager(StorageManager& storage, RSSIHandler& rssiHandler, DeviceManager& deviceManager) :
    storage(storage),
    rssiHandler(rssiHandler),
    deviceManager(deviceManager),
    currentState(NORMAL),
    lastStateChangeTime(0),
    movementStartTime(0),
    consecutiveUnlockSamples(0),
    consecutiveLockSamples(0)
{
}

// Инициализация
void LockStateManager::initialize() {
    // Загружаем текущее состояние блокировки
    bool isLocked = deviceManager.getLockState();
    currentState = isLocked ? LOCKED : NORMAL;
    
    debug_printf("Lock state initialized: %s\n", isLocked ? "LOCKED" : "NORMAL");
}

// Получение текущего состояния
DeviceState LockStateManager::getCurrentState() {
    return currentState;
}

// Установка текущего состояния
void LockStateManager::setCurrentState(DeviceState state) {
    if (currentState != state) {
        debug_printf("Changing state: %d -> %d\n", currentState, state);
        currentState = state;
        lastStateChangeTime = millis();
    }
}

// Блокировка компьютера
bool LockStateManager::lockComputer() {
    debug_println("\n=== Locking Computer ===");
    
    // Сохраняем состояние блокировки
    bool success = deviceManager.setLockState(true);
    
    if (success) {
        setCurrentState(LOCKED);
        resetConsecutiveSamples();
        debug_println("Computer locked successfully");
    } else {
        debug_println("Failed to lock computer");
    }
    
    debug_println("=== Lock Operation End ===\n");
    return success;
}

// Разблокировка компьютера
bool LockStateManager::unlockComputer() {
    debug_println("\n=== Unlocking Computer ===");
    
    // Проверяем, подключено ли устройство
    String currentDevice = deviceManager.getCurrentDevice();
    if (currentDevice.length() == 0) {
        debug_println("No connected device to unlock");
        return false;
    }
    
    // Получаем пароль для текущего устройства
    String password = deviceManager.getPassword(currentDevice);
    if (password.length() == 0) {
        debug_println("No password set for device");
        return false;
    }
    
    // Эмулируем ввод пароля через BLE HID
    // Этот код нужно адаптировать под конкретную реализацию HID
    debug_println("Emulating keyboard input...");
    
    // Сбрасываем состояние блокировки
    bool success = deviceManager.setLockState(false);
    
    if (success) {
        setCurrentState(NORMAL);
        resetConsecutiveSamples();
        debug_println("Computer unlocked successfully");
    } else {
        debug_println("Failed to unlock computer");
    }
    
    debug_println("=== Unlock Operation End ===\n");
    return success;
}

// Обновление состояния на основе RSSI измерений
void LockStateManager::updateState() {
    // Получаем текущий RSSI
    int currentRssi = rssiHandler.getAverageRssi();
    
    // Проверяем, можно ли изменить состояние
    bool canChange = canChangeState();
    
    // Проверяем стабильность сигнала
    bool stableSignal = rssiHandler.isRssiStable();
    
    // Логика обновления состояния на основе RSSI и текущего состояния
    switch (currentState) {
        case NORMAL:
            // Проверяем, удаляется ли пользователь
            if (rssiHandler.isMovingAway()) {
                movementStartTime = millis();
                setCurrentState(MOVING_AWAY);
                debug_println("State changed: NORMAL -> MOVING_AWAY");
            }
            break;
            
        case MOVING_AWAY:
            // Если прошло достаточно времени движения, блокируем компьютер
            if (millis() - movementStartTime > MOVEMENT_TIME) {
                if (canChange && stableSignal) {
                    lockComputer();
                    debug_println("State changed: MOVING_AWAY -> LOCKED (timed)");
                }
            } 
            // Если пользователь перестал удаляться, возвращаемся в NORMAL
            else if (!rssiHandler.isMovingAway()) {
                setCurrentState(NORMAL);
                debug_println("State changed: MOVING_AWAY -> NORMAL (movement stopped)");
            }
            break;
            
        case LOCKED:
            // Проверяем, приближается ли пользователь
            if (currentRssi > -50) { // Значение порога может быть настраиваемым
                consecutiveUnlockSamples++;
                
                // Для разблокировки требуем больше последовательных измерений
                int requiredSamples = CONSECUTIVE_SAMPLES_NEEDED + 2;
                
                if (consecutiveUnlockSamples >= requiredSamples && canChange && stableSignal) {
                    unlockComputer();
                    debug_println("State changed: LOCKED -> NORMAL (unlocked)");
                }
            } else {
                consecutiveUnlockSamples = 0;
            }
            break;
            
        case APPROACHING:
            // Если пользователь достаточно близко, разблокируем компьютер
            if (currentRssi > -50) { // Значение порога может быть настраиваемым
                if (canChange && stableSignal) {
                    unlockComputer();
                    debug_println("State changed: APPROACHING -> NORMAL (close enough)");
                }
            } 
            // Если пользователь перестал приближаться, возвращаемся в LOCKED
            else if (!rssiHandler.isApproaching()) {
                setCurrentState(LOCKED);
                debug_println("State changed: APPROACHING -> LOCKED (approach stopped)");
            }
            break;
    }
}

// Проверка, можно ли изменить состояние
bool LockStateManager::canChangeState() {
    return (millis() - lastStateChangeTime) > STATE_CHANGE_DELAY;
}

// Сброс счетчиков последовательных измерений
void LockStateManager::resetConsecutiveSamples() {
    consecutiveUnlockSamples = 0;
    consecutiveLockSamples = 0;
} 