#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "esp_task_wdt.h"
#include "esp_gap_ble_api.h"
#include "NimBLEClient.h"
#include <M5Unified.h>
#include <nvs_flash.h>
#include <nvs.h>

// Включаем модульные компоненты
#include "../src/DebugUtils.h"
#include "../src/StorageManager.h"
#include "../src/DeviceManager.h"
#include "../src/RSSIHandler.h"
#include "../src/LockStateManager.h"

// Глобальные объекты
StorageManager storageManager;
DeviceManager deviceManager(storageManager);
RSSIHandler rssiHandler;
LockStateManager lockStateManager(storageManager, rssiHandler, deviceManager);

// Глобальные переменные для BLE
NimBLEServer* bleServer = nullptr;
NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* input = nullptr;
NimBLECharacteristic* output = nullptr;
static NimBLEScan* pScan = nullptr;

// Флаги управления
bool serialOutputEnabled = true;
bool scanMode = false;

// Данные подключения
struct {
    bool connected = false;
    uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
    std::string address = "";
} connection_info;

// Отображение
M5Canvas* Disbuff = nullptr;
unsigned long screenOffTime = 0;
bool temporaryScreenActive = false;

// Код дескриптора отчёта HID для клавиатуры
static const uint8_t hidReportDescriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0              // End Collection
};

// Константы для клавиш клавиатуры
#define KEY_ENTER     0x28
#define KEY_ESCAPE    0x29
#define KEY_BACKSPACE 0x2A
#define KEY_TAB       0x2B
#define KEY_CAPSLOCK  0x39
#define KEY_A         0x04
#define KEY_B         0x05
#define KEY_Z         0x1D

// Будущие прототипы функций
void updateDisplay();
void echoSerialInput();
void sendKey(char key);
void typePassword(const String& password);
void lockComputer();
void unlockComputer();

// Класс для колбэков сервера BLE
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        connection_info.connected = true;
        connection_info.conn_handle = desc->conn_handle;
        connection_info.address = std::string(NimBLEAddress(desc->peer_ota_addr).toString());
        
        if (serialOutputEnabled) {
            Serial.print("Client connected: ");
            Serial.println(connection_info.address.c_str());
        }
        
        // Сохраняем адрес устройства и устанавливаем его как текущее
        String deviceAddress = String(connection_info.address.c_str());
        deviceManager.setCurrentDevice(deviceAddress);
        
        updateDisplay();
    }
    
    void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        connection_info.connected = false;
        connection_info.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        
        if (serialOutputEnabled) {
            Serial.println("Client disconnected");
        }
        
        pServer->startAdvertising();
        updateDisplay();
    }
    
    void onRssiUpdate(NimBLEServer* pServer, int rssi) {
        rssiHandler.addMeasurement(rssi);
        Serial.printf("RSSI Update: %d dBm\n", rssi);
    }
};

// Функции обработки ввода
void echoSerialInput() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command.length() > 0) {
            if (serialOutputEnabled) {
                Serial.printf("Command: '%s'\n", command.c_str());
            }
            
            // Команды можно будет добавить здесь
            if (command == "lock") {
                lockStateManager.lockComputer();
            }
            else if (command == "unlock") {
                lockStateManager.unlockComputer();
            }
            else if (command == "debug on") {
                serialOutputEnabled = true;
                Serial.println("Debug output enabled");
            }
            else if (command == "debug off") {
                Serial.println("Debug output disabled");
                serialOutputEnabled = false;
            }
            else {
                if (serialOutputEnabled) {
                    Serial.println("Unknown command");
                }
            }
        }
    }
}

// Функции для имитации клавиатуры
void sendKey(char key) {
    uint8_t msg[] = {0, 0, KEY_A, 0, 0, 0, 0, 0};  // Клавиша A как пример
    
    if (key >= 'a' && key <= 'z') {
        msg[2] = KEY_A + (key - 'a');  // Для строчных букв
    }
    else if (key >= 'A' && key <= 'Z') {
        msg[0] = 0x02;  // Shift
        msg[2] = KEY_A + (key - 'A');  // Для заглавных букв
    }
    else if (key == '\n') {
        msg[2] = KEY_ENTER;  // Enter
    }
    
    input->setValue(msg, sizeof(msg));
    input->notify();
    
    // Пустое сообщение для освобождения клавиш
    uint8_t release[] = {0, 0, 0, 0, 0, 0, 0, 0};
    input->setValue(release, sizeof(release));
    input->notify();
    
    delay(10);  // Небольшая задержка
}

