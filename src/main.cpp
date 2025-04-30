#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "esp_task_wdt.h"
#include "esp_gap_ble_api.h"
#include "NimBLEClient.h"
#include <M5Unified.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "NvsUtils.h"
#include "DeviceSettingsUtils.h" // Добавляем include для нового модуля
#include "device_utils.h" // Добавляем для функции getShortKey
#include "password_manager.h"
#include "DeviceLockUtils.h" // Добавляем для функций saveDeviceLockState/loadDeviceLockState

// Глобальные определения для длительного нажатия кнопки A
static unsigned long btnAPressStart = 0;
#ifndef LONG_PRESS_DURATION
#define LONG_PRESS_DURATION 2000
#endif

// Добавляем эти строки сразу после включений
#define RSSI_HISTORY_SIZE 10

struct RssiMeasurement {
    int value;
    uint32_t timestamp;
    bool isValid;
};

static RssiMeasurement rssiHistory[RSSI_HISTORY_SIZE] = {};
static int rssiHistoryIndex = 0;
static int lastAverageRssi = 0;

// В начале файла после включений, до определения переменных

// Структура для хранения настроек устройства
struct DeviceSettings {
    int unlockRssi;     // Минимальный RSSI для разблокировки
    int lockRssi;       // RSSI для блокировки
    String password;    // Пароль (зашифрованный)
};

void unlockComputer();
void lockComputer();
// String getPasswordForDevice(const String& deviceAddress);
// void savePasswordForDevice(const String& deviceAddress, const String& password);
DeviceSettings getDeviceSettings(const String& deviceAddress);
void saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings);

bool isRssiStable();
void clearAllPreferences();
// String cleanMacAddress(const char* macAddress);  // Удаляем прототип
void temporaryScreenOn();
void checkTemporaryScreen();
// String getDevicePassword(const String& shortKey);
void updateCurrentShortKey(const char* deviceAddress);
// Добавляем прототип новой функции
void clearOldPasswords();

// Глобальные переменные и определения
static const int RSSI_FAR_THRESHOLD = -65;

// В начале файла после всех включений и перед функциями

// Добавляем константы для RSSI по умолчанию
#define DEFAULT_LOCK_RSSI -60    // Порог RSSI для блокировки по умолчанию
#define DEFAULT_UNLOCK_RSSI -45  // Порог RSSI для разблокировки по умолчанию

// Динамические пороги, могут обновляться при длительном нажатии кнопки A
static int dynamicLockThreshold = DEFAULT_LOCK_RSSI;
static int dynamicUnlockThreshold = DEFAULT_UNLOCK_RSSI;

// Добавляем глобальную переменную для управления отладочным выводом
static bool serialOutputEnabled = true;

// В начало файла после включения библиотек
static const char* KEY_LAST_ADDR = "last_addr";
static const char* KEY_PASSWORD = "password";  // Добавляем константу для пароля
static const char* KEY_LOCK_STATE_PREFIX = "locked_";   // Префикс для ключей состояния блокировки по устройству

// Добавляем прототип функции
void lockComputer();

struct {
    bool connected = false;
    uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
    std::string address = "";
} connection_info;

static NimBLEAdvertisementData advData;
static NimBLEServer* bleServer = nullptr;
static NimBLEHIDDevice* hid;
static NimBLECharacteristic* input;
static NimBLECharacteristic* output;
static bool connected = false;
static M5Canvas* Disbuff = nullptr;  // Буфер для отрисовки
static int16_t lastReceivedRssi = 0;
static String lastMovement = "==";

// Добавим переменную для режима работы
static bool scanMode = false;

// Добавим переменную для хранения адреса подключенного устройства
static std::string connectedDeviceAddress = "";

// Константы для определения движения
static const int MOVEMENT_SAMPLES = 10;     // Сколько измерений для определения движения
static const int MOVEMENT_THRESHOLD = 5;    // Порог изменения RSSI для движения (dBm)
static const int MOVEMENT_TIME = 3000;      // Время движения для блокировки (3 секунды)

// Константы для определения потери сигнала
static const int SIGNAL_LOSS_THRESHOLD = -75;  // dBm, критический уровень сигнала
static const int SIGNAL_LOSS_TIME = 5000;      // 5 секунд слабого сигнала для блокировки
static unsigned long weakSignalStartTime = 0;   // Время начала слабого сигнала

// Пороги для определения расстояния
static const int RSSI_NEAR_THRESHOLD = -50;    // Когда мы близко к компьютеру
static const int RSSI_LOCK_THRESHOLD = -65;    // Порог для блокировки
static const int SAMPLES_TO_CONFIRM = 5;       // Увеличиваем до 5 измерений

// Добавляем переменные для стабилизации изменений состояния
static const unsigned long STATE_CHANGE_DELAY = 20000;  // 20 секунд между изменениями состояния
static unsigned long lastStateChangeTime = 0;           // Время последнего изменения состояния
static int consecutiveUnlockSamples = 0;                // Счетчик последовательных измерений для разблокировки
static int consecutiveLockSamples = 0;                  // Счетчик последовательных измерений для блокировки
static const int CONSECUTIVE_SAMPLES_NEEDED = 3;        // Сколько последовательных измерений нужно для изменения состояния

// Пороги для предупреждений
static const int SIGNAL_WARNING_THRESHOLD = -65;  // Порог для предупреждения
static const int SIGNAL_CRITICAL_THRESHOLD = -75; // Критический порог

// Состояния устройства
enum DeviceState {
    NORMAL,         // Обычный режим
    MOVING_AWAY,    // Движение от компьютера
    LOCKED,         // Компьютер заблокирован
    APPROACHING     // Приближение к компьютеру
};

static DeviceState currentState = NORMAL;
static unsigned long movementStartTime = 0;

// Изменяем константы для хранения паролей
static const char* KEY_PWD_PREFIX = "pwd_";  // Префикс для ключей паролей
static const int MAX_STORED_PASSWORDS = 5;   // Максимум сохраненных паролей

// Добавим более сложный ключ шифрования (32 байта)


// Прототипы функций
void saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings);
void addRssiMeasurement(const RssiMeasurement& measurement);
void drawBatteryIndicator(int x, int y, int width, int height, float batteryLevel, bool isCharging);

// Определения функций
void saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings) {
    Serial.println("\n=== Saving Device Settings ===");
    Serial.printf("Device address: %s\n", deviceAddress.c_str());
    
    String shortKey = cleanMacAddress(deviceAddress.c_str()); // Восстанавливаем вызов
    Serial.printf("Short key: %s\n", shortKey.c_str());
    
    String pwdKey = "pwd_" + shortKey;
    String unlockKey = "unlock_" + shortKey;
    String lockKey = "lock_" + shortKey;
    
    Serial.printf("Password key: %s\n", pwdKey.c_str());
    Serial.printf("Unlock key: %s\n", unlockKey.c_str());
    Serial.printf("Lock key: %s\n", lockKey.c_str());
    
    // Если под этим ключом уже записана строка, а новая короче – нужно стереть старую запись
    esp_err_t eraseErr = nvs_erase_key(nvsHandle, pwdKey.c_str());
    if (eraseErr != ESP_OK && eraseErr != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error erasing old password key: %d\n", eraseErr);
    } else {
        // Делаем коммит после стирания ключа
        esp_err_t commitErr = nvs_commit(nvsHandle);
        if (commitErr != ESP_OK) {
            Serial.printf("Error committing erase: %d\n", commitErr);
        }
    }
    
    esp_err_t err = nvs_set_str(nvsHandle, pwdKey.c_str(), settings.password.c_str());
    if (err != ESP_OK) {
        Serial.printf("Error saving password: %d\n", err);
        return;
    }
    
    err = nvs_set_i32(nvsHandle, unlockKey.c_str(), settings.unlockRssi);
    if (err != ESP_OK) {
        Serial.printf("Error saving unlock RSSI: %d\n", err);
    }
    
    err = nvs_set_i32(nvsHandle, lockKey.c_str(), settings.lockRssi);
    if (err != ESP_OK) {
        Serial.printf("Error saving lock RSSI: %d\n", err);
    }
    
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error committing settings: %d\n", err);
    } else {
        Serial.println("Settings saved successfully");
    }
    Serial.println("=== End Saving Settings ===\n");
}

DeviceSettings getDeviceSettings(const String& deviceAddress) {
    DeviceSettings settings;
    settings.unlockRssi = DEFAULT_UNLOCK_RSSI;
    settings.lockRssi = DEFAULT_LOCK_RSSI;
    
    String shortKey = cleanMacAddress(deviceAddress.c_str()); // Восстанавливаем вызов
    
    char pwd[64] = {0};
    size_t length = sizeof(pwd);
    
    if (nvs_get_str(nvsHandle, ("pwd_" + shortKey).c_str(), pwd, &length) == ESP_OK) {
        settings.password = String(pwd);
    }
    
    int32_t value;
    if (nvs_get_i32(nvsHandle, ("unlock_" + shortKey).c_str(), &value) == ESP_OK) {
        settings.unlockRssi = value;
    }
    
    if (nvs_get_i32(nvsHandle, ("lock_" + shortKey).c_str(), &value) == ESP_OK) {
        settings.lockRssi = value;
    }
    
    return settings;
}

// Стандартный дескриптор HID клавиатуры
static const uint8_t hidReportDescriptor[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xa1, 0x01,  // Collection (Application)
    0x85, 0x01,  // Report ID (1)
    0x05, 0x07,  // Usage Page (Key Codes)
    0x19, 0xe0,  // Usage Minimum (224)
    0x29, 0xe7,  // Usage Maximum (231)
    0x15, 0x00,  // Logical Minimum (0)
    0x25, 0x01,  // Logical Maximum (1)
    0x75, 0x01,  // Report Size (1)
    0x95, 0x08,  // Report Count (8)
    0x81, 0x02,  // Input (Data, Variable, Absolute)
    0x95, 0x01,  // Report Count (1)
    0x75, 0x08,  // Report Size (8)
    0x81, 0x01,  // Input (Constant)
    0x95, 0x06,  // Report Count (6)
    0x75, 0x08,  // Report Size (8)
    0x15, 0x00,  // Logical Minimum (0)
    0x25, 0x65,  // Logical Maximum (101)
    0x05, 0x07,  // Usage Page (Key Codes)
    0x19, 0x00,  // Usage Minimum (0)
    0x29, 0x65,  // Usage Maximum (101)
    0x81, 0x00,  // Input (Data, Array)
    0xc0         // End Collection
};

// Константы для измерения RSSI
static const int RSSI_SAMPLES = 10;  // Увеличиваем количество измерений
static int rssiValues[RSSI_SAMPLES] = {0};  // Инициализируем нулями
static int rssiIndex = 0;
static int validSamples = 0;  // Счетчик валидных измерений
static float exponentialAverage = 0; // Экспоненциальное скользящее среднее
static bool exponentialAverageInitialized = false; // Флаг инициализации

// Улучшенная функция для получения среднего RSSI с фильтрацией выбросов
int getAverageRssi() {
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
    const float alpha = 0.3;
    exponentialAverage = alpha * filteredAverage + (1 - alpha) * exponentialAverage;
    
    return (int)exponentialAverage;
}

// При добавлении нового значения
void addRssiValue(int rssi) {
    if (rssi < -100 || rssi > 0) return;  // Отбрасываем невалидные значения
    
    rssiValues[rssiIndex] = rssi;
    rssiIndex = (rssiIndex + 1) % RSSI_SAMPLES;
    if (validSamples < RSSI_SAMPLES) validSamples++;
    
    // Обновляем глобальное значение среднего RSSI
    lastAverageRssi = getAverageRssi();
}

// После других static переменных, до функции updateDisplay()
static int lastMovementCount = 0;  // Счетчик для отслеживания движения

// Добавим функцию для обновления экрана
void updateDisplay() {
    // При удержании кнопки A дольше LONG_PRESS_DURATION показываем сообщение и выходим
    if (M5.BtnA.isPressed() && btnAPressStart != 0 && (millis() - btnAPressStart) >= LONG_PRESS_DURATION) {
        Disbuff->fillSprite(BLACK);
        Disbuff->setTextColor(GREEN);
        Disbuff->setCursor(5, 60);
        Disbuff->print("RSSI thresholds set");
        Disbuff->pushSprite(0, 0);
        return;
    }
    int8_t isLocked = 0;
    nvs_get_i8(nvsHandle, KEY_IS_LOCKED, &isLocked);
    
    // Получаем текущие данные о батарее
    float currentBatteryLevel = M5.Power.getBatteryLevel();
    bool isCharging = M5.Power.isCharging();
    
    Disbuff->fillSprite(BLACK);
    Disbuff->setTextSize(1);
    
    // BLE статус
    Disbuff->setTextColor(connected ? GREEN : RED);
    Disbuff->setCursor(5, 0);
    Disbuff->printf("BLE:%s", connected ? "OK" : "NO");
    
    if (connected) {
        // RSSI
        Disbuff->setTextColor(WHITE);
        Disbuff->setCursor(5, 12);
        Disbuff->printf("RS:%d", lastAverageRssi);
        
        // Среднее RSSI
        Disbuff->setCursor(5, 24);
        Disbuff->printf("AV:%d", lastAverageRssi);
        
        // Состояние
        Disbuff->setCursor(5, 36);
        Disbuff->printf("ST:");
        switch (currentState) {
            case NORMAL: 
                Disbuff->setTextColor(GREEN);
                Disbuff->print("NORM"); 
                break;
            case MOVING_AWAY: 
                Disbuff->setTextColor(YELLOW);
                Disbuff->printf("AWAY:%d", 
                    (MOVEMENT_TIME - (millis() - movementStartTime)) / 1000);
                break;
            case LOCKED: 
                Disbuff->setTextColor(RED);
                Disbuff->print("LOCK"); 
                break;
            case APPROACHING: 
                Disbuff->setTextColor(BLUE);
                Disbuff->print("APPR"); 
                break;
        }
        
        // Счетчик движения (65px)
        if (currentState == MOVING_AWAY) {
            Disbuff->setCursor(5, 48);
            Disbuff->setTextColor(YELLOW);
            Disbuff->printf("CNT:%d/%d", lastMovementCount, MOVEMENT_SAMPLES);
        }
        
        // Разница RSSI (80px)
        Disbuff->setCursor(5, 60);
        Disbuff->setTextColor(WHITE);
        static int lastRssiDiff = 0;
        static int previousRssi = 0;
        lastRssiDiff = lastAverageRssi - previousRssi;
        previousRssi = lastAverageRssi;
        if (currentState == MOVING_AWAY || currentState == NORMAL) {
            if (lastRssiDiff < 0) {
                Disbuff->setTextColor(RED);    // Красный для удаления
                Disbuff->printf("<<:%d", abs(lastRssiDiff));  // Стрелки влево
            } else if (lastRssiDiff > 0) {
                Disbuff->setTextColor(GREEN);  // Зеленый для приближения
                Disbuff->printf(">>:%d", lastRssiDiff);       // Стрелки вправо
            } else {
                Disbuff->setTextColor(YELLOW); // Желтый для отсутствия движения
                Disbuff->printf("==:%d", lastRssiDiff);       // Равно для стабильности
            }
        }
        
        // Уровень сигнала (95px)
        Disbuff->setCursor(5, 72);
        if (lastAverageRssi > -50) {
            Disbuff->setTextColor(GREEN);
            Disbuff->print("SIG:HIGH");
        } else if (lastAverageRssi > SIGNAL_LOSS_THRESHOLD) {
            Disbuff->setTextColor(YELLOW);
            Disbuff->print("SIG:MID");
        } else {
            Disbuff->setTextColor(RED);
            if (weakSignalStartTime > 0) {
                int timeLeft = (SIGNAL_LOSS_TIME - (millis() - weakSignalStartTime)) / 1000;
                Disbuff->printf("SIG:%ds", timeLeft);
            } else {
                Disbuff->print("SIG:LOW");
            }
        }
        
        // Динамические пороги (lock/unlock)
        Disbuff->setTextSize(1);
        Disbuff->setTextColor(WHITE);
        Disbuff->setCursor(5, 84);
        Disbuff->printf("L:%d  U:%d", dynamicLockThreshold, dynamicUnlockThreshold);
        Disbuff->setTextSize(1);
        
        // Пароль (110px)
        Disbuff->setCursor(5, 96);
        Disbuff->setTextSize(1);
        if (getPasswordForDevice(connectedDeviceAddress.c_str()).length() > 0) {
            Disbuff->setTextColor(GREEN);
            Disbuff->print("PWD:OK");
        } else {
            Disbuff->setTextColor(RED);
            Disbuff->print("PWD:NO");
        }
        
        // Индикатор заряда батареи (122px)
        Disbuff->setCursor(5, 108);
        Disbuff->setTextSize(1);
        
        // Устанавливаем цвет в зависимости от уровня заряда
        uint16_t batteryColor;
        if (currentBatteryLevel > 75) {
            batteryColor = GREEN;
        } else if (currentBatteryLevel > 25) {
            batteryColor = YELLOW;
        } else {
            batteryColor = RED;
        }
        Disbuff->setTextColor(batteryColor);
        
        // Отображаем статус зарядки и уровень заряда
        Disbuff->printf("BAT:%d%%%s", (int)currentBatteryLevel, isCharging ? "+" : "");
        
        // Отображаем графический индикатор заряда
        drawBatteryIndicator(5, 120, 30, 10, currentBatteryLevel, isCharging);
    } else {
        // Если не подключены, показываем только статус батареи
        Disbuff->setCursor(5, 108);
        Disbuff->setTextSize(1);
        
        // Устанавливаем цвет в зависимости от уровня заряда
        uint16_t batteryColor;
        if (currentBatteryLevel > 75) {
            batteryColor = GREEN;
        } else if (currentBatteryLevel > 25) {
            batteryColor = YELLOW;
        } else {
            batteryColor = RED;
        }
        Disbuff->setTextColor(batteryColor);
        
        // Отображаем статус зарядки и уровень заряда
        Disbuff->printf("BAT:%d%%%s", (int)currentBatteryLevel, isCharging ? "+" : "");
        
        // Отображаем графический индикатор заряда
        drawBatteryIndicator(5, 120, 30, 10, currentBatteryLevel, isCharging);
    }
    
    Disbuff->pushSprite(0, 0);
}

