#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "esp_task_wdt.h"
#include "esp_gap_ble_api.h"
#include "NimBLEClient.h"
#include <M5Unified.h>
#include <nvs_flash.h>
#include <nvs.h>

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
void unlockComputer();
void lockComputer();
String getPasswordForDevice(const String& deviceAddress);
void clearAllPreferences();
String cleanMacAddress(const char* macAddress);  // Добавляем прототип

// Глобальные переменные и определения
extern nvs_handle_t nvsHandle;  // Объявляем как внешнюю переменную
static const int RSSI_FAR_THRESHOLD = -65;

// В начале файла, после всех включений и перед функциями

// Добавляем константы для RSSI по умолчанию
#define DEFAULT_LOCK_RSSI -60    // Порог RSSI для блокировки по умолчанию
#define DEFAULT_UNLOCK_RSSI -45  // Порог RSSI для разблокировки по умолчанию

// Добавляем глобальную переменную для управления отладочным выводом
static bool serialOutputEnabled = true;

// В начало файла после включения библиотек
static const char* KEY_IS_LOCKED = "is_locked";
static const char* KEY_LAST_ADDR = "last_addr";
static const char* KEY_PASSWORD = "password";  // Добавляем константу для пароля

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
static const uint8_t ENCRYPTION_KEY[] = {
    0x89, 0x4E, 0x1C, 0xE7, 0x3A, 0x5D, 0x2B, 0xF8,
    0x6C, 0x91, 0x0D, 0xB4, 0x7F, 0xE2, 0x9A, 0x3C,
    0x5E, 0x8D, 0x1B, 0xF4, 0x6A, 0x2C, 0x9E, 0x0B,
    0x7D, 0x4F, 0xA3, 0xE5, 0x8C, 0x1D, 0xB6, 0x3F
};

// Структура для хранения настроек устройства
struct DeviceSettings {
    int unlockRssi;     // Минимальный RSSI для разблокировки
    int lockRssi;       // RSSI для блокировки
    String password;    // Пароль (зашифрованный)
};



// Прототипы функций
void saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings);
DeviceSettings getDeviceSettings(const String& deviceAddress);
void addRssiMeasurement(const RssiMeasurement& measurement);

// Определения функций
void saveDeviceSettings(const String& deviceAddress, const DeviceSettings& settings) {
    Serial.println("\n=== Saving Device Settings ===");
    Serial.printf("Device address: %s\n", deviceAddress.c_str());
    
    String shortKey = cleanMacAddress(deviceAddress.c_str());
    Serial.printf("Short key: %s\n", shortKey.c_str());
    
    String pwdKey = "pwd_" + shortKey;
    String unlockKey = "unlock_" + shortKey;
    String lockKey = "lock_" + shortKey;
    
    Serial.printf("Password key: %s\n", pwdKey.c_str());
    Serial.printf("Unlock key: %s\n", unlockKey.c_str());
    Serial.printf("Lock key: %s\n", lockKey.c_str());
    
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
    settings.unlockRssi = RSSI_NEAR_THRESHOLD;
    settings.lockRssi = RSSI_FAR_THRESHOLD;
    
    String shortKey = cleanMacAddress(deviceAddress.c_str());
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
static const int RSSI_SAMPLES = 5;
static int rssiValues[RSSI_SAMPLES] = {0};  // Инициализируем нулями
static int rssiIndex = 0;
static int validSamples = 0;  // Счетчик валидных измерений

// Улучшенная функция для получения среднего RSSI
int getAverageRssi() {
    if (validSamples == 0) return 0;
    
    int sum = 0;
    int count = 0;
    
    // Считаем среднее только по валидным значениям
    for (int i = 0; i < RSSI_SAMPLES; i++) {
        if (rssiValues[i] != 0) {  // Пропускаем начальные нули
            sum += rssiValues[i];
            count++;
        }
    }
    
    return count > 0 ? sum / count : 0;
}

// При добавлении нового значения
void addRssiValue(int rssi) {
    if (rssi < -100 || rssi > 0) return;  // Отбрасываем невалидные значения
    
    rssiValues[rssiIndex] = rssi;
    rssiIndex = (rssiIndex + 1) % RSSI_SAMPLES;
    if (validSamples < RSSI_SAMPLES) validSamples++;
}

// После других static переменных, до функции updateDisplay()
static int lastMovementCount = 0;  // Счетчик для отслеживания движения

// Добавим функцию для обновления экрана
void updateDisplay() {
    int8_t isLocked = 0;
    nvs_get_i8(nvsHandle, KEY_IS_LOCKED, &isLocked);
    
    Disbuff->fillSprite(BLACK);
    Disbuff->setTextSize(2);
    
    // BLE статус (5px)
    Disbuff->setTextColor(connected ? GREEN : RED);
    Disbuff->setCursor(5, 5);
    Disbuff->printf("BLE:%s", connected ? "OK" : "NO");
    
    if (connected) {
        // RSSI (20px)
        Disbuff->setTextColor(WHITE);
        Disbuff->setCursor(5, 20);
        Disbuff->printf("RS:%d", lastAverageRssi);
        
        // Среднее RSSI (35px)
        Disbuff->setCursor(5, 35);
        Disbuff->printf("AV:%d", lastAverageRssi);
        
        // Состояние (50px)
        Disbuff->setCursor(5, 50);
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
            Disbuff->setCursor(5, 65);
            Disbuff->setTextColor(YELLOW);
            Disbuff->printf("CNT:%d/%d", lastMovementCount, MOVEMENT_SAMPLES);
        }
        
        // Разница RSSI (80px)
        Disbuff->setCursor(5, 80);
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
        Disbuff->setCursor(5, 95);
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
        
        // Пароль (110px)
        Disbuff->setCursor(5, 110);
        if (getPasswordForDevice(connectedDeviceAddress.c_str()).length() > 0) {
            Disbuff->setTextColor(GREEN);
            Disbuff->print("PWD:OK");
        } else {
            Disbuff->setTextColor(RED);
            Disbuff->print("PWD:NO");
        }
    }
    
    Disbuff->pushSprite(0, 0);
}