void typePassword(const String& password) {
    for (int i = 0; i < password.length(); i++) {
        sendKey(password[i]);
    }
    
    // Enter в конце ввода пароля
    sendKey('\n');
}

// Функции блокировки и разблокировки
void lockComputer() {
    lockStateManager.lockComputer();
}

void unlockComputer() {
    lockStateManager.unlockComputer();
}

// Обновление дисплея
void updateDisplay() {
    if (!Disbuff) return;
    
    Disbuff->fillScreen(BLACK);
    
    if (connection_info.connected) {
        // Заголовок (5px от верха)
        Disbuff->setTextSize(2);
        Disbuff->setTextColor(GREEN);
        Disbuff->setCursor(5, 5);
        Disbuff->print("CONNECTED");
        
        // RSSI (20px)
        Disbuff->setTextColor(WHITE);
        Disbuff->setCursor(5, 20);
        Disbuff->printf("RS:%d", rssiHandler.getAverageRssi());
        
        // Состояние (35px)
        DeviceState state = lockStateManager.getCurrentState();
        Disbuff->setCursor(5, 35);
        switch (state) {
            case NORMAL:
                Disbuff->setTextColor(GREEN);
                Disbuff->print("NORMAL");
                break;
            case MOVING_AWAY:
                Disbuff->setTextColor(YELLOW);
                Disbuff->print("MOVING");
                break;
            case LOCKED:
                Disbuff->setTextColor(RED);
                Disbuff->print("LOCKED");
                break;
            case APPROACHING:
                Disbuff->setTextColor(CYAN);
                Disbuff->print("APPROACH");
                break;
        }
    } else {
        Disbuff->setTextSize(2);
        Disbuff->setTextColor(RED);
        Disbuff->setCursor(5, 5);
        Disbuff->print("DISCONNECTED");
        
        if (scanMode) {
            Disbuff->setTextColor(YELLOW);
            Disbuff->setCursor(5, 25);
            Disbuff->print("SCANNING...");
        }
    }
    
    // Нижняя строка (70px) - подсказки по кнопкам
    Disbuff->setTextSize(1);
    Disbuff->setTextColor(WHITE);
    Disbuff->setCursor(5, 70);
    Disbuff->print("A:Lock B:Unlock");
    
    // Выводим на экран
    Disbuff->pushSprite(0, 0);
}

// Инициализация устройства
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
    
    // Инициализация модулей
    if (!storageManager.initialize("m5kb_v1")) {
        Serial.println("Failed to initialize storage");
    }
    
    // Инициализация BLE
    NimBLEDevice::init("M5 BLE HID");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    
    // Создаем сервер
    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());
    
    // Создаем HID устройство
    hid = new NimBLEHIDDevice(bleServer);
    hid->setManufacturer("M5Stack");
    hid->setHidInfo(0x00, 0x01);
    
    input = hid->getInputReport(1);
    output = hid->getOutputReport(1);
    
    // Устанавливаем дескриптор отчета
    hid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
    hid->startServices();
    
    // Настройка рекламы
    NimBLEAdvertising* pAdvertising = bleServer->getAdvertising();
    pAdvertising->setAppearance(HID_KEYBOARD);
    pAdvertising->addServiceUUID(hid->hidService()->getUUID());
    pAdvertising->start();
    
    // Настройка сканирования
    pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    
    // Инициализация менеджера состояния блокировки
    lockStateManager.initialize();
    
    // Обновляем дисплей
    updateDisplay();
    
    Serial.println("BLE Keyboard setup complete");
}

// Основной цикл
void loop() {
    // Обновление кнопок
    M5.update();
    
    // Обработка кнопок
    if (M5.BtnA.wasPressed()) {
        lockComputer();
        updateDisplay();
    }
    
    if (M5.BtnB.wasPressed()) {
        unlockComputer();
        updateDisplay();
    }
    
    // Обновление состояния
    lockStateManager.updateState();
    
    // Обработка Serial
    echoSerialInput();
    
    // Обновление дисплея каждую секунду
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate >= 1000) {
        lastDisplayUpdate = millis();
        updateDisplay();
    }
    
    delay(10);
}