// Добавляем функцию отрисовки индикатора батареи
void drawBatteryIndicator(int x, int y, int width, int height, float batteryLevel, bool isCharging) {
    // Устанавливаем цвет в зависимости от уровня заряда
    uint16_t batteryColor;
    if (batteryLevel > 75) {
        batteryColor = GREEN;
    } else if (batteryLevel > 25) {
        batteryColor = YELLOW;
    } else {
        batteryColor = RED;
    }
    
    const int capWidth = 3;   // Ширина выступа батарейки
    const int capHeight = 4;  // Высота выступа батарейки
    
    // Рисуем основную часть батарейки (прямоугольник)
    Disbuff->drawRect(x, y, width, height, WHITE);
    
    // Рисуем выступ (положительный контакт) батарейки
    Disbuff->fillRect(
        x + width, 
        y + (height - capHeight) / 2, 
        capWidth, 
        capHeight, 
        WHITE
    );
    
    // Вычисляем ширину заполненной части батарейки
    int fillWidth = map((int)batteryLevel, 0, 100, 0, width - 2);
    
    // Заполняем внутреннюю часть батарейки
    if (fillWidth > 0) {
        Disbuff->fillRect(x + 1, y + 1, fillWidth, height - 2, batteryColor);
        
        // Рисуем деления внутри батарейки
        if (width > 10) {
            for (int i = 1; i < 5; i++) {
                int lineX = x + i * (width / 5);
                if (lineX < x + fillWidth) continue; // Не рисуем линии в заполненной части
                Disbuff->drawLine(
                    lineX, 
                    y + 1, 
                    lineX, 
                    y + height - 2, 
                    WHITE
                );
            }
        }
    }
    
    // Добавляем значок молнии если заряжается
    if (isCharging) {
        Disbuff->setTextColor(YELLOW);
        Disbuff->setCursor(x + width + capWidth + 2, y);
        Disbuff->print("+"); // Используем "+", т.к. символ молнии может не поддерживаться
    }
}

// Сначала объявляем класс для колбэков сканирования
class MyScanCallbacks: public NimBLEScanCallbacks {
private:
    NimBLEAdvertisedDevice* targetDevice = nullptr;
    
public:
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        if (!connected || !scanMode) return;  // Пропускаем если не подключены
        
        if (advertisedDevice->isAdvertisingService(NimBLEUUID("1812"))) {  // 0x1812 - HID Service
            if (serialOutputEnabled) {
                Serial.printf("\nFound HID device: %s, RSSI: %d\n", 
                    advertisedDevice->getAddress().toString().c_str(),
                    advertisedDevice->getRSSI());
            }
            
            // Сохраняем устройство и останавливаем сканирование
            if (targetDevice != nullptr) {
                delete targetDevice;
            }
            targetDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
            NimBLEDevice::getScan()->stop();
            
            // Пытаемся подключиться
            if (bleServer && !connected) {
                connectedDeviceAddress = targetDevice->getAddress().toString();
                connected = true;
                // Загружаем сохранённые пороги для этого устройства
                {
                    DeviceSettings ds = getDeviceSettings(connectedDeviceAddress.c_str());
                    dynamicLockThreshold = ds.lockRssi;
                    dynamicUnlockThreshold = ds.unlockRssi;
                    if (serialOutputEnabled) {
                        Serial.printf("Scan loaded thresholds: lock=%d, unlock=%d\n", 
                            dynamicLockThreshold, dynamicUnlockThreshold);
                    }
                }
                updateDisplay();
            }
        }
    }
    
    ~MyScanCallbacks() {
        if (targetDevice != nullptr) {
            delete targetDevice;
        }
    }
};

// Затем объявляем глобальные переменные
static NimBLEScan* pScan = nullptr;
static MyScanCallbacks* scanCallbacks = new MyScanCallbacks();

// Переносим определения ПЕРЕД классом ServerCallbacks

// Добавляем предварительное объявление для adjustBrightness
void adjustBrightness(esp_power_level_t txPower);
bool isUSBConnected();

// Затем класс для колбэков сервера
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        if (serialOutputEnabled) {
            Serial.println("\n=== BLE Connection Start ===");
            Serial.printf("Device: %s\n", NimBLEAddress(desc->peer_ota_addr).toString().c_str());
            Serial.printf("Connection Handle: %d\n", desc->conn_handle);
            Serial.printf("Role: %d\n", desc->role);
            Serial.printf("Connection Interval: %d\n", desc->conn_itvl);
            Serial.printf("Latency: %d, Supervision Timeout: %d\n", desc->conn_latency, desc->supervision_timeout);
            Serial.printf("Security State: %d\n", desc->sec_state.encrypted);
            Serial.printf("Time: %lu ms\n", millis());
        }

        connected = true;
        connectedDeviceAddress = NimBLEAddress(desc->peer_ota_addr).toString();
        
        // Обновляем короткий ключ устройства
        updateCurrentShortKey(connectedDeviceAddress.c_str());
        
        if (serialOutputEnabled) {
            Serial.println("Short key updated");
        }

        // Сохраняем информацию о подключении
        connection_info.connected = true;
        connection_info.conn_handle = desc->conn_handle;
        connection_info.address = NimBLEAddress(desc->peer_ota_addr).toString();
        
        // Добавляем отладочную информацию о сохраненном адресе
        if (serialOutputEnabled) {
            Serial.println("\n=== Connection Info Debug ===");
            Serial.printf("Connected: %s\n", connection_info.connected ? "Yes" : "No");
            Serial.printf("Connection Handle: %d\n", connection_info.conn_handle);
            Serial.printf("Device Address: '%s'\n", connection_info.address.c_str());
            Serial.printf("Address Length: %d\n", connection_info.address.length());
            Serial.printf("Connected Device Address: '%s'\n", connectedDeviceAddress.c_str());
            Serial.println("=== End Connection Info Debug ===\n");
        }
        
        // Проверяем, есть ли пароль для этого устройства
        String password = getPasswordForDevice(connectedDeviceAddress.c_str());
        if (password.length() == 0) {
            // Если пароля нет, сохраняем пароль по умолчанию
            if (serialOutputEnabled) {
                Serial.println("No password found for this device. Saving default password.");
            }
            // Вместо вызова savePasswordForDevice используем прямой вызов функций
            DeviceSettings settings = getDeviceSettings(connectedDeviceAddress.c_str());
            settings.password = encryptPassword("12345");
            saveDeviceSettings(connectedDeviceAddress.c_str(), settings);
            
            if (serialOutputEnabled) {
                Serial.printf("Saving password for device: %s\n", connectedDeviceAddress.c_str());
                Serial.printf("Password length: %d\n", 5);
                Serial.printf("Encrypted length: %d\n", settings.password.length());
            }
        }
        
        // Сохраняем адрес устройства в NVS для восстановления после перезагрузки
        if (serialOutputEnabled) {
            Serial.println("Saving device address to NVS...");
        }
        
        // Сохраняем адрес устройства
        esp_err_t err = nvs_set_str(nvsHandle, KEY_LAST_ADDR, connectedDeviceAddress.c_str());
        if (err != ESP_OK) {
            if (serialOutputEnabled) {
                Serial.printf("Error saving device address: %d\n", err);
            }
        } else {
            // Сохраняем флаг, что устройство было сопряжено
            err = nvs_set_u8(nvsHandle, "paired", 1);
            if (err != ESP_OK) {
                if (serialOutputEnabled) {
                    Serial.printf("Error saving paired flag: %d\n", err);
                }
            } else {
                // Сохраняем дополнительную информацию о подключении
                err = nvs_set_u16(nvsHandle, "conn_handle", desc->conn_handle);
                if (err != ESP_OK && serialOutputEnabled) {
                    Serial.printf("Error saving connection handle: %d\n", err);
                }
                
                // Сохраняем время последнего подключения
                err = nvs_set_u32(nvsHandle, "last_conn_time", millis());
                if (err != ESP_OK && serialOutputEnabled) {
                    Serial.printf("Error saving connection time: %d\n", err);
                }
                
                // Фиксируем изменения
                err = nvs_commit(nvsHandle);
                if (err != ESP_OK) {
                    if (serialOutputEnabled) {
                        Serial.printf("Error committing pairing info: %d\n", err);
                    }
                } else if (serialOutputEnabled) {
                    Serial.println("Pairing information saved to NVS successfully");
                }
            }
        }

        // Приостанавливаем сканирование на время подключения
        if (pScan->isScanning()) {
            if (serialOutputEnabled) {
                Serial.println("Stopping scan during connection...");
            }
            pScan->stop();
        }

        if (serialOutputEnabled) {
            Serial.println("=== BLE Connection Complete ===\n");
        }
        
        // Проверяем, было ли устройство заблокировано
        bool wasLocked = loadDeviceLockState(connectedDeviceAddress.c_str()); // Проверяем состояние блокировки для этого устройства
        if (wasLocked) {
            // Устанавливаем состояние LOCKED
            currentState = LOCKED;
            lastStateChangeTime = millis() - STATE_CHANGE_DELAY / 2;
            consecutiveLockSamples = 0;
            consecutiveUnlockSamples = 0;
            if (serialOutputEnabled) {
                Serial.println("Device was locked before reconnection.");
                Serial.printf("Current RSSI: %d, unlock threshold: %d\n", lastAverageRssi, dynamicUnlockThreshold);
                Serial.println("Will monitor signal strength and unlock when stable.");
            }
            // Автоматически разблокируем, если уже рядом
            if (lastAverageRssi > dynamicUnlockThreshold) {
                if (serialOutputEnabled) {
                    Serial.println("Auto-unlock on reconnect as device is in range");
                }
                unlockComputer();
                currentState = NORMAL;
            }
        } else {
            // Убедимся, что состояние точно 0, если ключ был найден, но значение было не 1
             int8_t lockedCheckValue; // Используем временную переменную
             if (nvs_get_i8(nvsHandle, KEY_IS_LOCKED, &lockedCheckValue) == ESP_OK && lockedCheckValue != 0) {
                 saveGlobalLockState(false); // Теперь должно быть видно
             }
        }
        // Загружаем сохраненные пороги для этого устройства и применяем их
        {
            DeviceSettings devSettings = getDeviceSettings(connectedDeviceAddress.c_str());
            dynamicLockThreshold = devSettings.lockRssi;
            dynamicUnlockThreshold = devSettings.unlockRssi;
            if (serialOutputEnabled) {
                Serial.printf("Loaded thresholds: lock=%d, unlock=%d\n",
                    dynamicLockThreshold, dynamicUnlockThreshold);
            }
        }
    }

    void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        if (serialOutputEnabled) {
            Serial.println("\n=== BLE Device Disconnected ===");
            Serial.printf("Device: %s\n", NimBLEAddress(desc->peer_ota_addr).toString().c_str());
            Serial.printf("Connection Handle: %d\n", desc->conn_handle);
            Serial.printf("Security State: %d\n", desc->sec_state.encrypted);
            Serial.printf("Time: %lu ms\n", millis());
            
            // Добавляем отладочную информацию о текущем состоянии connection_info
            Serial.println("\n=== Connection Info Before Disconnect ===");
            Serial.printf("Connected: %s\n", connection_info.connected ? "Yes" : "No");
            Serial.printf("Connection Handle: %d\n", connection_info.conn_handle);
            Serial.printf("Device Address: '%s'\n", connection_info.address.c_str());
            Serial.printf("Address Length: %d\n", connection_info.address.length());
            Serial.printf("Connected Device Address: '%s'\n", connectedDeviceAddress.c_str());
            Serial.println("=== End Connection Info ===\n");
        }
        
        // Сохраняем адрес устройства перед отключением
        std::string lastAddress = connection_info.address;
        
        connected = false;
        
        // Обновляем информацию о подключении, но сохраняем адрес
        connection_info.connected = false;
        connection_info.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        // НЕ сбрасываем адрес, чтобы его можно было использовать для получения пароля
        // connection_info.address = "";
        
        // Сохраняем адрес устройства при отключении, если компьютер заблокирован
        if (currentState == LOCKED) {
            if (serialOutputEnabled) {
                Serial.println("Device disconnected while locked. Saving last address...");
            }
            
            // Сохраняем адрес последнего подключенного устройства
            esp_err_t err = nvs_set_str(nvsHandle, KEY_LAST_ADDR, connectedDeviceAddress.c_str());
            if (err != ESP_OK) {
                if (serialOutputEnabled) {
                    Serial.printf("Error saving last address: %d\n", err);
                }
            } else {
                // Фиксируем изменения
                err = nvs_commit(nvsHandle);
                if (err != ESP_OK) {
                    if (serialOutputEnabled) {
                        Serial.printf("Error committing last address: %d\n", err);
                    }
                } else if (serialOutputEnabled) {
                    Serial.println("Last address saved successfully");
                }
            }
        }
        
        if (serialOutputEnabled) {
            // Добавляем отладочную информацию о текущем состоянии connection_info после обновления
            Serial.println("\n=== Connection Info After Disconnect ===");
            Serial.printf("Connected: %s\n", connection_info.connected ? "Yes" : "No");
            Serial.printf("Connection Handle: %d\n", connection_info.conn_handle);
            Serial.printf("Device Address: '%s'\n", connection_info.address.c_str());
            Serial.printf("Address Length: %d\n", connection_info.address.length());
            Serial.printf("Connected Device Address: '%s'\n", connectedDeviceAddress.c_str());
            Serial.printf("Last Address: '%s'\n", lastAddress.c_str());
            Serial.println("=== End Connection Info ===\n");
        }
        
        // Перезапускаем рекламу при отключении
        if (serialOutputEnabled) {
            Serial.println("Restarting advertising after disconnect...");
        }
        
        // Добавляем задержку перед перезапуском рекламы
        delay(100);
        
        // Перезапускаем рекламу с отладочной информацией
        int rc = pServer->getAdvertising()->start();
        if (serialOutputEnabled) {
            Serial.printf("Restart advertising result: %d\n", rc);
            if (rc != 0) {
                Serial.println("Error restarting advertising after disconnect.");
            }
        }
    }

    // Добавляем колбэк для RSSI
    void onRssiUpdate(NimBLEServer* pServer, int rssi) {
        lastAverageRssi = rssi;
        Serial.printf("RSSI Update: %d dBm\n", rssi);
    }

    // Добавляем методы безопасности
    uint32_t onPassKeyRequest() {
        Serial.println("=== Security: PassKey Request ===");
        return 123456;
    }

    void onAuthenticationComplete(uint16_t conn_handle) {
        Serial.println("=== Security: Authentication Complete ===");
        Serial.printf("Connection handle: %d\n", conn_handle);
    }

    bool onConfirmPIN(uint32_t pin) {
        Serial.printf("=== Security: Confirm PIN: %d ===\n", pin);
        return true;
    }

    // Функция для адаптивного управления мощностью
    void adjustTransmitPower(DeviceState state, int rssi) {
        esp_power_level_t newPower;
        static unsigned long lastPowerChangeTime = 0;
        static esp_power_level_t lastPower = ESP_PWR_LVL_P9;
        static bool isPowerGraduallyReducing = false;
        
        // Получаем настройки для подключенного устройства
        DeviceSettings settings;
        bool hasCustomSettings = false;
        
        if (connected && connectedDeviceAddress.length() > 0) {
            settings = getDeviceSettings(connectedDeviceAddress.c_str());
            hasCustomSettings = true;
        }
        
        // Рассчитываем пороги RSSI на основе пользовательских настроек
        int veryCloseThreshold = -35;  // По умолчанию
        int closeThreshold = -45;
        int mediumThreshold = -55;
        int farThreshold = -65;
        
        if (hasCustomSettings) {
            // Настраиваем пороги относительно значений lock/unlock
            // Используем более щадящие пороги для обеспечения стабильной связи
            int range = settings.unlockRssi - settings.lockRssi;
            
            if (range > 5) {
                veryCloseThreshold = settings.unlockRssi;
                closeThreshold = settings.unlockRssi - (range / 4);
                mediumThreshold = settings.unlockRssi - (range / 2);
                farThreshold = settings.lockRssi + 5; // Чуть лучше чем порог блокировки
            }
        }
        
        // Определяем базовую мощность на основе RSSI
        esp_power_level_t basePower;
        if (rssi > veryCloseThreshold) {
            basePower = ESP_PWR_LVL_N9;  // Не используем самую низкую мощность N12 для надежности
        } else if (rssi > closeThreshold) {
            basePower = ESP_PWR_LVL_N6;
        } else if (rssi > mediumThreshold) {
            basePower = ESP_PWR_LVL_N3;
        } else if (rssi > farThreshold) {
            basePower = ESP_PWR_LVL_P3;
        } else {
            basePower = ESP_PWR_LVL_P9;  // Максимальная мощность
        }
        
        // Если находимся в состоянии блокировки или удаления, нужно обеспечить надежный сигнал
        if (state == MOVING_AWAY || state == LOCKED) {
            // Для состояния MOVING_AWAY используем повышенную мощность
            if (basePower < ESP_PWR_LVL_P3) {
                basePower = ESP_PWR_LVL_P3;
            }
        }
        
        // Учитываем состояние зарядки
        bool isUSB = isUSBConnected();
        
        if (!isUSB) {
            // При работе от батареи постепенно снижаем мощность, не делая резких скачков
            if (!isPowerGraduallyReducing && lastPower > basePower) {
                isPowerGraduallyReducing = true;
                lastPowerChangeTime = millis();
            } else if (isPowerGraduallyReducing) {
                // Проверяем, прошло ли достаточно времени для снижения мощности
                unsigned long currentTime = millis();
                if (currentTime - lastPowerChangeTime > 5000) { // 5 секунд между шагами снижения
                    if (lastPower > basePower) {
                        // Снижаем мощность на один шаг
                        esp_power_level_t nextPower;
                        switch (lastPower) {
                            case ESP_PWR_LVL_P9: nextPower = ESP_PWR_LVL_P6; break;
                            case ESP_PWR_LVL_P6: nextPower = ESP_PWR_LVL_P3; break;
                            case ESP_PWR_LVL_P3: nextPower = ESP_PWR_LVL_N0; break;
                            case ESP_PWR_LVL_N0: nextPower = ESP_PWR_LVL_N3; break;
                            case ESP_PWR_LVL_N3: nextPower = ESP_PWR_LVL_N6; break;
                            case ESP_PWR_LVL_N6: nextPower = ESP_PWR_LVL_N9; break;
                            default: nextPower = basePower;
                        }
                        basePower = nextPower;
                        lastPowerChangeTime = currentTime;
                    } else {
                        isPowerGraduallyReducing = false;
                    }
                } else {
                    // Пока не прошло достаточно времени, используем предыдущую мощность
                    basePower = lastPower;
                }
            }
        } else {
            // При работе от USB можем сразу использовать рассчитанную мощность
            isPowerGraduallyReducing = false;
        }
        
        // Применяем рассчитанную мощность
        newPower = basePower;
        
        // Предотвращаем слишком низкую мощность при удалении
        if (state == MOVING_AWAY && rssi < settings.lockRssi + 5) {
            // Когда мы приближаемся к порогу блокировки, используем максимальную мощность
            // чтобы гарантировать отправку команды блокировки
            newPower = ESP_PWR_LVL_P9;
        }
        
        // Применяем новую мощность, если она изменилась
        if (newPower != lastPower) {
            if (serialOutputEnabled) {
                Serial.printf("Adjusting TX power: %d -> %d (RSSI: %d, State: %d, USB: %d)\n",
                    lastPower, newPower, rssi, state, isUSB);
            }
            NimBLEDevice::setPower(newPower);
            lastPower = newPower;
        }
        
        // Регулируем яркость дисплея
        adjustBrightness(newPower);
    }
};