// Сначала объявляем класс для колбэков сканирования
class MyScanCallbacks: public NimBLEScanCallbacks {
private:
    NimBLEAdvertisedDevice* targetDevice = nullptr;
    
public:
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        // Проверяем, является ли это HID устройством
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

// Затем класс для колбэков сервера
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        Serial.printf("\n!!! onConnect CALLED for device: %s !!!\n", 
            NimBLEAddress(desc->peer_ota_addr).toString().c_str());
            
        // Сохраняем всю информацию о подключении
        connection_info.connected = true;
        connection_info.conn_handle = desc->conn_handle;
        connection_info.address = NimBLEAddress(desc->peer_ota_addr).toString();
        
        Serial.printf("Connection handle: %d\n", connection_info.conn_handle);
        Serial.printf("Device address: %s\n", connection_info.address.c_str());
        
        connected = true;
        connectedDeviceAddress = connection_info.address;
        updateDisplay();
        
        // После подключения запускаем сканирование
        delay(1000);
        Serial.println("\n=== Starting scan after connection ===");
        pScan = NimBLEDevice::getScan();
        pScan->setActiveScan(true);
        pScan->setInterval(100);
        pScan->setWindow(50);
        pScan->setScanCallbacks(scanCallbacks, true);
        bool started = pScan->start(0);
        Serial.printf("Scan started: %d\n", started);
        scanMode = true;
    }

    void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        Serial.println("\n!!! onDisconnect CALLED !!!\n");
        Serial.println("=== onDisconnect called ===");
        Serial.printf("Connected count: %d\n", pServer->getConnectedCount());
        Serial.printf("Connected flag: %d\n", connected);
        
        connected = false;
        updateDisplay();
        pServer->startAdvertising();
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
String encryptPassword(const String& password) {
    String result = "";
    for(size_t i = 0; i < password.length(); i++) {
        uint8_t c = password[i];
        c = c ^ ENCRYPTION_KEY[i % 32];
        c = c ^ ENCRYPTION_KEY[(i + 7) % 32];
        c = c ^ ENCRYPTION_KEY[(i * 13 + 5) % 32];
        result += char(c);
    }
    return result;
}

String decryptPassword(const String& encrypted) {
    return encryptPassword(encrypted);
}

void clearAllPasswords() {
    nvs_erase_all(nvsHandle);
    nvs_commit(nvsHandle);
    Serial.println("All passwords cleared");
}

