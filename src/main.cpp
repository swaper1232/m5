#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "esp_task_wdt.h"
#include "esp_gap_ble_api.h"
#include "NimBLEClient.h"
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEBeacon.h>
#include <NimBLEScan.h>
#include <M5StickCPlus2.h>
#include <M5Unified.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "device/DeviceLockState.h"
#include "device/DeviceSettings.h"
#include "device/ServerCallbacks.h"
#include "rssi/RSSIManager.h"

#define NVS_NAMESPACE "m5kb_v1"

// Глобальные переменные и определения
extern nvs_handle_t nvsHandle;
static bool serialOutputEnabled = true;
static const char* KEY_IS_LOCKED = "is_locked";
static const char* KEY_LAST_ADDR = "last_addr";
static const char* KEY_PASSWORD = "password";

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
static M5Canvas* Disbuff = nullptr;
static String lastMovement = "==";

// Добавим переменную для режима работы
static bool scanMode = false;

// Добавим переменную для хранения адреса подключенного устройства
static std::string connectedDeviceAddress = "";

// Константы для определения движения
static const int MOVEMENT_SAMPLES = 10;     // Сколько измерений для определения движения
static const int MOVEMENT_TIME = 3000;      // Время движения для блокировки (3 секунды)

// Константы для определения потери сигнала
static const int SIGNAL_LOSS_TIME = 5000;      // 5 секунд слабого сигнала для блокировки
static unsigned long weakSignalStartTime = 0;   // Время начала слабого сигнала

// Пороги для определения расстояния
static const int SAMPLES_TO_CONFIRM = 5;       // Увеличиваем до 5 измерений

// Добавляем переменные для стабилизации изменений состояния
static const unsigned long STATE_CHANGE_DELAY = 20000;  // Минимальное время между изменениями состояния
static unsigned long lastStateChangeTime = 0;           // Время последнего изменения состояния
static int consecutiveUnlockSamples = 0;               // Счетчик последовательных измерений для разблокировки
static int consecutiveLockSamples = 0;                 // Счетчик последовательных измерений для блокировки
static const int CONSECUTIVE_SAMPLES_NEEDED = 3;        // Сколько последовательных измерений нужно для изменения состояния

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

// Колбэк для RSSI
void onRssiUpdate(NimBLEServer* pServer, int rssi) {
    RSSIManager::getInstance().addMeasurement(rssi);
    if (serialOutputEnabled) {
        Serial.printf("RSSI Update: %d dBm\n", rssi);
    }
}

// Функция определения необходимости блокировки
bool shouldLockComputer() {
    if (!connected) return false;
    if (currentState == LOCKED) return false;
    
    auto& deviceSettings = DeviceSettingsManager::getInstance();
    DeviceSettings settings = deviceSettings.getDeviceSettings(connectedDeviceAddress.c_str());
    static bool wasNear = false;
    static int samplesBelow = 0;
    static bool lockSent = false;
    
    auto& rssiManager = RSSIManager::getInstance();
    int currentRssi = rssiManager.getAverageRssi();
    
    if (currentRssi > settings.unlockRssi) {
        wasNear = true;
        samplesBelow = 0;
        lockSent = false;
        return false;
    }
    
    if (wasNear && currentRssi < settings.lockRssi) {
        samplesBelow++;
        if (serialOutputEnabled) {
            Serial.printf("Signal below threshold: %d/%d samples\n", 
                samplesBelow, SAMPLES_TO_CONFIRM);
        }
            
        if (samplesBelow >= SAMPLES_TO_CONFIRM && !lockSent && rssiManager.isStable()) {
            if (serialOutputEnabled) {
                Serial.println("!!! Lock condition detected !!!");
            }
            return true;
        }
    } else {
        samplesBelow = 0;
    }
    
    return false;
}

void updateDisplay() {
    auto& rssiManager = RSSIManager::getInstance();
    
    // RSSI (20px)
    Disbuff->setCursor(0, 20);
    Disbuff->setTextSize(2);
    Disbuff->printf("RS:%d", rssiManager.getAverageRssi());
    
    // Среднее RSSI (35px)
    Disbuff->setCursor(0, 35);
    Disbuff->printf("AV:%d", rssiManager.getAverageRssi());
    
    // Разница RSSI (80px)
    Disbuff->setCursor(0, 80);
    Disbuff->setTextSize(2);
    int lastRssiDiff = rssiManager.getLastDifference();
    String movement = rssiManager.getMovementIndicator();
    
    if (movement == "<<") {
        Disbuff->setTextColor(RED);
        Disbuff->printf("<<:%d", abs(lastRssiDiff));
    } else if (movement == ">>") {
        Disbuff->setTextColor(GREEN);
        Disbuff->printf(">>:%d", lastRssiDiff);
    } else {
        Disbuff->setTextColor(WHITE);
        Disbuff->printf("==:%d", lastRssiDiff);
    }
    
    // Обновляем индикатор сигнала
    if (rssiManager.isSignalWeak()) {
        Disbuff->setTextColor(RED);
        Disbuff->printf(" WEAK");
    } else {
        Disbuff->setTextColor(GREEN);
        Disbuff->printf(" OK");
    }
    
    Disbuff->setTextColor(WHITE);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    
    // Инициализация NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    // Настройка дисплея
    M5.Display.setRotation(3);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    
    // Инициализация BLE
    NimBLEDevice::init("M5BLELocker");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    // Создаем сервер
    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());
    
    // Создаем HID устройство
    hid = new NimBLEHIDDevice(bleServer);
    input = hid->getInputReport(1);
    output = hid->getOutputReport(1);
    
    // Запускаем сервер
    bleServer->start();
    hid->setManufacturer("M5Stack");
    hid->setVendorId(0xe502);
    hid->setProductId(0xa111);
    hid->setVersion(0x0210);
    hid->setHidInfo(0x00, 0x02);
    
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setAppearance(HID_KEYBOARD);
    pAdvertising->addServiceUUID(hid->getHidService()->getUUID());
    pAdvertising->start();
    
    // Инициализация дисплея
    Disbuff = new M5Canvas(&M5.Display);
    Disbuff->createSprite(M5.Display.width(), M5.Display.height());
    Disbuff->fillSprite(BLACK);
    Disbuff->setTextColor(WHITE);
    
    if (serialOutputEnabled) {
        Serial.println("BLE HID устройство готово");
        Serial.println("Ожидание подключения...");
    }
}

void loop() {
    M5.update();
    
    if (connected) {
        // Проверяем необходимость блокировки
        if (shouldLockComputer()) {
            // Отправляем команду блокировки
            uint8_t msg[] = {0x00};  // Команда блокировки
            input->setValue(msg, sizeof(msg));
            input->notify();
            
            currentState = LOCKED;
            if (serialOutputEnabled) {
                Serial.println("Компьютер заблокирован");
            }
        }
        
        // Обновляем дисплей
        Disbuff->fillSprite(BLACK);
        updateDisplay();
        Disbuff->pushSprite(0, 0);
    }
    
    delay(100);  // Небольшая задержка для стабильности
} 