static esp_ble_scan_params_t scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
};

// Функция определения необходимости блокировки
bool shouldLockComputer(int avgRssi) {
    if (!connected) return false;
    
    // Не блокируем, если уже заблокировано
    if (currentState == LOCKED) return false;
    
    DeviceSettings settings = getDeviceSettings(connectedDeviceAddress.c_str());
    static bool wasNear = false;
    static int samplesBelow = 0;
    static bool lockSent = false;  // Добавляем обратно флаг отправки
    
    // Используем индивидуальные настройки
    if (avgRssi > settings.unlockRssi) {
        wasNear = true;
        samplesBelow = 0;
        lockSent = false;  // Сбрасываем флаг
        return false;
    }
    
    if (wasNear && avgRssi < settings.lockRssi) {
        samplesBelow++;
        Serial.printf("Signal below threshold: %d/%d samples\n", 
            samplesBelow, SAMPLES_TO_CONFIRM);
            
        // Если достаточно измерений подтверждают удаление
        if (samplesBelow >= SAMPLES_TO_CONFIRM && !lockSent) {
            Serial.println("!!! Lock condition detected !!!");
            return true;
        }
    } else {
        samplesBelow = 0;  // Сбрасываем счетчик если сигнал стал лучше
    }
    
    return false;
}

// Объявляем прототипы функций
String encryptPassword(const String& password);
String decryptPassword(const String& encrypted);
void clearAllPasswords();

// Определяем функции

void clearAllPasswords() {
    Serial.println("\n=== Clearing passwords ===");
    
    // Получаем список всех ключей
    nvs_iterator_t it = nvs_entry_find("nvs", "m5kb_v1", NVS_TYPE_STR);
    while (it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        String key = String(info.key);
        
        // Проверяем только ключи паролей
        if (key.startsWith("pwd_")) {
            String shortKey = key.substring(4); // Убираем "pwd_"
            
            // Если длина ключа больше 6 символов, это старый формат
            if (shortKey.length() > 6) {
                Serial.printf("Removing old key: %s\n", key.c_str());
                nvs_erase_key(nvsHandle, key.c_str());
                nvs_erase_key(nvsHandle, ("unlock_" + shortKey).c_str());
                nvs_erase_key(nvsHandle, ("lock_" + shortKey).c_str());
            }
        }
        it = nvs_entry_next(it);
    }
    nvs_release_iterator(it);
    
    nvs_commit(nvsHandle);
    Serial.println("=== Passwords cleared ===\n");
}

// Функция для получения пароля по адресу устройства
// String getPasswordForDevice(const String& deviceAddress) {
//     DeviceSettings settings = getDeviceSettings(deviceAddress);
//     return settings.password.length() > 0 ? decryptPassword(settings.password) : "";
// }

// Функция для сохранения пароля
// void savePasswordForDevice(const String& deviceAddress, const String& password) {
//     DeviceSettings settings = getDeviceSettings(deviceAddress);
//     settings.password = encryptPassword(password);
//     saveDeviceSettings(deviceAddress, settings);
//     
//     if (serialOutputEnabled) {
//         Serial.printf("Saving password for device: %s\n", deviceAddress.c_str());
//         Serial.printf("Password length: %d\n", password.length());
//         Serial.printf("Encrypted length: %d\n", settings.password.length());
//     }
// }

// Вспомогательная функция для получения короткого ключа из MAC-адреса
String getShortKey(const char* macAddress) {
    return cleanMacAddress(macAddress);
}

void listStoredDevices() {
    Serial.println("\nStored devices:");
    Serial.println("=== Debug Info ===");
    
    String currentShortKey = "";
    
    // Показываем текущее подключенное устройство
    Serial.println("Checking connected device:");
    if (connected) {
        Serial.printf("  Connected device: %s\n", connectedDeviceAddress.c_str());
        currentShortKey = cleanMacAddress(connectedDeviceAddress.c_str()); // Восстанавливаем вызов
        Serial.printf("  Short key: %s\n", currentShortKey.c_str());
        
        DeviceSettings settings = getDeviceSettings(connectedDeviceAddress.c_str());
        Serial.printf("  Password length: %d\n", settings.password.length());
        
        if (settings.password.length() > 0) {
            Serial.printf("\nDevice: %s (CONNECTED)\n", connectedDeviceAddress.c_str());
            Serial.printf("  Unlock RSSI: %d\n", settings.unlockRssi);
            Serial.printf("  Lock RSSI: %d\n", settings.lockRssi);
            Serial.printf("  Current RSSI: %d\n", lastAverageRssi);
            Serial.printf("  Has password: YES\n");
        }
    } else {
        Serial.println("  No device connected");
    }
    
    // Показываем последнее заблокированное устройство
    Serial.println("\nChecking last locked device:");
    char lastAddr[32] = {0};
    size_t length = sizeof(lastAddr);
    String lastShortKey = "";
    
    if (nvs_get_str(nvsHandle, KEY_LAST_ADDR, lastAddr, &length) == ESP_OK) {
        Serial.printf("  Last locked device: %s\n", lastAddr);
        if (strlen(lastAddr) > 0) {
            lastShortKey = cleanMacAddress(lastAddr); // Восстанавливаем вызов
            Serial.printf("  Short key: %s\n", lastShortKey.c_str());
            
            if (!connected || strcmp(lastAddr, connectedDeviceAddress.c_str()) != 0) {
                DeviceSettings settings = getDeviceSettings(lastAddr);
                Serial.printf("  Password length: %d\n", settings.password.length());
                
                if (settings.password.length() > 0) {
                    Serial.printf("\nDevice: %s (LAST LOCKED)\n", lastAddr);
                    Serial.printf("  Unlock RSSI: %d\n", settings.unlockRssi);
                    Serial.printf("  Lock RSSI: %d\n", settings.lockRssi);
                    Serial.printf("  Has password: YES\n");
                }
            } else {
                Serial.println("  Last locked device is current connected device");
            }
        } else {
            Serial.println("  Last locked device is empty");
        }
    } else {
        Serial.println("  No last locked device found");
    }
    
    // Показываем все сохраненные устройства
    Serial.println("\nChecking saved devices:");
    nvs_iterator_t it = nvs_entry_find("nvs", "m5kb_v1", NVS_TYPE_STR);
    while (it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        String key = String(info.key);
        
        // Проверяем только ключи, начинающиеся с pwd_
        if (key.startsWith("pwd_")) {
            String shortKey = key.substring(4); // Убираем "pwd_"
            shortKey.replace(":", ""); // Убираем двоеточия, если они есть
            Serial.printf("Found device with key: %s\n", key.c_str());
            
            // Получаем пароль
            char pwd[64];
            size_t pwdLen = sizeof(pwd);
            if (nvs_get_str(nvsHandle, key.c_str(), pwd, &pwdLen) == ESP_OK && strlen(pwd) > 0) {
                // Получаем RSSI пороги
                int32_t unlockRssi, lockRssi;
                if (nvs_get_i32(nvsHandle, ("unlock_" + shortKey).c_str(), &unlockRssi) == ESP_OK &&
                    nvs_get_i32(nvsHandle, ("lock_" + shortKey).c_str(), &lockRssi) == ESP_OK) {
                    
                    String status = "(SAVED)";
                    if (shortKey == currentShortKey) status = "(CURRENT)";
                    if (shortKey == lastShortKey) status = "(LAST LOCKED)";
                    
                    Serial.printf("\nDevice with key %s %s\n", shortKey.c_str(), status.c_str());
                    Serial.printf("  Unlock RSSI: %d\n", unlockRssi);
                    Serial.printf("  Lock RSSI: %d\n", lockRssi);
                    Serial.printf("  Has password: YES\n");
                }
            }
        }
        it = nvs_entry_next(it);
    }
    nvs_release_iterator(it);
    
    Serial.println("\n=== End Debug Info ===");
}