// Функция для получения пароля по адресу устройства
String getPasswordForDevice(const String& deviceAddress) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    return settings.password.length() > 0 ? decryptPassword(settings.password) : "";
}

// Функция для сохранения пароля
void savePasswordForDevice(const String& deviceAddress, const String& password) {
    DeviceSettings settings = getDeviceSettings(deviceAddress);
    settings.password = encryptPassword(password);
    saveDeviceSettings(deviceAddress, settings);
    
    if (serialOutputEnabled) {
        Serial.printf("Saving password for device: %s\n", deviceAddress.c_str());
        Serial.printf("Password length: %d\n", password.length());
        Serial.printf("Encrypted length: %d\n", settings.password.length());
    }
}

// Вспомогательная функция для получения короткого ключа из MAC-адреса
String getShortKey(const char* macAddress) {
    // Убираем двоеточия и берем последние 8 символов
    String cleanMac = String(macAddress);
    cleanMac.replace(":", "");
    return cleanMac.substring(cleanMac.length() - 8);
}

String cleanMacAddress(const char* macAddress) {
    String result = String(macAddress);
    result.replace(":", "");
    return result.substring(result.length() - 8);
}

void listStoredDevices() {
    Serial.println("\nStored devices:");
    Serial.println("=== Debug Info ===");
    
    String currentShortKey = "";
    
    // Показываем текущее подключенное устройство
    Serial.println("Checking connected device:");
    if (connected) {
        Serial.printf("  Connected device: %s\n", connectedDeviceAddress.c_str());
        currentShortKey = cleanMacAddress(connectedDeviceAddress.c_str());
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
            lastShortKey = cleanMacAddress(lastAddr);
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
    
    // Преобразование ASCII в HID код
    if (key >= 'a' && key <= 'z') {
        keyCode = 4 + (key - 'a');
    } else if (key >= 'A' && key <= 'Z') {
        keyCode = 4 + (key - 'A');
        modifiers = 0x02;
    } else if (key >= '1' && key <= '9') {
        keyCode = 30 + (key - '1');
    } else if (key == '0') {
        keyCode = 39;
    }
    
    if (keyCode > 0) {
        uint8_t msg[8] = {modifiers, 0, keyCode, 0, 0, 0, 0, 0};  // Используем modifiers напрямую
        input->setValue(msg, sizeof(msg));
        input->notify();
        delay(50);
        
        // Отпускаем клавишу
        uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        input->setValue(release, sizeof(release));
        input->notify();
        delay(50);
    }
}

// Функция ввода пароля
void typePassword(const String& password) {
    for (char c : password) {
        sendKey(c);
    }
    // Отправляем Enter
    uint8_t enter[8] = {0, 0, 0x28, 0, 0, 0, 0, 0};
    input->setValue(enter, sizeof(enter));
    input->notify();
    delay(50);
    
    // Отпускаем Enter
    uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    input->setValue(release, sizeof(release));
    input->notify();
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
    settings.unlockRssi = baseRssi - 10;
    settings.lockRssi = baseRssi - 25;
    
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
    Serial.printf("Unlock RSSI   : %d (-10 from base)\n", settings.unlockRssi);
    Serial.printf("Lock RSSI     : %d (-25 from base)\n", settings.lockRssi);
    Serial.printf("Critical level: %d (-35 from base)\n", baseRssi - 35);
}

// Добавим функцию для эхо ввода
void echoSerialInput() {
    static String inputBuffer = "";
    
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                Serial.println("\n=== Command execution ===");
                
                // Обрабатываем команду
                if (inputBuffer == "setpwd") {
                    if (connected) {
                        setPasswordFromSerial();
                    } else {
                        Serial.println("Error: No device connected!");
                    }
                } else if (inputBuffer == "getpwd") {
                    if (connected) {
                        String pwd = getPasswordForDevice(connectedDeviceAddress.c_str());
                        Serial.printf("Device: %s\n", connectedDeviceAddress.c_str());
                        if (pwd.length() > 0) {
                            Serial.println("Password is SET");
                        } else {
                            Serial.println("Password is NOT SET");
                        }
                    } else {
                        Serial.println("Error: No device connected!");
                    }
                } else if (inputBuffer == "list") {
                    listStoredDevices();
                } else if (inputBuffer == "silent") {
                    serialOutputEnabled = !serialOutputEnabled;
                    Serial.printf("Serial output %s\n", 
                        serialOutputEnabled ? "enabled" : "disabled");
                } else if (inputBuffer == "help") {
                    Serial.println("\nAvailable commands:");
                    Serial.println("setpwd  - Set password for current device");
                    Serial.println("getpwd  - Show current password");
                    Serial.println("list    - Show stored devices");
                    Serial.println("silent  - Toggle debug output");
                    Serial.println("setrssl - Set RSSI threshold for locking");
                    Serial.println("setrssu - Set RSSI threshold for unlocking");
                    Serial.println("showrssi- Show current RSSI settings");
                    Serial.println("unlock  - Clear lock state");
                    Serial.println("clear   - Clear all stored preferences");
                    Serial.println("help    - Show this help");
                } else if (inputBuffer == "unlock") {
                    nvs_set_i8(nvsHandle, KEY_IS_LOCKED, 0);
                    nvs_commit(nvsHandle);
                    currentState = NORMAL;
                    Serial.println("Lock state cleared");
                    updateDisplay();
                } else if (inputBuffer == "clear") {
                    clearAllPreferences();
                    Serial.println("All preferences cleared");
                } else if (inputBuffer == "test") {
                    // Тест шифрования
                    String testPass = "MyTestPassword123";
                    String encrypted = encryptPassword(testPass);
                    String decrypted = decryptPassword(encrypted);
                    
                    Serial.println("\nEncryption test:");
                    Serial.printf("Original : %s\n", testPass.c_str());
                    Serial.printf("Encrypted: ");
                    for(char c : encrypted) {
                        Serial.printf("%02X ", (uint8_t)c);
                    }
                    Serial.println();
                    Serial.printf("Decrypted: %s\n", decrypted.c_str());
                    Serial.printf("Test %s\n", testPass == decrypted ? "PASSED" : "FAILED");
                } else if (inputBuffer == "setrssl" || inputBuffer == "setrssu") {
                    if (connected) {
                        bool isLock = (inputBuffer == "setrssl");
                        Serial.printf("Enter RSSI value for %s threshold (-30 to -90):\n", 
                            isLock ? "LOCK" : "UNLOCK");
                        
                        // Очищаем буфер Serial
                        while(Serial.available()) {
                            Serial.read();
                        }
                        
                        String rssiStr = "";
                        while (true) {
                            if (Serial.available()) {
                                char c = Serial.read();
                                if (c == '\n' || c == '\r') {
                                    if (rssiStr.length() > 0) {  // Проверяем, что что-то введено
                                        break;
                                    }
                                } else {
                                    rssiStr += c;
                                    Serial.print(c);  // Эхо ввода
                                }
                            }
                            delay(10);
                        }
                        
                        int rssi = rssiStr.toInt();
                        if (rssi >= -90 && rssi <= -30) {
                            DeviceSettings settings = getDeviceSettings(connectedDeviceAddress.c_str());
                            if (isLock) {
                                settings.lockRssi = rssi;
                            } else {
                                settings.unlockRssi = rssi;
                            }
                            saveDeviceSettings(connectedDeviceAddress.c_str(), settings);
                            Serial.printf("\n%s RSSI threshold set to %d\n", 
                                isLock ? "Lock" : "Unlock", rssi);
                        } else {
                            Serial.printf("\nInvalid RSSI value: %d! Must be between -90 and -30\n", rssi);
                        }
                    } else {
                        Serial.println("Error: No device connected!");
                    }
                } else if (inputBuffer == "showrssi") {
                    if (connected) {
                        DeviceSettings settings = getDeviceSettings(connectedDeviceAddress.c_str());
                        Serial.printf("\nDevice: %s\n", connectedDeviceAddress.c_str());
                        Serial.printf("Unlock RSSI: %d\n", settings.unlockRssi);
                        Serial.printf("Lock RSSI: %d\n", settings.lockRssi);
                        Serial.printf("Current RSSI: %d\n", lastAverageRssi);
                    }
                } else {
                    Serial.println("Unknown command. Type 'help' for available commands.");
                }
                
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
    
    if (rssi > -35) {  // Очень близко
        newPower = ESP_PWR_LVL_N12;  // Минимальная мощность
    } else if (rssi > -45) {  // Близко
        newPower = ESP_PWR_LVL_N9;
    } else if (rssi > -55) {  // Средняя дистанция
        newPower = ESP_PWR_LVL_N3;
    } else if (rssi > -65) {  // Дальняя дистанция
        newPower = ESP_PWR_LVL_P3;
    } else {  // Очень далеко
        newPower = ESP_PWR_LVL_P9;  // Максимальная мощность
    }
    
    NimBLEDevice::setPower(newPower);
    adjustBrightness(newPower);  // Регулируем яркость
}

// В начале файла после определений
nvs_handle_t nvsHandle;

void initStorage() {
    Serial.println("Initializing storage...");
    esp_err_t err = nvs_open("m5kb_v1", NVS_READWRITE, &nvsHandle);
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

// Функция для установки состояния блокировки
void setLockState(bool locked) {
    esp_err_t err = nvs_set_i8(nvsHandle, KEY_IS_LOCKED, locked ? 1 : 0);
    if (err != ESP_OK) {
        Serial.printf("Error setting lock state: %d\n", err);
        return;
    }
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error committing lock state: %d\n", err);
    }
}

// Функция для получения состояния блокировки
bool getLockState() {
    int8_t locked = 0;
    esp_err_t err = nvs_get_i8(nvsHandle, KEY_IS_LOCKED, &locked);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("Error getting lock state: %d\n", err);
    }
    return locked != 0;
}

