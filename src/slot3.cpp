/*
 * Slot 3 - Рабочая версия с корректным освобождением ресурсов
 * 
 * Особенности:
 * - Правильная последовательность остановки сервисов
 * - Корректное освобождение памяти
 * - Стабильное переключение режимов
 * 
 * Дата: 2024-01-17
 */

#include <M5StickCPlus2.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "esp_task_wdt.h"
#include "esp_gap_ble_api.h"
#include "NimBLEClient.h"

static NimBLEAdvertisementData advData;
static NimBLEServer* bleServer;
static NimBLEHIDDevice* hid;
static NimBLECharacteristic* input;
static NimBLECharacteristic* output;
static bool connected = false;
static M5Canvas* Disbuff = nullptr;  // Буфер для отрисовки
static int16_t lastReceivedRssi = 0;
static String lastMovement = "==";
static int lastAverageRssi = 0;

// Добавим переменную для режима работы
static bool scanMode = false;

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
static const int RSSI_SAMPLES = 5;        // Количество измерений для усреднения
static const int RSSI_INTERVAL = 1000;    // Интервал между измерениями (мс)
static const int RSSI_THRESHOLD = 5;      // Порог изменения для определения движения

// Переменные для хранения измерений RSSI
static int rssiValues[RSSI_SAMPLES];
static int rssiIndex = 0;
static unsigned long lastRssiCheck = 0;

// Функция для получения среднего RSSI
int getAverageRssi() {
    int sum = 0;
    for (int i = 0; i < RSSI_SAMPLES; i++) {
        sum += rssiValues[i];
    }
    return sum / RSSI_SAMPLES;
}

// Добавим функцию для обновления экрана
void updateDisplay() {
    Disbuff->fillSprite(BLACK);  // Очищаем экран
    
    // Статус BLE
    Disbuff->setTextSize(2);
    Disbuff->setTextColor(connected ? GREEN : RED);
    Disbuff->setCursor(10, 10);
    Disbuff->printf("BLE:");
    Disbuff->setCursor(10, 35);
    Disbuff->printf("%s", connected ? "Connected" : "Waiting");
    
    // RSSI и движение (только если подключены)
    if (connected) {
        Disbuff->setTextColor(WHITE);
        Disbuff->setCursor(10, 70);
        Disbuff->printf("RSSI: %d", lastAverageRssi);
        
        Disbuff->setCursor(10, 95);
        Disbuff->printf("Move: %s", lastMovement.c_str());
    }
    
    Disbuff->pushSprite(0, 0);  // Выводим на экран
}

// Изменим колбэки
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        Serial.println("=== onConnect called ===");
        Serial.printf("Connected count: %d\n", pServer->getConnectedCount());
        Serial.printf("MTU Size: %d\n", pServer->getPeerMTU(0));
        Serial.printf("Peer Address: %s\n", pServer->getPeerInfo(0).getAddress().toString().c_str());
        
        connected = true;
        updateDisplay();
    }

    void onDisconnect(NimBLEServer* pServer, int reason) {
        Serial.println("=== onDisconnect called ===");
        Serial.printf("Connected count: %d\n", pServer->getConnectedCount());
        Serial.printf("Connected flag: %d\n", connected);
        Serial.printf("Disconnect reason: 0x%02x\n", reason);
        
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
    
    // Инициализация BLE
    Serial.println("1. Initializing BLE...");
    NimBLEDevice::init("M5 BLE KB");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    // Настройки безопасности
    Serial.println("2. Setting up security...");
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | 
                                 BLE_SM_PAIR_AUTHREQ_MITM | 
                                 BLE_SM_PAIR_AUTHREQ_SC);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    
    // Создаем HID сервер
    Serial.println("3. Creating server...");
    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());
    Serial.printf("Server address: %s\n", NimBLEDevice::getAddress().toString().c_str());
    
    // HID сервис
    Serial.println("4. Setting up HID service...");
    hid = new NimBLEHIDDevice(bleServer);
    hid->setManufacturer("M5Stack");
    hid->setPnp(0x02, 0x05AC, 0x820A, 0x0001);
    hid->setHidInfo(0x00, 0x01);
    
    // Получаем характеристики до запуска сервисов
    Serial.println("Getting characteristics...");
    input = hid->getInputReport(1);
    output = hid->getOutputReport(1);
    
    hid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
    hid->startServices();
    
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
}