// Функция отправки одного символа
void sendKey(char key) {
    uint8_t keyCode = 0;
    uint8_t modifiers = 0;  // Используем отдельную переменную для модификаторов
    
    // Отладочная информация
    if (serialOutputEnabled) {
        Serial.printf("Sending key: '%c' (ASCII: %d)\n", key, (int)key);
    }
    
    // Преобразование ASCII в HID код
    if (key >= 'a' && key <= 'z') {
        keyCode = 4 + (key - 'a');
    } else if (key >= 'A' && key <= 'Z') {
        keyCode = 4 + (key - 'A');
        modifiers = 0x02;  // Shift
    } else if (key >= '1' && key <= '9') {
        keyCode = 30 + (key - '1');
    } else if (key == '0') {
        keyCode = 39;
    } else {
        // Специальные символы
        switch (key) {
            case ' ': keyCode = 0x2C; break;  // Space
            case '-': keyCode = 0x2D; break;  // - (minus)
            case '=': keyCode = 0x2E; break;  // = (equals)
            case '[': keyCode = 0x2F; break;  // [ (left bracket)
            case ']': keyCode = 0x30; break;  // ] (right bracket)
            case '\\': keyCode = 0x31; break; // \ (backslash)
            case ';': keyCode = 0x33; break;  // ; (semicolon)
            case '\'': keyCode = 0x34; break; // ' (apostrophe)
            case '`': keyCode = 0x35; break;  // ` (grave accent)
            case ',': keyCode = 0x36; break;  // , (comma)
            case '.': keyCode = 0x37; break;  // . (period)
            case '/': keyCode = 0x38; break;  // / (forward slash)
            
            // Символы с Shift
            case '!': keyCode = 0x1E; modifiers = 0x02; break; // ! (Shift + 1)
            case '@': keyCode = 0x1F; modifiers = 0x02; break; // @ (Shift + 2)
            case '#': keyCode = 0x20; modifiers = 0x02; break; // # (Shift + 3)
            case '$': keyCode = 0x21; modifiers = 0x02; break; // $ (Shift + 4)
            case '%': keyCode = 0x22; modifiers = 0x02; break; // % (Shift + 5)
            case '^': keyCode = 0x23; modifiers = 0x02; break; // ^ (Shift + 6)
            case '&': keyCode = 0x24; modifiers = 0x02; break; // & (Shift + 7)
            case '*': keyCode = 0x25; modifiers = 0x02; break; // * (Shift + 8)
            case '(': keyCode = 0x26; modifiers = 0x02; break; // ( (Shift + 9)
            case ')': keyCode = 0x27; modifiers = 0x02; break; // ) (Shift + 0)
            case '_': keyCode = 0x2D; modifiers = 0x02; break; // _ (Shift + -)
            case '+': keyCode = 0x2E; modifiers = 0x02; break; // + (Shift + =)
            case '{': keyCode = 0x2F; modifiers = 0x02; break; // { (Shift + [)
            case '}': keyCode = 0x30; modifiers = 0x02; break; // } (Shift + ])
            case '|': keyCode = 0x31; modifiers = 0x02; break; // | (Shift + \)
            case ':': keyCode = 0x33; modifiers = 0x02; break; // : (Shift + ;)
            case '"': keyCode = 0x34; modifiers = 0x02; break; // " (Shift + ')
            case '~': keyCode = 0x35; modifiers = 0x02; break; // ~ (Shift + `)
            case '<': keyCode = 0x36; modifiers = 0x02; break; // < (Shift + ,)
            case '>': keyCode = 0x37; modifiers = 0x02; break; // > (Shift + .)
            case '?': keyCode = 0x38; modifiers = 0x02; break; // ? (Shift + /)
            
            default:
                if (serialOutputEnabled) {
                    Serial.printf("Warning: Unsupported character '%c' (ASCII: %d)\n", key, (int)key);
                }
                break;
        }
    }
    
    if (keyCode > 0) {
        if (serialOutputEnabled) {
            Serial.printf("HID keyCode: 0x%02X, modifiers: 0x%02X\n", keyCode, modifiers);
        }
        
        uint8_t msg[8] = {modifiers, 0, keyCode, 0, 0, 0, 0, 0};  // Используем modifiers напрямую
        input->setValue(msg, sizeof(msg));
        input->notify();
        delay(50);
        
        // Отпускаем клавишу
        uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        input->setValue(release, sizeof(release));
        input->notify();
    }
}

// Функция ввода пароля
void typePassword(const String& password) {
    if (serialOutputEnabled) {
        Serial.println("=== Typing password ===");
        Serial.printf("Password length: %d\n", password.length());
        // Выводим пароль в виде звездочек для безопасности
        Serial.print("Password (masked): ");
        for (int i = 0; i < password.length(); i++) {
            Serial.print("*");
        }
        Serial.println();
    }
    
    // Добавляем небольшую задержку перед вводом пароля
    delay(500);
    
    for (int i = 0; i < password.length(); i++) {
        char c = password[i];
        sendKey(c);
        // Добавляем задержку между символами для надежности
        delay(100);
    }
    
    if (serialOutputEnabled) {
        Serial.println("Password typed, sending Enter...");
    }
    
    // Добавляем задержку перед нажатием Enter
    delay(200);
    
    // Отправляем Enter
    uint8_t enter[8] = {0, 0, 0x28, 0, 0, 0, 0, 0};
    input->setValue(enter, sizeof(enter));
    input->notify();
    delay(50);
    
    // Отпускаем Enter
    uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    input->setValue(release, sizeof(release));
    input->notify();
    
    if (serialOutputEnabled) {
        Serial.println("=== Password entry complete ===");
    }
}

// Добавим константы для пароля по умолчанию
static const char* DEFAULT_PASSWORD = "12345";  // Пример пароля

// Добавляем функцию для ввода пароля через Serial
void setPasswordFromSerial() {
    Serial.println("\n=== Password Setup ===");
    Serial.printf("Current device: %s\n", connectedDeviceAddress.c_str());
    Serial.printf("Current RSSI: %d (Make sure you're at your normal working position)\n", lastAverageRssi);
    Serial.println("Enter new password (end with newline):");
    
    String password = "";
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (password.length() > 0) {
                    break;
                }
            } else {
                password += c;
                Serial.print("*");  // Маскируем ввод звездочками
            }
        }
        delay(10);
    }
    Serial.println();
    
    // Сохраняем пароль и настройки RSSI
    DeviceSettings settings;
    settings.password = encryptPassword(password);
    
    // Устанавливаем пороги RSSI относительно текущего уровня
    int baseRssi = lastAverageRssi;
    settings.unlockRssi = baseRssi + 10;
    settings.lockRssi = baseRssi - 10;
    
    // Проверяем длину пароля перед сохранением
    if (serialOutputEnabled) {
        Serial.printf("Password length before encryption: %d\n", password.length());
        Serial.printf("Password length after encryption: %d\n", settings.password.length());
    }
    
    saveDeviceSettings(connectedDeviceAddress.c_str(), settings);
    
    // Проверяем сохранение
    DeviceSettings checkSettings = getDeviceSettings(connectedDeviceAddress.c_str());
    if (checkSettings.password.length() == 0) {
        Serial.println("ERROR: Password was not saved correctly!");
        return;
    }
    
    Serial.println("Password and RSSI thresholds saved:");
    Serial.printf("Base RSSI     : %d (current position)\n", baseRssi);
    Serial.printf("Unlock RSSI   : %d (+10 from base)\n", settings.unlockRssi);
    Serial.printf("Lock RSSI     : %d (-10 from base)\n", settings.lockRssi);
    Serial.printf("Critical level: %d (-35 from base)\n", baseRssi - 35);
    
    // Обновляем динамические пороги после установки пароля и RSSI порогов
    dynamicLockThreshold = settings.lockRssi;
    dynamicUnlockThreshold = settings.unlockRssi;
    if (serialOutputEnabled) {
        Serial.printf("Dynamic thresholds updated via setpwd: lock=%d, unlock=%d\n",
            dynamicLockThreshold, dynamicUnlockThreshold);
    }
}