// Функция для очистки всех настроек
void clearAllSettings() {
    Serial.println("Clearing all settings...");
    esp_err_t err = nvs_erase_all(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error clearing settings: %d\n", err);
        return;
    }
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("Error committing clear: %d\n", err);
    } else {
        Serial.println("All settings cleared");
    }
}

// В начале файла добавим глобальную переменную для хранения найденного устройства
static NimBLEAdvertisedDevice* targetDevice = nullptr;

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
    Disbuff->setTextSize(2);
    
    Serial.println("=== Initial HID Setup ===");
    
    // Инициализируем NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("Erasing NVS flash...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        Serial.printf("Failed to initialize NVS: %d\n", ret);
        nvs_close(nvsHandle);  // Закрываем handle если была ошибка
    } else {
        Serial.println("NVS initialized successfully");
        ret = nvs_open("m5kb_v1", NVS_READWRITE, &nvsHandle);
        if (ret != ESP_OK) {
            Serial.printf("Error opening NVS handle: %d\n", ret);
        } else {
            Serial.println("Storage initialized successfully");
        }
    }
    
    // Инициализация BLE
    Serial.println("1. Initializing BLE...");
    NimBLEDevice::init("M5 BLE KB");
    
    // Настраиваем BLE для лучшей производительности
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(185);  // Оптимальный MTU
    
    // Настройки безопасности
    Serial.println("2. Setting up security...");
    NimBLEDevice::setSecurityAuth(
        BLE_SM_PAIR_AUTHREQ_BOND |     // Сохранение связи
        BLE_SM_PAIR_AUTHREQ_MITM |     // Защита от MITM атак
        BLE_SM_PAIR_AUTHREQ_SC);       // Secure Connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);  // Устройство без ввода/вывода
    
    // Создаем HID сервер
    Serial.println("3. Creating server...");
    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());
    
    // Теперь настраиваем сканирование
    pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(scanCallbacks);
    pScan->setActiveScan(true);
    pScan->setInterval(40);
    pScan->setWindow(30);
    
    if (serialOutputEnabled) {
        Serial.println("Starting initial scan for HID devices...");
    }
    
    // Запускаем начальное сканирование
    pScan->start(0, false);
    
    // Настройка рекламы
    Serial.println("5. Starting advertising...");
    NimBLEAdvertising* pAdvertising = bleServer->getAdvertising();
    
    advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advData.setAppearance(0x03C1);
    advData.setName("M5 BLE KB");
    advData.setCompleteServices(NimBLEUUID("1812"));
    
    pAdvertising->setAdvertisementData(advData);
    pAdvertising->setMinInterval(0x20);
    pAdvertising->setMaxInterval(0x40);
    
    pAdvertising->start();
    Serial.println("=== Setup complete ===\n");
    
    // Проверяем состояние BLE согласно документации NimBLE
    Serial.println("\n=== Checking BLE Status ===");
    Serial.printf("BLE Address: %s\n", NimBLEDevice::getAddress().toString().c_str());
    Serial.printf("TX Power: %d\n", NimBLEDevice::getPower());
    
    // Проверяем состояние сканера
    pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(160);    // 100ms (160 * 0.625ms)
    pScan->setWindow(80);       // 50ms (80 * 0.625ms)
    pScan->setScanCallbacks(new MyScanCallbacks());
    
    // Явно устанавливаем начальное состояние как NORMAL
    currentState = NORMAL;  
    
    // Проверяем предыдущее состояние
    int8_t wasLocked = 0;
    char lastAddr[32] = {0};
    size_t length = sizeof(lastAddr);
    
    if (nvs_get_i8(nvsHandle, KEY_IS_LOCKED, &wasLocked) == ESP_OK &&
        nvs_get_str(nvsHandle, KEY_LAST_ADDR, lastAddr, &length) == ESP_OK) {
        
        if (wasLocked && strlen(lastAddr) > 0) {
            currentState = LOCKED;
            connectedDeviceAddress = lastAddr;
            Serial.println("\n=== Previous state was LOCKED ===");
            Serial.printf("Last connected device: %s\n", lastAddr);
        } else {
            nvs_set_i8(nvsHandle, KEY_IS_LOCKED, 0);
            nvs_commit(nvsHandle);
        }
    }
    
    // Начинаем в режиме HID
    scanMode = false;
    
    // Добавим задержку
    delay(100);
    
    // Инициализация истории измерений
    updatePowerStatus();  // Получаем первое измерение
    for (int i = 0; i < VOLTAGE_HISTORY_SIZE; i++) {
        voltageHistory[i] = batteryVoltage;  // Заполняем историю текущим значением
    }
    historyFilled = true;
    maxAverageVoltage = batteryVoltage;  // Инициализируем максимальное среднее
    
    // Инициализация сканирования
    pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(scanCallbacks);  // Устанавливаем колбэки до старта
    pScan->setActiveScan(true);
    pScan->setInterval(20);
    pScan->setWindow(15);
    pScan->setDuplicateFilter(false);
    
    if (serialOutputEnabled) {
        Serial.println("Scan callbacks set");
    }
}