void loop() {
    static unsigned long lastUpdate = 0;
    static unsigned long lastCheck = 0;
    static unsigned long lastRssiCheck = 0;
    static bool lastRealState = false;
    
    M5.update();
    
    // Оставляем существующую проверку статуса
    if (millis() - lastCheck >= 500) {
        lastCheck = millis();
        bool realConnected = bleServer->getConnectedCount() > 0;
        
        if (realConnected != lastRealState) {
            lastRealState = realConnected;
            connected = realConnected;
            Serial.printf("Connection state changed and synced: %d\n", connected);
            updateDisplay();
        }
        
        Serial.printf("Status Check - Connected flag: %d, Real count: %d\n", 
                     connected, bleServer->getConnectedCount());
    }
    
    // Обновляем экран как обычно
    if (millis() - lastUpdate >= 100) {
        lastUpdate = millis();
        
        Disbuff->fillSprite(BLACK);
        Disbuff->setTextColor(connected ? GREEN : RED);
        Disbuff->setCursor(10, 10);
        Disbuff->printf("BLE:");
        Disbuff->setCursor(10, 35);
        Disbuff->printf("%s", connected ? "Connected" : "Waiting");
        
        if (connected) {
            Disbuff->setTextColor(WHITE);
            Disbuff->setCursor(10, 60);
            Disbuff->printf("Count: %d", bleServer->getConnectedCount());
            Disbuff->setCursor(10, 85);
            Disbuff->printf("RSSI: %d", lastAverageRssi);
        }
        
        Disbuff->pushSprite(0, 0);
    }
    
    // Обработка кнопки A для теста
    if (M5.BtnA.wasPressed()) {
        Serial.println("Button A pressed");
        Serial.printf("Connected state: %d\n", connected);
        
        if (connected) {
            Serial.println("Sending key 'a'");
            uint8_t msg[] = {0, 0, 4, 0, 0, 0, 0, 0}; // Код клавиши 'a'
            hid->getInputReport(1)->setValue(msg, sizeof(msg));
            hid->getInputReport(1)->notify();
            
            delay(50);  // Увеличиваем задержку между нажатием и отпусканием
            
            Serial.println("Releasing key");
            uint8_t msg2[] = {0, 0, 0, 0, 0, 0, 0, 0};
            hid->getInputReport(1)->setValue(msg2, sizeof(msg2));
            hid->getInputReport(1)->notify();
            
            delay(50);  // Добавляем задержку после отпускания
        }
    }
    
    // В обработчике кнопки B будем переключать режимы
    if (M5.BtnB.wasPressed()) {
        if (!scanMode) {
            // Переключаемся в режим сканирования
            Serial.println("Switching to scan mode...");
            if (connected) {
                bleServer->disconnect(0);  // Отключаем все соединения
            }
            bleServer->stopAdvertising();  // Останавливаем рекламу
            scanMode = true;
            
            // Запускаем сканирование
            NimBLEScan* pScan = NimBLEDevice::getScan();
            pScan->setActiveScan(true);
            pScan->start(0);  // Бесконечное сканирование
        } else {
            Serial.println("\n=== Restoring HID Mode ===");
            
            // 1. Останавливаем сканирование
            Serial.println("Stopping scan...");
            NimBLEDevice::getScan()->stop();
            delay(1000);  // Важно! Ждем полной остановки сканирования
            
            // 2. Останавливаем остальные операции
            Serial.println("Stopping other operations...");
            if (bleServer) {
                bleServer->getAdvertising()->stop();
                if (connected) {
                    bleServer->disconnect(0);
                }
            }
            delay(100);
            
            // 3. Освобождаем память
            Serial.println("Cleaning up...");
            if (hid) {
                delete hid;
                hid = nullptr;
            }
            bleServer = nullptr;
            input = nullptr;
            output = nullptr;
            delay(100);
            
            // 4. Сбрасываем стек
            Serial.println("Reinitializing BLE stack...");
            NimBLEDevice::deinit();
            delay(500);
            
            // 5. Инициализируем заново
            NimBLEDevice::init("M5 BLE KB");
            NimBLEDevice::setPower(ESP_PWR_LVL_P9);
            
            // 6. Пересоздаем сервер и HID
            Serial.println("Recreating server and HID...");
            bleServer = NimBLEDevice::createServer();
            bleServer->setCallbacks(new ServerCallbacks());
            
            hid = new NimBLEHIDDevice(bleServer);
            hid->setManufacturer("M5Stack");
            hid->setPnp(0x02, 0x05AC, 0x820A, 0x0001);
            hid->setHidInfo(0x00, 0x01);
            
            input = hid->getInputReport(1);
            output = hid->getOutputReport(1);
            
            hid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
            hid->startServices();
            
            // 7. Запуск рекламы
            Serial.println("Starting advertising...");
            bleServer->getAdvertising()->start();
            
            scanMode = false;
            Serial.println("=== Restore complete ===\n");
        }
    }
    
    // В loop() добавим обработку режима сканирования
    if (scanMode) {
        // Показываем результаты сканирования
        NimBLEScanResults results = NimBLEDevice::getScan()->getResults();
        for(int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice* device = results.getDevice(i);
            Serial.printf("Device: %s, RSSI: %d\n", 
                device->getAddress().toString().c_str(),
                device->getRSSI()
            );
        }
        NimBLEDevice::getScan()->clearResults();
    }
    
    delay(1);
} 