// Добавим функцию для эхо ввода
void echoSerialInput() {
    static String inputBuffer = "";
    
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                Serial.println("\n=== Command execution ===");
                
                if (inputBuffer == "help") {
                    Serial.println("\nAvailable commands:");
                    Serial.println("setpwd  - Set password for current device");
                    Serial.println("getpwd  - Show current password");
                    Serial.println("getaddr - Show current device address");
                    Serial.println("setaddr <mac> - Set device address manually");
                    Serial.println("listpwd - List all stored passwords");
                    Serial.println("getpwdmac <mac> - Get password for specific MAC address");
                    Serial.println("getpwdkey <key> - Get password by NVS key (e.g. 41024dac)");
                    Serial.println("list    - Show stored devices");
                    Serial.println("silent  - Toggle debug output");
                    Serial.println("setrssl - Set RSSI threshold for locking");
                    Serial.println("setrssu - Set RSSI threshold for unlocking");
                    Serial.println("showrssi- Show current RSSI settings");
                    Serial.println("unlock  - Clear lock state");
                    Serial.println("clear   - Clear all stored preferences");
                    Serial.println("pair    - Enter BLE pairing mode");
                    Serial.println("help    - Show this help");
                }
                else if (inputBuffer == "pair") {
                    if (serialOutputEnabled) {
                        Serial.println("\n=== Starting Pairing Mode ===");
                    }
                    
                    // Если устройство подключено, отключаем его
                    if (connected) {
                        if (serialOutputEnabled) {
                            Serial.println("Disconnecting current device...");
                        }
                        NimBLEDevice::getServer()->disconnect(0);
                        delay(500);
                    }
                    
                    // Очищаем все соединения
                    if (bleServer != nullptr) {
                        bleServer->disconnect(0);
                        delay(100);
                    }
                    
                    // Останавливаем текущую рекламу если есть
                    NimBLEAdvertising* pAdvertising = bleServer->getAdvertising();
                    if(pAdvertising->isAdvertising()) {
                        pAdvertising->stop();
                        delay(100);
                    }
                    
                    // Очищаем сохраненные ключи перед перезапуском
                    NimBLEDevice::deleteAllBonds();
                    delay(100);
                    
                    // Перезапускаем BLE стек
                    NimBLEDevice::deinit(true);
                    delay(100);
                    
                    // Инициализируем с новыми настройками
                    NimBLEDevice::init("M5 BLE HID");
                    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
                    
                    if (serialOutputEnabled) {
                        Serial.println("Restarting device for pairing...");
                    }
                    
                    delay(1000);  // Даем время на вывод сообщения
                    ESP.restart();  // Перезагружаем устройство
                }
                // Восстанавливаем все остальные существующие команды
                else if (inputBuffer == "setpwd") {
                    if (connected) {
                        setPasswordFromSerial();
                                } else {
                        Serial.println("Error: No device connected!");
                    }
                }
                else if (inputBuffer == "getpwd") {
                    // Выводим текущий пароль для подключенного устройства
                    if (connected) {
                        String deviceAddress = String(connection_info.address.c_str());
                        
                        Serial.println("\n=== Password Information ===");
                        Serial.printf("Device: %s\n", deviceAddress.c_str());
                        
                        if (deviceAddress.length() > 0) {
                            String password = getPasswordForDevice(deviceAddress);
                            
                            if (password.length() > 0) {
                                Serial.print("Password: ");
                                // Выводим пароль
                                Serial.println(password);
                                
                                // Выводим пароль в виде звездочек для безопасности
                                Serial.print("Password (masked): ");
                                for (int i = 0; i < password.length(); i++) {
                                    Serial.print("*");
                                }
                                Serial.println();
                                
                                // Выводим ASCII коды символов для отладки
                                Serial.print("ASCII codes: ");
                                for (int i = 0; i < password.length(); i++) {
                                    Serial.printf("%d ", (int)password[i]);
                                }
                                Serial.println();
                            } else {
                                Serial.println("No password stored for this device!");
                                
                                // Выводим ключи для отладки
                                String shortKey = getShortKey(deviceAddress.c_str());
                                Serial.printf("Short key: %s\n", shortKey.c_str());
                                Serial.printf("Password key: pwd_%s\n", shortKey.c_str());
                                
                                // Проверяем наличие записи в NVS
                                nvs_handle_t nvsHandle;
                                esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvsHandle);
                                if (err == ESP_OK) {
                                    String pwdKey = "pwd_" + shortKey;
                                    size_t required_size;
                                    err = nvs_get_str(nvsHandle, pwdKey.c_str(), NULL, &required_size);
                                    if (err == ESP_OK) {
                                        Serial.printf("Password exists in NVS, size: %d bytes\n", required_size);
                                    } else {
                                        Serial.printf("Password not found in NVS, error: %d\n", err);
                                    }
                                    nvs_close(nvsHandle);
                                }
                            }
                        } else {
                            Serial.println("Error: Device address is empty!");
                            Serial.printf("Connection info address: '%s'\n", connection_info.address.c_str());
                            
                            // Попробуем получить список всех устройств
                            Serial.println("\nListing all stored devices:");
                            listStoredDevices();
                        }
                        Serial.println("=== End Password Information ===");
                    } else {
                        Serial.println("Error: No device connected!");
                    }
                }
                else if (inputBuffer == "list") {
                    listStoredDevices();
                }
                else if (inputBuffer == "getaddr") {
                    // Выводим текущий адрес подключенного устройства
                    if (connected) {
                        Serial.println("\n=== Device Address Information ===");
                        Serial.printf("Connected: %s\n", connected ? "Yes" : "No");
                        Serial.printf("Connection Handle: %d\n", connection_info.conn_handle);
                        Serial.printf("Device Address: %s\n", connection_info.address.c_str());
                        Serial.printf("Address Length: %d\n", connection_info.address.length());
                        Serial.println("=== End Device Address Information ===");
                    } else {
                        Serial.println("Error: No device connected!");
                    }
                }
                else if (inputBuffer == "listpwd") {
                    // Выводим список всех сохраненных паролей
                    Serial.println("\n=== Stored Passwords ===");
                    
                    nvs_handle_t localNvsHandle;
                    esp_err_t err = nvs_open("m5kb_v1", NVS_READWRITE, &localNvsHandle);
                    if (err != ESP_OK) {
                        Serial.printf("Error opening NVS: %d\n", err);
                        Serial.println("=== End Stored Passwords ===");
                        return;
                    }
                    
                    // Проверяем наличие пароля для текущего устройства
                    if (connected && connection_info.address.length() > 0) {
                        String deviceAddress = String(connection_info.address.c_str());
                        String shortKey = getShortKey(deviceAddress.c_str());
                        String pwdKey = "pwd_" + shortKey;
                        
                        Serial.printf("Checking password for current device: %s\n", deviceAddress.c_str());
                        Serial.printf("Short key: %s\n", shortKey.c_str());
                        Serial.printf("Password key: %s\n", pwdKey.c_str());
                        
                        size_t required_size;
                        err = nvs_get_str(localNvsHandle, pwdKey.c_str(), NULL, &required_size);
                        if (err == ESP_OK) {
                            Serial.printf("Password exists for current device, size: %d bytes\n", required_size);
                            
                            // Получаем пароль
                            char* encrypted = new char[required_size];
                            err = nvs_get_str(localNvsHandle, pwdKey.c_str(), encrypted, &required_size);
                            if (err == ESP_OK) {
                                String password = decryptPassword(String(encrypted));
                                Serial.printf("Device: %s (CURRENT), Password: %s\n", shortKey.c_str(), password.c_str());
                                
                                // Выводим ASCII коды символов для отладки
                                Serial.print("  ASCII codes: ");
                                for (int i = 0; i < password.length(); i++) {
                                    Serial.printf("%d ", (int)password[i]);
                                }
                                Serial.println();
                            }
                            delete[] encrypted;
                } else {
                            Serial.printf("No password found for current device, error: %d\n", err);
                        }
                    }
                    
                    // Проверяем наличие пароля для устройства 00:a7:41:02:4d:ac
                    String testAddress = "00:a7:41:02:4d:ac";
                    String testShortKey = cleanMacAddress(testAddress.c_str());
                    String testPwdKey = "pwd_" + testShortKey;
                    
                    Serial.printf("\nChecking password for test device: %s\n", testAddress.c_str());
                    Serial.printf("Short key: %s\n", testShortKey.c_str());
                    Serial.printf("Password key: %s\n", testPwdKey.c_str());
                    
                    size_t test_required_size;
                    err = nvs_get_str(localNvsHandle, testPwdKey.c_str(), NULL, &test_required_size);
                    if (err == ESP_OK) {
                        Serial.printf("Password exists for test device, size: %d bytes\n", test_required_size);
                        
                        // Получаем пароль
                        char* encrypted = new char[test_required_size];
                        err = nvs_get_str(localNvsHandle, testPwdKey.c_str(), encrypted, &test_required_size);
                        if (err == ESP_OK) {
                            String password = decryptPassword(String(encrypted));
                            Serial.printf("Device: %s (TEST), Password: %s\n", testShortKey.c_str(), password.c_str());
                            
                            // Выводим ASCII коды символов для отладки
                            Serial.print("  ASCII codes: ");
                            for (int i = 0; i < password.length(); i++) {
                                Serial.printf("%d ", (int)password[i]);
                            }
                            Serial.println();
                        }
                        delete[] encrypted;
                    } else {
                        Serial.printf("No password found for test device, error: %d\n", err);
                    }
                    
                    // Проверяем несколько известных ключей напрямую
                    const char* knownKeys[] = {
                        "paired",
                        "conn_handle",
                        "last_conn_time",
                        "device_addr",
                        "is_locked"
                    };
                    
                    Serial.println("\nChecking known keys in NVS:");
                    for (const char* key : knownKeys) {
                        Serial.printf("Checking key: %s\n", key);
                        
                        // Проверяем, является ли ключ паролем
                        if (strncmp(key, "pwd_", 4) == 0) {
                            size_t required_size;
                            err = nvs_get_str(localNvsHandle, key, NULL, &required_size);
                            if (err == ESP_OK) {
                                Serial.printf("  Password exists, size: %d bytes\n", required_size);
                                
                                // Получаем пароль
                                char* encrypted = new char[required_size];
                                err = nvs_get_str(localNvsHandle, key, encrypted, &required_size);
                                if (err == ESP_OK) {
                                    String password = decryptPassword(String(encrypted));
                                    Serial.printf("  Password: %s\n", password.c_str());
                                    
                                    // Выводим ASCII коды символов для отладки
                                    Serial.print("  ASCII codes: ");
                                    for (int i = 0; i < password.length(); i++) {
                                        Serial.printf("%d ", (int)password[i]);
                                    }
                                    Serial.println();
                                }
                                delete[] encrypted;
                            } else {
                                Serial.printf("  Password not found, error: %d\n", err);
                            }
                        } 
                        // Проверяем другие типы ключей
                        else {
                            // Пробуем получить как uint8_t
                            uint8_t u8_value;
                            err = nvs_get_u8(localNvsHandle, key, &u8_value);
                            if (err == ESP_OK) {
                                Serial.printf("  Value (u8): %d\n", u8_value);
                                continue;
                            }
                            
                            // Пробуем получить как int8_t
                            int8_t i8_value;
                            err = nvs_get_i8(localNvsHandle, key, &i8_value);
                            if (err == ESP_OK) {
                                Serial.printf("  Value (i8): %d\n", i8_value);
                                continue;
                            }
                            
                            // Пробуем получить как uint16_t
                            uint16_t u16_value;
                            err = nvs_get_u16(localNvsHandle, key, &u16_value);
                            if (err == ESP_OK) {
                                Serial.printf("  Value (u16): %d\n", u16_value);
                                continue;
                            }
                            
                            // Пробуем получить как int16_t
                            int16_t i16_value;
                            err = nvs_get_i16(localNvsHandle, key, &i16_value);
                            if (err == ESP_OK) {
                                Serial.printf("  Value (i16): %d\n", i16_value);
                                continue;
                            }
                            
                            // Пробуем получить как uint32_t
                            uint32_t u32_value;
                            err = nvs_get_u32(localNvsHandle, key, &u32_value);
                            if (err == ESP_OK) {
                                Serial.printf("  Value (u32): %d\n", u32_value);
                                continue;
                            }
                            
                            // Пробуем получить как int32_t
                            int32_t i32_value;
                            err = nvs_get_i32(localNvsHandle, key, &i32_value);
                            if (err == ESP_OK) {
                                Serial.printf("  Value (i32): %d\n", i32_value);
                                continue;
                            }
                            
                            // Пробуем получить как строку
                            size_t str_size;
                            err = nvs_get_str(localNvsHandle, key, NULL, &str_size);
                            if (err == ESP_OK) {
                                Serial.printf("  String exists, size: %d bytes\n", str_size);
                                
                                char* str_value = new char[str_size];
                                err = nvs_get_str(localNvsHandle, key, str_value, &str_size);
                                if (err == ESP_OK) {
                                    Serial.printf("  String value: %s\n", str_value);
                                }
                                delete[] str_value;
                                continue;
                            }
                            
                            Serial.printf("  Key not found or has unsupported type, error: %d\n", err);
                        }
                    }
                    
                    nvs_close(localNvsHandle);
                    
                    Serial.println("=== End Stored Passwords ===");
                }
                else if (inputBuffer.startsWith("getpwdmac ")) {
                    // Получаем MAC-адрес из команды
                    String mac = inputBuffer.substring(10);
                    mac.trim();
                    
                    Serial.println("\n=== Password by MAC ===");
                    Serial.printf("MAC address: %s\n", mac.c_str());
                    
                    // Очищаем MAC-адрес от разделителей
                    String cleanMac = cleanMacAddress(mac.c_str());
                    String shortKey = getShortKey(mac.c_str());
                    
                    Serial.printf("Clean MAC: %s\n", cleanMac.c_str());
                    Serial.printf("Short key: %s\n", shortKey.c_str());
                    
                    // Получаем пароль
                    String password = getPasswordForDevice(mac);
                    
                    if (password.length() > 0) {
                        Serial.print("Password: ");
                        Serial.println(password);
                        
                        // Выводим ASCII коды символов для отладки
                        Serial.print("ASCII codes: ");
                        for (int i = 0; i < password.length(); i++) {
                            Serial.printf("%d ", (int)password[i]);
                        }
                        Serial.println();
                    } else {
                        Serial.println("No password found for this MAC address!");
                        
                        // Проверяем наличие записи в NVS
                        nvs_handle_t localNvsHandle;
                        esp_err_t err = nvs_open("m5kb_v1", NVS_READWRITE, &localNvsHandle);
                        if (err == ESP_OK) {
                            String pwdKey = "pwd_" + shortKey;
                            size_t required_size;
                            err = nvs_get_str(localNvsHandle, pwdKey.c_str(), NULL, &required_size);
                            if (err == ESP_OK) {
                                Serial.printf("Password exists in NVS, size: %d bytes\n", required_size);
                                
                                // Пробуем получить пароль напрямую
                                char* encrypted = new char[required_size];
                                err = nvs_get_str(localNvsHandle, pwdKey.c_str(), encrypted, &required_size);
                                if (err == ESP_OK) {
                                    String decrypted = decryptPassword(String(encrypted));
                                    Serial.printf("Direct password: %s\n", decrypted.c_str());
                                    
                                    // Выводим ASCII коды символов для отладки
                                    Serial.print("ASCII codes: ");
                                    for (int i = 0; i < decrypted.length(); i++) {
                                        Serial.printf("%d ", (int)decrypted[i]);
                                    }
                                    Serial.println();
                                }
                                delete[] encrypted;
                            } else {
                                Serial.printf("Password not found in NVS, error: %d\n", err);
                            }
                            nvs_close(localNvsHandle);
                        }
                    }
                    
                    Serial.println("=== End Password by MAC ===");
                }
                else if (inputBuffer.startsWith("setaddr ")) {
                    // Получаем MAC-адрес из команды
                    String mac = inputBuffer.substring(8);
                    mac.trim();
                    
                    Serial.println("\n=== Setting Device Address ===");
                    Serial.printf("MAC address: %s\n", mac.c_str());
                    
                    // Устанавливаем адрес устройства
                    connection_info.address = mac.c_str();
                    connectedDeviceAddress = mac.c_str();
                    
                    Serial.println("\n=== Connection Info After Setting Address ===");
                    Serial.printf("Connected: %s\n", connection_info.connected ? "Yes" : "No");
                    Serial.printf("Connection Handle: %d\n", connection_info.conn_handle);
                    Serial.printf("Device Address: '%s'\n", connection_info.address.c_str());
                    Serial.printf("Address Length: %d\n", connection_info.address.length());
                    Serial.printf("Connected Device Address: '%s'\n", connectedDeviceAddress.c_str());
                    Serial.println("=== End Connection Info ===\n");
                    
                    Serial.println("Device address set successfully");
                    Serial.println("=== End Setting Device Address ===");
                }
                else if (inputBuffer.startsWith("getpwdkey ")) {
                    // Получаем ключ из команды
                    String key = inputBuffer.substring(10);
                    key.trim();
                    
                    Serial.println("\n=== Password by Key ===");
                    Serial.printf("Key: %s\n", key.c_str());
                    
                    // Проверяем, начинается ли ключ с "pwd_"
                    if (!key.startsWith("pwd_")) {
                        key = "pwd_" + key;
                        Serial.printf("Adding prefix: %s\n", key.c_str());
                    }
                    
                    // Получаем пароль напрямую из NVS
                    nvs_handle_t localNvsHandle;
                    esp_err_t err = nvs_open("m5kb_v1", NVS_READWRITE, &localNvsHandle);
                    if (err == ESP_OK) {
                        size_t required_size;
                        err = nvs_get_str(localNvsHandle, key.c_str(), NULL, &required_size);
                        if (err == ESP_OK) {
                            Serial.printf("Password exists in NVS, size: %d bytes\n", required_size);
                            
                            // Получаем пароль
                            char* encrypted = new char[required_size];
                            err = nvs_get_str(localNvsHandle, key.c_str(), encrypted, &required_size);
                            if (err == ESP_OK) {
                                String password = decryptPassword(String(encrypted));
                                Serial.printf("Password: %s\n", password.c_str());
                                
                                // Выводим ASCII коды символов для отладки
                                Serial.print("ASCII codes: ");
                                for (int i = 0; i < password.length(); i++) {
                                    Serial.printf("%d ", (int)password[i]);
                                }
                                Serial.println();
                            }
                            delete[] encrypted;
                        } else {
                            Serial.printf("Password not found in NVS, error: %d\n", err);
                        }
                        nvs_close(localNvsHandle);
                    } else {
                        Serial.printf("Error opening NVS: %d\n", err);
                    }
                    
                    Serial.println("=== End Password by Key ===");
                }
                else if (inputBuffer == "silent") {
                    serialOutputEnabled = !serialOutputEnabled;
                    Serial.printf("Serial output %s\n", 
                        serialOutputEnabled ? "enabled" : "disabled");
                }
                else if (inputBuffer == "clear") {
                    clearAllPasswords();
                    Serial.println("Old passwords cleared. Please set new password if needed.");
                }
                // ... все остальные существующие команды ...
                
                Serial.println("=== End of command ===\n");
                inputBuffer = "";
            }
        } else {
            inputBuffer += c;
            Serial.print(c);
        }
    }
}

// Добавим константу для минимального интервала между попытками разблокировки
static const unsigned long UNLOCK_ATTEMPT_INTERVAL = 5000;  // 5 секунд
static unsigned long lastUnlockAttempt = 0;

// Добавим счетчик неудачных попыток
static int failedUnlockAttempts = 0;        // Счетчик неудачных попыток
static unsigned long lastFailedAttempt = 0;  // Время последней неудачной попытки

// Добавим константы для управления мощностью
static const esp_power_level_t POWER_NEAR_PC = ESP_PWR_LVL_N12;    // -12dBm минимальная
static const esp_power_level_t POWER_LOCKED = ESP_PWR_LVL_N12;     // Меняем на минимальную в блоке
static const esp_power_level_t POWER_RETURNING = ESP_PWR_LVL_P9;   // +9dBm максимальная

// Оставляем новые константы для интервалов сканирования
static const uint16_t SCAN_INTERVAL_NORMAL = 40;     // 25ms (40 * 0.625ms)
static const uint16_t SCAN_WINDOW_NORMAL = 20;       // 12.5ms
static const uint16_t SCAN_INTERVAL_LOCKED = 320;    // 200ms - сканируем реже
static const uint16_t SCAN_WINDOW_LOCKED = 16;       // 10ms - меньше слушаем

// Добавим константы для яркости
static const uint8_t BRIGHTNESS_MAX = 100;    // Максимальная яркость
static const uint8_t BRIGHTNESS_NORMAL = 50;  // Нормальная яркость
static const uint8_t BRIGHTNESS_LOW = 20;     // Пониженная яркость
static const uint8_t BRIGHTNESS_MIN = 0;      // Выключенный экран
static const uint8_t BRIGHTNESS_MEDIUM = 70;  // Средняя яркость для временного включения экрана

// Добавляем константы для яркости в начало файла
static const uint8_t BRIGHTNESS_USB = 100;     // Яркость при USB питании
static const uint8_t BRIGHTNESS_BATTERY = 30;  // Яркость от батареи

// Добавляем после других static переменных
static float batteryVoltage = 0;
static float batteryLevel = 0;
static const float VOLTAGE_THRESHOLD = -15.0;     // Допустимое падение напряжения
static const float DISCONNECT_THRESHOLD = -25.0;   // Порог для определения отключения
static const int VOLTAGE_HISTORY_SIZE = 10;
static float voltageHistory[VOLTAGE_HISTORY_SIZE] = {0};
static int historyIndex = 0;
static bool historyFilled = false;
static float maxAverageVoltage = 0;
static bool wasConnected = true;

// После других static переменных
static String currentShortKey = "";

// Глобальные переменные для управления временным включением экрана
static unsigned long screenOnTime = 0;
static bool screenTemporaryOn = false;