void loop() {
    static unsigned long lastUpdate = 0;
    static unsigned long lastCheck = 0;
    static unsigned long lastRssiCheck = 0;
    static bool lastRealState = false;
    static unsigned long lastDebugCheck = 0;
    static unsigned long lastVoltageCheck = 0;
    
    M5.update();
    
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
            
            if (connected) {
                NimBLEConnInfo connInfo = bleServer->getPeerInfo(0);
                connectedDeviceAddress = connInfo.getAddress().toString();
                
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
                }
            }
            updateDisplay();
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
        
        if (millis() - lastRssiCheck >= 200) {
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
        if (millis() - lastRssiCheck >= 200) {
            lastRssiCheck = millis();
            
            // После обновления RSSI проверяем необходимость блокировки
            if (lastAverageRssi < RSSI_LOCK_THRESHOLD && currentState != LOCKED) {
                if (serialOutputEnabled) {
                    Serial.printf("Signal below threshold (%d < %d), locking...\n", 
                        lastAverageRssi, RSSI_LOCK_THRESHOLD);
                }
                lockComputer();
                currentState = LOCKED;
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
        nvs_set_i8(nvsHandle, KEY_IS_LOCKED, 1);
        nvs_commit(nvsHandle);
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
    
    String password = getPasswordForDevice(connectedDeviceAddress.c_str());
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
            nvs_set_i8(nvsHandle, KEY_IS_LOCKED, 0);
            nvs_commit(nvsHandle);
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
    
    rssiHistory[rssiHistoryIndex] = measurement;
    rssiHistoryIndex = (rssiHistoryIndex + 1) % RSSI_HISTORY_SIZE;
    
    // Обновляем среднее значение
    int sum = 0;
    int count = 0;
    uint32_t currentTime = millis();
    
    for (int i = 0; i < RSSI_HISTORY_SIZE; i++) {
        if (rssiHistory[i].isValid && 
            (currentTime - rssiHistory[i].timestamp) < 5000) {
            sum += rssiHistory[i].value;
            count++;
        }
    }
    
    if (count > 0) {
        lastAverageRssi = sum / count;
        if (serialOutputEnabled) {
            Serial.printf("Updated average RSSI: %d (from %d measurements)\n", lastAverageRssi, count);
        }
    }
}