// Функция обновления состояния питания
void updatePowerStatus() {
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

float calculateAverageVoltage() {
    float sum = 0;
    int count = historyFilled ? VOLTAGE_HISTORY_SIZE : historyIndex;
    if (count == 0) return 0;
    
    for (int i = 0; i < count; i++) {
        sum += voltageHistory[i];
    }
    return sum / count;
}

bool isUSBConnected() {
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

// Функция для управления яркостью в зависимости от мощности передатчика
void adjustBrightness(esp_power_level_t txPower) {
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

// Функция для настройки параметров сканирования
void adjustScanParameters(DeviceState state) {
    if (!pScan) return;
    
    if (state == LOCKED) {
        pScan->setInterval(SCAN_INTERVAL_LOCKED);
        pScan->setWindow(SCAN_WINDOW_LOCKED);
    } else {
        pScan->setInterval(SCAN_INTERVAL_NORMAL);
        pScan->setWindow(SCAN_WINDOW_NORMAL);
    }
    
    if (pScan->isScanning()) {
        pScan->stop();
    }
    pScan->start(0, false);
}

// Функция для адаптивного управления мощностью
void adjustTransmitPower(DeviceState state, int rssi) {
    esp_power_level_t newPower;
    static unsigned long lastPowerChangeTime = 0;
    static esp_power_level_t lastPower = ESP_PWR_LVL_P9;
    static bool isPowerGraduallyReducing = false;
    
    // Получаем настройки для подключенного устройства
    DeviceSettings settings;
    bool hasCustomSettings = false;
    
    if (connected && connectedDeviceAddress.length() > 0) {
        settings = getDeviceSettings(connectedDeviceAddress.c_str());
        hasCustomSettings = true;
    }
    
    // Рассчитываем пороги RSSI на основе пользовательских настроек
    int veryCloseThreshold = -35;  // По умолчанию
    int closeThreshold = -45;
    int mediumThreshold = -55;
    int farThreshold = -65;
    
    if (hasCustomSettings) {
        // Настраиваем пороги относительно значений lock/unlock
        // Используем более щадящие пороги для обеспечения стабильной связи
        int range = settings.unlockRssi - settings.lockRssi;
        
        if (range > 5) {
            veryCloseThreshold = settings.unlockRssi;
            closeThreshold = settings.unlockRssi - (range / 4);
            mediumThreshold = settings.unlockRssi - (range / 2);
            farThreshold = settings.lockRssi + 5; // Чуть лучше чем порог блокировки
        }
    }
    
    // Определяем базовую мощность на основе RSSI
    esp_power_level_t basePower;
    if (rssi > veryCloseThreshold) {
        basePower = ESP_PWR_LVL_N9;  // Не используем самую низкую мощность N12 для надежности
    } else if (rssi > closeThreshold) {
        basePower = ESP_PWR_LVL_N6;
    } else if (rssi > mediumThreshold) {
        basePower = ESP_PWR_LVL_N3;
    } else if (rssi > farThreshold) {
        basePower = ESP_PWR_LVL_P3;
    } else {
        basePower = ESP_PWR_LVL_P9;  // Максимальная мощность
    }
    
    // Если находимся в состоянии блокировки или удаления, нужно обеспечить надежный сигнал
    if (state == MOVING_AWAY || state == LOCKED) {
        // Для состояния MOVING_AWAY используем повышенную мощность
        if (basePower < ESP_PWR_LVL_P3) {
            basePower = ESP_PWR_LVL_P3;
        }
    }
    
    // Учитываем состояние зарядки
    bool isUSB = isUSBConnected();
    
    if (!isUSB) {
        // При работе от батареи постепенно снижаем мощность, не делая резких скачков
        if (!isPowerGraduallyReducing && lastPower > basePower) {
            isPowerGraduallyReducing = true;
            lastPowerChangeTime = millis();
        } else if (isPowerGraduallyReducing) {
            // Проверяем, прошло ли достаточно времени для снижения мощности
            unsigned long currentTime = millis();
            if (currentTime - lastPowerChangeTime > 5000) { // 5 секунд между шагами снижения
                if (lastPower > basePower) {
                    // Снижаем мощность на один шаг
                    esp_power_level_t nextPower;
                    switch (lastPower) {
                        case ESP_PWR_LVL_P9: nextPower = ESP_PWR_LVL_P6; break;
                        case ESP_PWR_LVL_P6: nextPower = ESP_PWR_LVL_P3; break;
                        case ESP_PWR_LVL_P3: nextPower = ESP_PWR_LVL_N0; break;
                        case ESP_PWR_LVL_N0: nextPower = ESP_PWR_LVL_N3; break;
                        case ESP_PWR_LVL_N3: nextPower = ESP_PWR_LVL_N6; break;
                        case ESP_PWR_LVL_N6: nextPower = ESP_PWR_LVL_N9; break;
                        default: nextPower = basePower;
                    }
                    basePower = nextPower;
                    lastPowerChangeTime = currentTime;
                } else {
                    isPowerGraduallyReducing = false;
                }
            } else {
                // Пока не прошло достаточно времени, используем предыдущую мощность
                basePower = lastPower;
            }
        }
    } else {
        // При работе от USB можем сразу использовать рассчитанную мощность
        isPowerGraduallyReducing = false;
    }
    
    // Применяем рассчитанную мощность
    newPower = basePower;
    
    // Предотвращаем слишком низкую мощность при удалении
    if (state == MOVING_AWAY && rssi < settings.lockRssi + 5) {
        // Когда мы приближаемся к порогу блокировки, используем максимальную мощность
        // чтобы гарантировать отправку команды блокировки
        newPower = ESP_PWR_LVL_P9;
    }
    
    // Применяем новую мощность, если она изменилась
    if (newPower != lastPower) {
        if (serialOutputEnabled) {
            Serial.printf("Adjusting TX power: %d -> %d (RSSI: %d, State: %d, USB: %d)\n",
                lastPower, newPower, rssi, state, isUSB);
        }
        NimBLEDevice::setPower(newPower);
        lastPower = newPower;
    }
    
    // Регулируем яркость дисплея
    adjustBrightness(newPower);
}

// В начале файла после определений


void initStorage() {
    Serial.println("Initializing storage...");
    // Инициализация NVS Flash
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        Serial.printf("Error init NVS flash: %d\n", err);
    }
    // Открываем NVS namespace
    err = nvs_open("m5kb_v1", NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error opening NVS handle: %d\n", err);
        return;
    }
    
    // Проверяем есть ли начальные значения
    int8_t isLocked;
    err = nvs_get_i8(nvsHandle, "is_locked", &isLocked);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Устанавливаем начальные значения
        nvs_set_i8(nvsHandle, "is_locked", 0);
        nvs_set_str(nvsHandle, "last_addr", "");
        nvs_commit(nvsHandle);
        Serial.println("Initial values set");
    }
    
    Serial.println("Storage initialized successfully");
    // Загружаем пороги для последнего устройства при инициализации NVS
    {
        char lastDev[32] = {0};
        size_t len = sizeof(lastDev);
        if (nvs_get_str(nvsHandle, KEY_LAST_ADDR, lastDev, &len) == ESP_OK && strlen(lastDev) > 0) {
            String lastAddr = String(lastDev);
            DeviceSettings ds = getDeviceSettings(lastAddr);
            dynamicLockThreshold = ds.lockRssi;
            dynamicUnlockThreshold = ds.unlockRssi;
            if (serialOutputEnabled) {
                Serial.printf("initStorage loaded thresholds for %s: lock=%d, unlock=%d\n",
                    lastAddr.c_str(), dynamicLockThreshold, dynamicUnlockThreshold);
            }
        }
    }
}

void checkLockState() {
    Serial.println("Checking lock state...");
    
    int8_t isLocked = 0;
    char lastAddr[32] = {0};
    size_t length = sizeof(lastAddr);
    
    esp_err_t err = nvs_get_i8(nvsHandle, "is_locked", &isLocked);
    if (err != ESP_OK) {
        Serial.printf("Error reading lock state: %d\n", err);
        return;
    }
    
    err = nvs_get_str(nvsHandle, "last_addr", lastAddr, &length);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error reading last address: %d\n", err);
        return;
    }
    
    Serial.printf("Lock state: %s, Last addr: %s\n", 
        isLocked ? "LOCKED" : "UNLOCKED", 
        lastAddr);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    
    Serial.begin(115200);
    delay(100);
    
    Serial.println("\nStarting BLE Keyboard Test");
    
    // Инициализация дисплея для M5StickC Plus 2
    M5.Display.setRotation(3);
    Disbuff = new M5Canvas(&M5.Display);
    Disbuff->createSprite(M5.Display.width(), M5.Display.height());
    Disbuff->setTextSize(1);
    
    Serial.println("=== Initial NVS Setup ===");
    
    // Инициализируем NVS
    initStorage(); // Вызываем нашу функцию
    
    // Восстанавливаем проверку кнопки для очистки NVS
    bool clearNVS = false;  // По умолчанию не очищаем
    M5.update();
    if (M5.BtnA.isPressed()) {
        clearNVS = true;
        if (serialOutputEnabled) {
            Serial.println("Button A pressed at startup - clearing NVS...");
        }
    }
    
    // Очищаем NVS только если это явно запрошено
    if (clearNVS) {
        if (serialOutputEnabled) {
            Serial.println("Clearing NVS...");
        }
        nvs_flash_erase();
        nvs_flash_init();
        
        // Переоткрываем NVS handle после очистки
        initStorage(); 
    }
    
    // Перезапускаем BLE стек
    NimBLEDevice::deinit(true);
    delay(100);
    
    // Инициализация BLE
    if (serialOutputEnabled) {
        Serial.println("Initializing BLE...");
    }
    NimBLEDevice::init("M5Locker");
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Устанавливаем макс мощность для сопряжения

    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());

    hid = new NimBLEHIDDevice(bleServer);
    input = hid->getInputReport(1); // Исправляем
    output = hid->getOutputReport(1); // Исправляем

    hid->setManufacturer("M5Stack"); // Исправляем
    // hid->pnp(0x02, 0xe502, 0xa111, 0x0210); // Удаляем
    hid->setHidInfo(0x00, 0x01); // Исправляем
    hid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor)); // Исправляем
    hid->startServices();

    NimBLEAdvertising* pAdvertising; // Объявляем
    pAdvertising = bleServer->getAdvertising();
    pAdvertising->setAppearance(HID_KEYBOARD);
    pAdvertising->addServiceUUID(hid->getHidService()->getUUID()); // Исправляем
    // pAdvertising->setScanResponse(true); // Комментируем, т.к. метод setScanResponseData ожидает данные
    pAdvertising->start();

    Serial.println("Advertising started...");

    // Восстанавливаем состояние блокировки при запуске по последнему устройству
    {
        char lastAddrBuf[32] = {0};
        size_t lastLen = sizeof(lastAddrBuf);
        if (nvs_get_str(nvsHandle, KEY_LAST_ADDR, lastAddrBuf, &lastLen) == ESP_OK
            && strlen(lastAddrBuf) > 0) {
            String lastAddr = String(lastAddrBuf);
            bool wasLocked = loadDeviceLockState(lastAddr);
            currentState = wasLocked ? LOCKED : NORMAL;
            if (wasLocked && serialOutputEnabled) {
                Serial.printf("Restored lock state for %s: LOCKED\n", lastAddr.c_str());
            }
        } else {
            currentState = NORMAL;
        }
    }

    // Инициализация сканера
    pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(scanCallbacks); // Исправляем
    pScan->setActiveScan(true);
    pScan->setInterval(SCAN_INTERVAL_NORMAL);
    pScan->setWindow(SCAN_WINDOW_NORMAL);
    pScan->setDuplicateFilter(false);

    Serial.println("Setup complete");

    // Загружаем пороги для последнего устройства, если оно было спарено
    {
        char lastDev[32] = {0};
        size_t len = sizeof(lastDev);
        if (nvs_get_str(nvsHandle, KEY_LAST_ADDR, lastDev, &len) == ESP_OK && strlen(lastDev) > 0) {
            String lastAddr = String(lastDev);
            DeviceSettings ds = getDeviceSettings(lastAddr);
            dynamicLockThreshold = ds.lockRssi;
            dynamicUnlockThreshold = ds.unlockRssi;
            if (serialOutputEnabled) {
                Serial.printf("Setup loaded thresholds for %s: lock=%d, unlock=%d\n",
                    lastAddr.c_str(), dynamicLockThreshold, dynamicUnlockThreshold);
            }
        }
    }
    // Перезапускаем BLE стек
}

void loop() {
    static unsigned long lastUpdate = 0;
    static unsigned long lastCheck = 0;
    static unsigned long lastRssiCheck = 0;
    static bool lastRealState = false;
    static unsigned long lastDebugCheck = 0;
    static unsigned long lastVoltageCheck = 0;
    static unsigned long lastReconnectCheck = 0;
    static bool reconnectAttempted = false;
    
    M5.update();
    
    // Обработка нажатий кнопок
    if (M5.BtnA.isPressed()) {
        if (btnAPressStart == 0) {
            btnAPressStart = millis();
        }
    } else {
        if (btnAPressStart != 0) {
            unsigned long pressDuration = millis() - btnAPressStart;
            if (pressDuration >= LONG_PRESS_DURATION) {
                // Длительное нажатие: если сигнал достаточно слабый (устройство удалено), обновляем уровни
                if (lastAverageRssi <= -70) {
                    DeviceSettings settings = getDeviceSettings(connectedDeviceAddress.c_str());
                    int baseRssi = lastAverageRssi;
                    settings.unlockRssi = baseRssi + 10;
                    settings.lockRssi = baseRssi - 10;
                    saveDeviceSettings(connectedDeviceAddress.c_str(), settings);
                    Serial.println("Thresholds updated via long press on button A");
                    // Показываем сообщение на экране, пока кнопка A удерживается
                    while (M5.BtnA.isPressed()) {
                        Disbuff->fillSprite(BLACK);
                        Disbuff->setTextColor(GREEN);
                        Disbuff->setCursor(5, 60);
                        Disbuff->print("RSSI thresholds set");
                        Disbuff->pushSprite(0, 0);
                        delay(100);
                        M5.update();
                    }
                    // Обновляем динамические пороги
                    dynamicLockThreshold = settings.lockRssi;
                    dynamicUnlockThreshold = settings.unlockRssi;
                    if (serialOutputEnabled) {
                        Serial.printf("Dynamic thresholds updated via long press: lock=%d, unlock=%d\n",
                            dynamicLockThreshold, dynamicUnlockThreshold);
                    }
                    // Сохраняем адрес устройства и commit, чтобы thresholds применялись после перезагрузки
                    if (!connectedDeviceAddress.empty()) {
                        esp_err_t errAddr = nvs_set_str(nvsHandle, KEY_LAST_ADDR, connectedDeviceAddress.c_str());
                        if (errAddr == ESP_OK) {
                            nvs_commit(nvsHandle);
                        }
                    }
                } else {
                    Serial.println("Not far enough to update thresholds");
                }
            } else {
                // Короткое нажатие: временное включение экрана
                temporaryScreenOn();
            }
            btnAPressStart = 0;
        }
    }
    
    if (M5.BtnB.wasPressed()) {
        if (serialOutputEnabled) {
            Serial.println("Button B pressed - typing password");
        }
        
        // Обновляем короткий ключ на случай, если он не был установлен
        if (currentShortKey.length() == 0 && connectedDeviceAddress.length() > 0) {
            currentShortKey = cleanMacAddress(connectedDeviceAddress.c_str()); // Восстанавливаем вызов
            if (serialOutputEnabled) {
                Serial.printf("Updated current short key: %s\n", currentShortKey.c_str());
            }
        }
        
        if (serialOutputEnabled) {
            Serial.printf("Current short key: %s\n", currentShortKey.c_str());
        }
        
        // Получаем пароль для текущего устройства
        String password = getPasswordForDevice(currentShortKey);
        if (password.length() > 0) {
            if (serialOutputEnabled) {
                Serial.printf("Password found, length: %d\n", password.length());
            }
            typePassword(password);
        } else {
            if (serialOutputEnabled) {
                Serial.println("No password stored for current device");
            }
        }
    }
    
    // Проверяем, не пора ли выключить временно включенный экран
    checkTemporaryScreen();
    
    // Проверяем питание и яркость каждые 500мс
    if (millis() - lastVoltageCheck >= 500) {
        lastVoltageCheck = millis();
        updatePowerStatus();  // Обновляем данные о питании
        
        // Регулируем яркость в зависимости от питания
        esp_power_level_t currentPower = (esp_power_level_t)NimBLEDevice::getPower();
        adjustBrightness(currentPower);
    }
    
    // Проверка подключения
    if (millis() - lastCheck >= 500) {
        lastCheck = millis();
        bool realConnected = bleServer->getConnectedCount() > 0;
        
        if (realConnected != lastRealState) {
            lastRealState = realConnected;
            connected = realConnected;
            
            if (serialOutputEnabled) {
                Serial.printf("\n=== Connection state changed: %s ===\n", 
                    connected ? "Connected" : "Disconnected");
                Serial.printf("Connected count: %d\n", bleServer->getConnectedCount());
                Serial.printf("Advertising active: %s\n", 
                    bleServer->getAdvertising()->isAdvertising() ? "Yes" : "No");
                
                // Добавляем отладочную информацию о текущем состоянии connection_info
                Serial.println("\n=== Connection Info Debug ===");
                Serial.printf("Connected: %s\n", connection_info.connected ? "Yes" : "No");
                Serial.printf("Connection Handle: %d\n", connection_info.conn_handle);
                Serial.printf("Device Address: '%s'\n", connection_info.address.c_str());
                Serial.printf("Address Length: %d\n", connection_info.address.length());
                Serial.printf("Connected Device Address: '%s'\n", connectedDeviceAddress.c_str());
                Serial.println("=== End Connection Info Debug ===\n");
            }
            
            if (connected) {
                // Сбрасываем флаг попытки переподключения
                reconnectAttempted = false;
                
                NimBLEConnInfo connInfo = bleServer->getPeerInfo(0);
                connectedDeviceAddress = connInfo.getAddress().toString();
                
                if (serialOutputEnabled) {
                    Serial.printf("Connected device: %s\n", connectedDeviceAddress.c_str());
                }
                
                pScan = NimBLEDevice::getScan();
                pScan->stop();
                delay(100);
                
                pScan->setActiveScan(true);
                pScan->setInterval(40);
                pScan->setWindow(20);
                pScan->setScanCallbacks(scanCallbacks, true);
                pScan->clearResults();
                pScan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
                pScan->setDuplicateFilter(false);
                
                if(pScan->start(0, false)) {
                    scanMode = true;
                    if (serialOutputEnabled) {
                        Serial.println("Scan started successfully");
                    }
                } else {
                    if (serialOutputEnabled) {
                        Serial.println("Failed to start scan");
                    }
                }
            } else {
                // Если отключились, блокируем компьютер, если он еще не заблокирован
                if (currentState != LOCKED) {
                    if (serialOutputEnabled) {
                        Serial.println("Bluetooth connection lost. Locking computer...");
                    }
                    lockComputer();
                    currentState = LOCKED;
                    lastStateChangeTime = millis();
                }
                
                // Если отключились, проверяем состояние рекламы
                if (!bleServer->getAdvertising()->isAdvertising()) {
                    if (serialOutputEnabled) {
                        Serial.println("Advertising not active, restarting...");
                    }
                    
                    // Перезапускаем рекламу
                    int rc = bleServer->getAdvertising()->start();
                    if (serialOutputEnabled) {
                        Serial.printf("Restart advertising result: %d\n", rc);
                    }
                }
            }
        updateDisplay();
        }
    }
    
    // Проверка необходимости переподключения
    if (!connected && !reconnectAttempted) {
        // Проверяем, было ли устройство сопряжено ранее
        uint8_t isPaired = 0;
        if (nvs_get_u8(nvsHandle, "paired", &isPaired) == ESP_OK && isPaired) {
            // Если прошло 5 секунд с момента запуска и устройство не подключено,
            // пробуем перезапустить рекламу с другими параметрами
            if (millis() - lastReconnectCheck >= 5000) {
                lastReconnectCheck = millis();
                
                if (serialOutputEnabled) {
                    Serial.println("\n=== Attempting reconnection ===");
                }
                
                // Останавливаем текущую рекламу
                bleServer->getAdvertising()->stop();
                delay(100);
                
                // Настраиваем рекламу для переподключения
                NimBLEAdvertising* pAdvertising = bleServer->getAdvertising();
                pAdvertising->clearData();
                
                // Создаем новые объекты для рекламных данных
                NimBLEAdvertisementData advData;
                
                // Настраиваем рекламу для переподключения
                advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
                advData.setAppearance(0x03C1);  // Keyboard appearance
                advData.setCompleteServices(NimBLEUUID("1812"));  // HID Service
                
                // Устанавливаем данные рекламы
                int rc = pAdvertising->setAdvertisementData(advData);
                
                // Настраиваем данные сканирования
                NimBLEAdvertisementData scanResponse;
                scanResponse.setName("M5 HID");
                rc = pAdvertising->setScanResponseData(scanResponse);
                
                // Устанавливаем более короткие интервалы рекламы для быстрого переподключения
                pAdvertising->setMinInterval(0x06);  // 3.75ms
                pAdvertising->setMaxInterval(0x0C);  // 7.5ms
                
                // Запускаем рекламу
                rc = pAdvertising->start();
                if (serialOutputEnabled) {
                    Serial.printf("Reconnection advertising result: %d\n", rc);
                }
                
                // Устанавливаем флаг, что попытка переподключения была сделана
                reconnectAttempted = true;
            }
        }
    }
    
    // Обновление экрана
    if (millis() - lastUpdate >= 100) {
        lastUpdate = millis();
        updateDisplay();
    }
    
    // Сканирование и проверка RSSI
    if (scanMode) {
        static unsigned long lastRssiCheck = 0;
        static unsigned long lastRssiPrint = 0;
        
        if (millis() - lastRssiCheck >= 500) {  // Каждые 500мс
            lastRssiCheck = millis();
            
            if (connected && bleServer && bleServer->getConnectedCount() > 0) {
                NimBLEConnInfo connInfo = bleServer->getPeerInfo(0);
                int8_t rssi;
                if (ble_gap_conn_rssi(connInfo.getConnHandle(), &rssi) == 0) {
                    if (rssi != 0 && rssi != 127) {
                        // Создаем измерение
                        RssiMeasurement measurement = {
                            .value = rssi,
                            .timestamp = millis(),
                            .isValid = true
                        };
                        addRssiMeasurement(measurement);
                        
                        // Выводим в Serial реже
                        if (serialOutputEnabled && millis() - lastRssiPrint >= 1000) {
                            lastRssiPrint = millis();
                            Serial.printf("\nRSSI: %d dBm (avg: %d)\n", rssi, lastAverageRssi);
                        }
                    }
                }
            }
        }
    }
    
    // Обработка Serial
    echoSerialInput();
    
    delay(1);
    
    if (scanMode) {
        static unsigned long lastRssiCheck = 0;
        static unsigned long lastRssiDebug = 0;
        
        // Периодически выводим отладочную информацию о RSSI
        if (serialOutputEnabled && millis() - lastRssiDebug >= 10000) {  // Каждые 10 секунд
            lastRssiDebug = millis();
            Serial.println("\n=== RSSI Debug Info ===");
            Serial.printf("Current state: %s\n", 
                currentState == NORMAL ? "NORMAL" : 
                currentState == MOVING_AWAY ? "MOVING_AWAY" : 
                currentState == LOCKED ? "LOCKED" : "APPROACHING");
            Serial.printf("Current RSSI: %d dBm (filtered)\n", lastAverageRssi);
            Serial.printf("Lock threshold: %d dBm\n", dynamicLockThreshold);
            Serial.printf("Unlock threshold: %d dBm\n", dynamicUnlockThreshold);
            Serial.printf("Consecutive lock samples: %d/%d\n", consecutiveLockSamples, CONSECUTIVE_SAMPLES_NEEDED);
            Serial.printf("Consecutive unlock samples: %d/%d\n", consecutiveUnlockSamples, CONSECUTIVE_SAMPLES_NEEDED);
            Serial.printf("Time since last state change: %lu ms\n", millis() - lastStateChangeTime);
            Serial.printf("Signal stability: %s\n", isRssiStable() ? "STABLE" : "UNSTABLE");
            Serial.println("=== End RSSI Debug ===\n");
        }
        
        if (millis() - lastRssiCheck >= 500) {  // Каждые 500мс
            lastRssiCheck = millis();
            
            // Проверяем, прошло ли достаточно времени с момента последнего изменения состояния
            bool canChangeState = (millis() - lastStateChangeTime) > STATE_CHANGE_DELAY;
            
            // Проверяем стабильность сигнала
            bool stable = isRssiStable();
            
            // Логика блокировки компьютера
            if (currentState != LOCKED) {
                // Проверяем на очень слабый сигнал, который может привести к потере соединения
                if (lastAverageRssi < SIGNAL_CRITICAL_THRESHOLD) {
                    if (serialOutputEnabled) {
                        Serial.printf("Signal critically low (%d < %d), locking immediately...\n", 
                            lastAverageRssi, SIGNAL_CRITICAL_THRESHOLD);
                    }
                    lockComputer();
                    currentState = LOCKED;
                    lastStateChangeTime = millis();
                    consecutiveLockSamples = 0;
                    consecutiveUnlockSamples = 0;
                }
                // Обычная логика блокировки при удалении
                else if (lastAverageRssi < dynamicLockThreshold) {
                    consecutiveLockSamples++;
                    if (serialOutputEnabled) {
                        Serial.printf("Signal below lock threshold (%d < %d), sample %d/%d, stable=%s\n", 
                            lastAverageRssi, dynamicLockThreshold, consecutiveLockSamples, 
                            CONSECUTIVE_SAMPLES_NEEDED, stable ? "YES" : "NO");
                    }
                    
                    // Для блокировки требуем больше последовательных измерений
                    // Это предотвратит ложные блокировки из-за временных колебаний сигнала
                    int requiredSamples = CONSECUTIVE_SAMPLES_NEEDED + 1; // Увеличиваем количество требуемых измерений
                    
                    // Если достаточно последовательных измерений и прошло достаточно времени
                    // Для блокировки не требуем стабильности сигнала, так как при удалении сигнал становится нестабильным
                    if (consecutiveLockSamples >= requiredSamples && canChangeState) {
                        if (serialOutputEnabled) {
                            Serial.printf("Signal consistently below threshold for %d samples, locking...\n", 
                                consecutiveLockSamples);
                        }
                lockComputer();
                currentState = LOCKED;
                        lastStateChangeTime = millis();
                        consecutiveLockSamples = 0;
                        consecutiveUnlockSamples = 0;
                    }
                } else {
                    // Не сбрасываем счетчик полностью при небольших колебаниях сигнала
                    if (lastAverageRssi > dynamicLockThreshold + 5) {
                        // Сбрасываем счетчик только если сигнал значительно улучшился
                        consecutiveLockSamples = 0;
                    } else if (consecutiveLockSamples > 0) {
                        // Уменьшаем счетчик, но не сбрасываем полностью при небольших колебаниях
                        consecutiveLockSamples--;
                    }
                }
            }
            
            // Логика разблокировки компьютера
    if (currentState == LOCKED) {
                // Используем скользящее среднее для разблокировки, чтобы избежать ложных срабатываний
                if (lastAverageRssi > dynamicUnlockThreshold) {
                    consecutiveUnlockSamples++;
                    if (serialOutputEnabled) {
                        Serial.printf("Signal above unlock threshold (%d > %d), sample %d/%d, stable=%s\n", 
                            lastAverageRssi, dynamicUnlockThreshold, consecutiveUnlockSamples, 
                            CONSECUTIVE_SAMPLES_NEEDED, stable ? "YES" : "NO");
                    }
                    
                    // Для разблокировки требуем больше последовательных измерений и более длительное время
                    // Это предотвратит ложные разблокировки из-за временных колебаний сигнала
                    int requiredSamples = CONSECUTIVE_SAMPLES_NEEDED + 2; // Увеличиваем количество требуемых измерений
                    
                    // Если достаточно последовательных измерений, прошло достаточно времени и сигнал относительно стабилен
                    if (consecutiveUnlockSamples >= requiredSamples && canChangeState) {
                        if (serialOutputEnabled) {
                            Serial.printf("Signal consistently above threshold for %d samples, unlocking...\n", 
                                consecutiveUnlockSamples);
                        }
                    unlockComputer();
                        currentState = NORMAL;
                        lastStateChangeTime = millis();
                        consecutiveLockSamples = 0;
                        consecutiveUnlockSamples = 0;
                    }
                } else {
                    // Не сбрасываем счетчик полностью при небольших колебаниях сигнала
                    // Это позволит разблокировать устройство даже при небольших колебаниях сигнала
                    if (lastAverageRssi < dynamicUnlockThreshold - 5) {
                        // Сбрасываем счетчик только если сигнал значительно ухудшился
                        consecutiveUnlockSamples = 0;
                    } else if (consecutiveUnlockSamples > 0) {
                        // Уменьшаем счетчик, но не сбрасываем полностью при небольших колебаниях
                        consecutiveUnlockSamples--;
                    }
                }
            }
        }
    }
}

void lockComputer() {
    // Временно увеличиваем мощность для надежной отправки команды
    esp_power_level_t savedPower = (esp_power_level_t)NimBLEDevice::getPower();
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    delay(100);
    
    // Пытаемся отправить команду несколько раз
    bool success = false;
    for(int attempt = 0; attempt < 3 && !success; attempt++) {
        Serial.printf("Lock attempt %d, Power: %d, RSSI: %d\n", 
            attempt + 1, NimBLEDevice::getPower(), lastAverageRssi);
        
        // Отправляем Win+L
        uint8_t msg[] = {0x08, 0, 0x0F, 0, 0, 0, 0, 0};
        input->setValue(msg, sizeof(msg));
        success = input->notify();
        
        if (success) {
            delay(50);
            // Отпускаем клавиши
            uint8_t release[] = {0, 0, 0, 0, 0, 0, 0, 0};
            input->setValue(release, sizeof(release));
            input->notify();
            
            Serial.println("Lock command sent successfully!");
            break;
        }
        
        delay(100);
    }
    
    // После блокировки устанавливаем экономичную мощность
    delay(100);
    NimBLEDevice::setPower(POWER_LOCKED);
    
    if (success) {
        // Сохраняем состояние блокировки и адрес устройства
        saveDeviceLockState(connectedDeviceAddress.c_str(), true); // Сохраняем блокировку для текущего устройства
        Serial.println("Lock state saved to NVS");
    }
    
    if (!success) {
        Serial.println("Failed to send lock command!");
    }
}

// Добавляем функцию разблокировки
void unlockComputer() {
    static unsigned long lastCheck = 0;
    const unsigned long CHECK_INTERVAL = 1000; // Проверяем раз в секунду
    
    if (millis() - lastCheck < CHECK_INTERVAL) {
        return;  // Выходим если прошло мало времени
    }
    lastCheck = millis();
    
    // Добавляем отладочную информацию
    if (serialOutputEnabled) {
        Serial.println("\n=== Attempting to unlock computer ===");
        Serial.printf("Connected device address: %s\n", connectedDeviceAddress.c_str());
        Serial.printf("Current RSSI: %d\n", lastAverageRssi);
        Serial.printf("Current state: %d\n", currentState);
    }
    
    String password = getPasswordForDevice(connectedDeviceAddress.c_str());
    
    // Добавляем отладочную информацию о пароле
    if (serialOutputEnabled) {
        Serial.printf("Retrieved password length: %d\n", password.length());
        if (password.length() == 0) {
            Serial.println("No password found using getPasswordForDevice!");
            
            // Пробуем получить пароль напрямую из NVS
            String shortKey = getShortKey(connectedDeviceAddress.c_str());
            Serial.printf("Short key: %s\n", shortKey.c_str());
            
            nvs_handle_t nvsHandle;
            esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvsHandle);
            if (err == ESP_OK) {
                String pwdKey = "pwd_" + shortKey;
                Serial.printf("Password key: %s\n", pwdKey.c_str());
                
                size_t required_size;
                err = nvs_get_str(nvsHandle, pwdKey.c_str(), NULL, &required_size);
                if (err == ESP_OK) {
                    Serial.printf("Password exists in NVS, size: %d bytes\n", required_size);
                    
                    // Получаем пароль напрямую
                    char* encrypted = new char[required_size];
                    err = nvs_get_str(nvsHandle, pwdKey.c_str(), encrypted, &required_size);
                    if (err == ESP_OK) {
                        password = decryptPassword(String(encrypted));
                        Serial.printf("Direct password from NVS: %s\n", password.c_str());
                        
                        // Выводим ASCII коды символов для отладки
                        Serial.print("ASCII codes: ");
                        for (int i = 0; i < password.length(); i++) {
                            Serial.printf("%d ", (int)password[i]);
                        }
                        Serial.println();
                    }
                    delete[] encrypted;
                } else {
                    Serial.printf("Password not found in NVS, error: %d\n", err);
                }
                nvs_close(nvsHandle);
            }
            
            // Проверяем, есть ли сохраненные пароли
            Serial.println("Checking for any saved passwords...");
            listStoredDevices();
            
            // Если пароля нет, используем пароль по умолчанию для отладки
            if (password.length() == 0) {
                password = "12345";
                Serial.println("Using default password for debugging: 12345");
            }
        } else {
            Serial.println("Password found for this device");
            
            // Выводим ASCII коды символов для отладки
            Serial.print("ASCII codes: ");
            for (int i = 0; i < password.length(); i++) {
                Serial.printf("%d ", (int)password[i]);
            }
            Serial.println();
        }
    }
    
    if (password.length() == 0) {
        if (serialOutputEnabled) {
            Serial.println("Cannot unlock: no password stored. Use 'setpwd' command to set password.");
        }
        return;
    }
    
    // Сохраняем текущую мощность
    int8_t currentPower = NimBLEDevice::getPower();
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    delay(100);
    
    bool success = false;
    for(int attempt = 0; attempt < 3 && !success; attempt++) {
        Serial.printf("Unlock attempt %d\n", attempt + 1);
        
        // 1. Отправляем Ctrl+Alt+Del
        uint8_t ctrlAltDel[8] = {0x05, 0, 0x4C, 0, 0, 0, 0, 0};
        input->setValue(ctrlAltDel, sizeof(ctrlAltDel));
        success = input->notify();
        
        if (success) {
            delay(2000);  // Ждем появления экрана входа
            
            // 2. Вводим пароль
            typePassword(password);
            
            // 3. Сбрасываем состояние блокировки
            saveDeviceLockState(connectedDeviceAddress.c_str(), false); // Сохраняем разблокировку для текущего устройства
            currentState = NORMAL;
            
            Serial.println("Computer unlocked successfully!");
            break;
        }
        delay(100);
    }
    
    // Возвращаем исходную мощность
    NimBLEDevice::setPower((esp_power_level_t)currentPower);
    
    if (!success) {
        failedUnlockAttempts++;
        lastFailedAttempt = millis();
        
        if (failedUnlockAttempts >= 3) {
            Serial.println("Too many failed attempts. Locked for 5 minutes");
            Disbuff->fillSprite(BLACK);
            Disbuff->setTextColor(RED);
            Disbuff->setCursor(5, 40);
            Disbuff->print("LOCKED!");
            Disbuff->pushSprite(0, 0);
            
            delay(300000);  // Блокируем на 5 минут (300000 мс)
            failedUnlockAttempts = 0;  // Сбрасываем счетчик
        }
    } else {
        failedUnlockAttempts = 0;  // При успешной разблокировке сбрасываем счетчик
    }
}

void saveDeviceSettings(const char* deviceAddress, const DeviceSettings& settings) {
    Serial.println("\n=== Saving Device Settings ===");
    Serial.printf("Device address: %s\n", deviceAddress);
    
    esp_err_t err = nvs_set_str(nvsHandle, "last_addr", deviceAddress);
    if (err != ESP_OK) {
        Serial.printf("Error saving device address: %d\n", err);
        return;
    }
    
    // Сохраняем настройки с коротким ключом
    String shortKey = String(deviceAddress).substring(String(deviceAddress).length() - 8);
    Serial.printf("Short key: %s\n", shortKey.c_str());
    
    String pwdKey = "pwd_" + shortKey;
    Serial.printf("Password key: %s\n", pwdKey.c_str());
    
    err = nvs_set_str(nvsHandle, pwdKey.c_str(), settings.password.c_str());
    if (err != ESP_OK) {
        Serial.printf("Error saving password: %d\n", err);
        return;
    }
    
    String unlockKey = "unlock_" + shortKey;
    String lockKey = "lock_" + shortKey;
    Serial.printf("Unlock key: %s\n", unlockKey.c_str());
    Serial.printf("Lock key: %s\n", lockKey.c_str());
    
    err = nvs_set_i32(nvsHandle, unlockKey.c_str(), settings.unlockRssi);
    err |= nvs_set_i32(nvsHandle, lockKey.c_str(), settings.lockRssi);
    
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error committing settings: %d\n", err);
    } else {
        Serial.println("Settings saved successfully");
    }
    Serial.println("=== End Saving Settings ===\n");
}

DeviceSettings getDeviceSettings(const char* deviceAddress) {
    DeviceSettings settings;
    String shortKey = String(deviceAddress).substring(String(deviceAddress).length() - 8);
    
    char pwd[64] = {0};
    size_t length = sizeof(pwd);
    esp_err_t err = nvs_get_str(nvsHandle, ("pwd_" + shortKey).c_str(), pwd, &length);
    if (err == ESP_OK) {
        settings.password = String(pwd);
    }
    
    int32_t value;
    err = nvs_get_i32(nvsHandle, ("unlock_" + shortKey).c_str(), &value);
    if (err == ESP_OK) {
        settings.unlockRssi = value;
    }
    
    err = nvs_get_i32(nvsHandle, ("lock_" + shortKey).c_str(), &value);
    if (err == ESP_OK) {
        settings.lockRssi = value;
    }
    
    return settings;
}

void clearAllPreferences() {
    nvs_erase_all(nvsHandle);
    nvs_commit(nvsHandle);
    Serial.println("All preferences cleared");
} 

// Добавляем функции для работы с RSSI
RssiMeasurement getMeasuredRssi() {
    RssiMeasurement measurement = {0, millis(), false};
    
    if (serialOutputEnabled) {
        Serial.println("\n=== RSSI Measurement Start ===");
    }
    
    // Метод 1: Через активное сканирование
    if (pScan) {
        if (serialOutputEnabled) Serial.println("Trying scan method...");
        NimBLEScanResults results = pScan->getResults();
        if (serialOutputEnabled) Serial.printf("Found devices: %d\n", results.getCount());
        
        for (int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice* device = results.getDevice(i);
            if (device->getAddress().toString() == connectedDeviceAddress) {
                int rssi = device->getRSSI();
                if (serialOutputEnabled) {
                    Serial.printf("Device found, RSSI: %d\n", rssi);
                }
                if (rssi != 0 && rssi < 0) {
                    measurement.value = rssi;
                    measurement.isValid = true;
                    if (serialOutputEnabled) {
                        Serial.println("✓ Valid RSSI from scan");
                    }
                    return measurement;
                }
            }
        }
        if (serialOutputEnabled) Serial.println("✗ Scan method failed");
    }

    // Метод 2: Через колбэк сервера
    if (serialOutputEnabled) Serial.printf("Trying callback method, last RSSI: %d\n", lastAverageRssi);
    if (lastAverageRssi != 0 && lastAverageRssi < 0) {
        measurement.value = lastAverageRssi;
        measurement.isValid = true;
        if (serialOutputEnabled) {
            Serial.println("✓ Valid RSSI from callback");
        }
        return measurement;
    }
    if (serialOutputEnabled) Serial.println("✗ Callback method failed");

    // Метод 3: Через подключенное устройство
    if (bleServer && bleServer->getConnectedCount() > 0) {
        if (serialOutputEnabled) Serial.println("Trying client method...");
        NimBLEClient* pClient = NimBLEDevice::createClient();
        if (pClient) {
            int rssi = pClient->getRssi();
            if (serialOutputEnabled) {
                Serial.printf("Client RSSI: %d\n", rssi);
            }
            if (rssi != 0 && rssi < 0) {
                measurement.value = rssi;
                measurement.isValid = true;
                if (serialOutputEnabled) {
                    Serial.println("✓ Valid RSSI from client");
                }
            }
            NimBLEDevice::deleteClient(pClient);
            if (measurement.isValid) {
                return measurement;
            }
        }
        if (serialOutputEnabled) Serial.println("✗ Client method failed");
    }

    if (serialOutputEnabled) {
        Serial.println("✗ No valid RSSI measurement obtained");
        Serial.println("=== RSSI Measurement End ===\n");
    }
    return measurement;
}

void addRssiMeasurement(const RssiMeasurement& measurement) {
    if (!measurement.isValid) return;
    
    // Сохраняем измерение в истории
    rssiHistory[rssiHistoryIndex] = measurement;
    rssiHistoryIndex = (rssiHistoryIndex + 1) % RSSI_HISTORY_SIZE;
    
    // Добавляем значение в буфер для фильтрации
    addRssiValue(measurement.value);
    
    // Отладочный вывод
    static unsigned long lastRssiDebug = 0;
    if (serialOutputEnabled && millis() - lastRssiDebug >= 5000) {
        lastRssiDebug = millis();
        Serial.printf("\nRSSI: %d dBm (filtered avg: %d)\n", 
            measurement.value, lastAverageRssi);
    }
}

// Добавляем функцию для проверки стабильности сигнала
bool isRssiStable() {
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
    const float STABILITY_THRESHOLD = 6.0;  // Увеличен порог с 3.0 до 6.0 dBm
    bool isStable = stdDev < STABILITY_THRESHOLD;
    
    if (serialOutputEnabled) {
        static unsigned long lastStabilityCheck = 0;
        if (millis() - lastStabilityCheck >= 5000) {
            lastStabilityCheck = millis();
            Serial.printf("RSSI stability: StdDev=%.2f, Stable=%s\n", 
                stdDev, isStable ? "YES" : "NO");
        }
    }
    
    return isStable;
}

// Функция для временного включения экрана на среднюю яркость
void temporaryScreenOn() {
    // Включаем экран на среднюю яркость
    M5.Display.setBrightness(BRIGHTNESS_MEDIUM);
    screenOnTime = millis();
    screenTemporaryOn = true;
    
    if (serialOutputEnabled) {
        Serial.println("Screen temporarily turned on for 10 seconds");
        Serial.printf("Screen on time set to: %lu\n", screenOnTime);
    }
}

// Функция для проверки и выключения временно включенного экрана
void checkTemporaryScreen() {
    // Если экран был временно включен и прошло 10 секунд
    if (screenTemporaryOn && (millis() - screenOnTime >= 10000)) {
        if (serialOutputEnabled) {
            Serial.printf("Screen timeout check: current=%lu, on_time=%lu, diff=%lu\n", 
                millis(), screenOnTime, millis() - screenOnTime);
        }
        
        screenTemporaryOn = false;
        
        // Возвращаем яркость в зависимости от текущего состояния
        esp_power_level_t currentPower = (esp_power_level_t)NimBLEDevice::getPower();
        
        // Принудительно устанавливаем яркость в зависимости от питания
        if (isUSBConnected()) {
            M5.Display.setBrightness(BRIGHTNESS_USB);
            if (serialOutputEnabled) {
                Serial.printf("Forced brightness to USB level: %d\n", BRIGHTNESS_USB);
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

// Функция для получения пароля устройства из NVS
String getDevicePassword(const String& shortKey) {
    if (shortKey.length() == 0) {
        if (serialOutputEnabled) {
            Serial.println("Error: Empty device key");
        }
        return "";
    }
    
    if (serialOutputEnabled) {
        Serial.printf("Getting password for device with short key: %s\n", shortKey.c_str());
    }
    
    // Открываем хранилище
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
    if (err != ESP_OK) {
        if (serialOutputEnabled) {
            Serial.printf("Error opening NVS: %s\n", esp_err_to_name(err));
        }
        return "";
    }
    
    // Формируем ключ для пароля
    String passwordKey = "pwd_" + shortKey;
    
    if (serialOutputEnabled) {
        Serial.printf("Looking for password with key: %s\n", passwordKey.c_str());
    }
    
    // Получаем размер строки
    size_t required_size = 0;
    err = nvs_get_str(nvsHandle, passwordKey.c_str(), NULL, &required_size);
    if (err != ESP_OK) {
        if (serialOutputEnabled) {
            Serial.printf("Error getting password size: %s\n", esp_err_to_name(err));
        }
        nvs_close(nvsHandle);
        return "";
    }
    
    // Выделяем буфер и получаем строку
    char* buffer = (char*)malloc(required_size);
    if (buffer == NULL) {
        if (serialOutputEnabled) {
            Serial.println("Memory allocation failed");
        }
        nvs_close(nvsHandle);
        return "";
    }
    
    err = nvs_get_str(nvsHandle, passwordKey.c_str(), buffer, &required_size);
    if (err != ESP_OK) {
        if (serialOutputEnabled) {
            Serial.printf("Error getting password: %s\n", esp_err_to_name(err));
        }
        free(buffer);
        nvs_close(nvsHandle);
        return "";
    }
    
    String encryptedPassword = String(buffer);
    free(buffer);
    nvs_close(nvsHandle);
    
    if (serialOutputEnabled) {
        Serial.printf("Encrypted password retrieved, length: %d\n", encryptedPassword.length());
    }
    
    // Расшифровываем пароль
    String password = decryptPassword(encryptedPassword);
    
    if (serialOutputEnabled) {
        Serial.printf("Decrypted password, length: %d\n", password.length());
    }
    
    return password;
}

// Функция для обновления короткого ключа устройства
void updateCurrentShortKey(const char* deviceAddress) {
    currentShortKey = cleanMacAddress(deviceAddress); // Восстанавливаем вызов
    if (serialOutputEnabled) {
        Serial.printf("Updated current short key to: %s\n", currentShortKey.c_str());
    }
}

// Глобальный экземпляр колбэков сканирования
// static MyScanCallbacks* scanCallbacks = new MyScanCallbacks(); // <-- Comment out this line temporarily

// Инициализация BLE
void initBLE() {
    NimBLEDevice::init("");
    // ... existing code ...
}

// Функция clearOldPasswords была перенесена в модуль password_manager
// Используйте clearOldPasswords() из password_manager.h

#ifndef LONG_PRESS_DURATION
#define LONG_PRESS_DURATION 2000
#endif

// Сохраняет состояние блокировки для указанного устройства
static void saveDeviceLockState(const String& deviceAddress, bool locked) {
    String sk = cleanMacAddress(deviceAddress.c_str());
    String key = String(KEY_LOCK_STATE_PREFIX) + sk;
    nvs_set_i8(nvsHandle, key.c_str(), locked ? 1 : 0);
    nvs_commit(nvsHandle);
}

// Загружает состояние блокировки для указанного устройства
static bool loadDeviceLockState(const String& deviceAddress) {
    String sk = cleanMacAddress(deviceAddress.c_str());
    String key = String(KEY_LOCK_STATE_PREFIX) + sk;
    int8_t flag = 0;
    if (nvs_get_i8(nvsHandle, key.c_str(), &flag) == ESP_OK) {
        return flag != 0;
    }
    return false;
}