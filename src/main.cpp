#include <M5StickCPlus2.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "esp_task_wdt.h"
#include "esp_gap_ble_api.h"
#include "NimBLEClient.h"
#include <Preferences.h>

// В начало файла после включения библиотек
void lockComputer();
void unlockComputer();
String getPasswordForDevice(const String& deviceAddress);
void savePasswordForDevice(const String& deviceAddress, const String& password);
void setPasswordFromSerial();
void checkSerialCommands();

// Создаем объект для работы с NVS
static Preferences preferences;
static const char* PREF_NAMESPACE = "ble_lock";
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

// Добавим переменную для хранения адреса подключенного устройства
static std::string connectedDeviceAddress = "";

// Константы для определения движения
static const int MOVEMENT_SAMPLES = 10;     // Сколько измерений для определения движения
static const int MOVEMENT_THRESHOLD = 5;    // Порог изменения RSSI для движения (dBm)
static const int MOVEMENT_TIME = 3000;      // Время движения для блокировки (3 секунды)
static const int SCAN_INTERVAL_LOCKED = 3000; // Интервал сканирования в заблокированном режиме

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

// Добавим функцию для обновления экрана
void updateDisplay() {
    Disbuff->fillSprite(BLACK);
    Disbuff->setTextSize(2);
    
    // Статус BLE (уменьшенный)
    Disbuff->setTextColor(connected ? GREEN : RED);
    Disbuff->setCursor(5, 5);
    Disbuff->printf("BLE:%s", connected ? "OK" : "NO");
    
    if (connected) {
        // RSSI информация
        Disbuff->setTextColor(WHITE);
        Disbuff->setCursor(5, 25);
        Disbuff->printf("RS:%d", lastAverageRssi);
        
        Disbuff->setCursor(5, 45);
        Disbuff->printf("AV:%d", getAverageRssi());
        
        // Движение и состояние
        Disbuff->setCursor(5, 65);
        Disbuff->printf("ST:");
        switch (currentState) {
            case NORMAL: 
                Disbuff->setTextColor(GREEN);
                Disbuff->print("NORM"); 
                break;
            case MOVING_AWAY: 
                Disbuff->setTextColor(YELLOW);
                Disbuff->printf("AWAY:%d", 
                    (MOVEMENT_TIME - (millis() - movementStartTime)) / 1000); // Секунды до блокировки
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
        
        // Счетчик движения
        static int lastMovementCount = 0;
        if (currentState == MOVING_AWAY) {
            Disbuff->setCursor(5, 85);
            Disbuff->setTextColor(YELLOW);
            Disbuff->printf("CNT:%d/%d", lastMovementCount, MOVEMENT_SAMPLES);
        }
        
        // Разница RSSI
        Disbuff->setCursor(5, 105);
        Disbuff->setTextColor(WHITE);
        static int lastRssiDiff = 0;
        if (currentState == MOVING_AWAY || currentState == NORMAL) {
            lastRssiDiff = getAverageRssi() - lastAverageRssi;
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
        
        // Индикатор уровня сигнала
        Disbuff->setCursor(5, 125);
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
        
        // Показываем наличие пароля
        Disbuff->setCursor(5, 145);
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
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        // Проверяем адрес устройства
        if (advertisedDevice->getAddress().toString() == connectedDeviceAddress) {
            int rssi = advertisedDevice->getRSSI();
            lastAverageRssi = rssi;
            updateDisplay();
            addRssiValue(rssi);
        }
    }

    void onScanEnd(NimBLEScanResults results) {
        // Можно полностью убрать этот метод, если он не нужен
    }
};

// Затем объявляем глобальные переменные
static NimBLEScan* pScan = nullptr;
static MyScanCallbacks* scanCallbacks = nullptr;

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
    static int samplesBelow = 0;
    static bool wasNear = false;
    static bool lockSent = false;
    
    // Не блокируем, если уже заблокировано
    if (currentState == LOCKED) return false;
    
    // Сначала определяем, находимся ли мы близко к компьютеру
    if (avgRssi > RSSI_NEAR_THRESHOLD) {
        wasNear = true;
        samplesBelow = 0;
        lockSent = false;
        return false;
    }
    
    // Если мы были близко и сигнал стал слабее порога
    if (wasNear && avgRssi < RSSI_LOCK_THRESHOLD) {
        samplesBelow++;
        Serial.printf("Signal below threshold: %d/%d samples\n", 
            samplesBelow, SAMPLES_TO_CONFIRM);
            
        // Если достаточно измерений подтверждают удаление
        if (samplesBelow >= SAMPLES_TO_CONFIRM && !lockSent) {
            Serial.println("\n!!! Lock condition detected !!!");
            Serial.printf("Average RSSI: %d (Threshold: %d)\n", avgRssi, RSSI_LOCK_THRESHOLD);
            return true;
        }
    } else {
        samplesBelow = 0;
    }
    
    return false;
}

// Добавляем константы для хранения паролей
static const char* KEY_PASSWORD_PREFIX = "pwd_";  // Префикс для ключей паролей
static const int MAX_STORED_PASSWORDS = 5;        // Максимум сохраненных паролей

// Функция для получения пароля
String getPasswordForDevice(const String& deviceAddress) {
    return preferences.getString(KEY_PASSWORD, "");  // Убираем отладочный вывод
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
    Serial.println("Enter new password (end with newline):");
    
    String newPassword = "";
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (newPassword.length() > 0) {
                    break;
                }
            } else {
                newPassword += c;
                Serial.print("*");
            }
        }
        delay(10);
    }
    
    if (newPassword.length() > 0) {
        savePasswordForDevice(connectedDeviceAddress.c_str(), newPassword);
        
        // Проверяем что пароль сохранился
        String savedPwd = getPasswordForDevice(connectedDeviceAddress.c_str());
        if (savedPwd == newPassword) {
            Serial.println("\nPassword saved and verified successfully!");
        } else {
            Serial.println("\nError: Password verification failed!");
        }
        
        // Показываем на экране
        Disbuff->fillSprite(BLACK);
        Disbuff->setTextColor(GREEN);
        Disbuff->setCursor(5, 40);
        Disbuff->print("NEW PWD!");
        Disbuff->pushSprite(0, 0);
        delay(1000);
    }
}

// Добавляем обработку команд через Serial
void checkSerialCommands() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command == "setpwd") {
            if (connected) {
                setPasswordFromSerial();
            } else {
                Serial.println("Error: No device connected!");
            }
        } else if (command == "getpwd") {
            if (connected) {
                String pwd = getPasswordForDevice(connectedDeviceAddress.c_str());
                Serial.printf("Current password for %s: %s\n", 
                    connectedDeviceAddress.c_str(),
                    pwd.length() > 0 ? pwd.c_str() : "not set");
            }
        } else if (command == "help") {
            Serial.println("\nAvailable commands:");
            Serial.println("setpwd - Set password for current device");
            Serial.println("getpwd - Show current password");
            Serial.println("help   - Show this help");
        }
    }
}

// Добавим константу для минимального интервала между попытками разблокировки
static const unsigned long UNLOCK_ATTEMPT_INTERVAL = 5000;  // 5 секунд
static unsigned long lastUnlockAttempt = 0;

// Функция для сохранения пароля
void savePasswordForDevice(const String& deviceAddress, const String& password) {
    Serial.printf("Saving password '%s'\n", password.c_str());
    
    // Сохраняем текущее состояние блокировки
    bool wasLocked = preferences.getBool(KEY_IS_LOCKED, false);
    String lastAddr = preferences.getString(KEY_LAST_ADDR, "");
    
    // Сохраняем новый пароль
    preferences.putString(KEY_PASSWORD, password);  // Используем тот же KEY_PASSWORD
    
    // Восстанавливаем состояние блокировки
    if (wasLocked) {
        preferences.putBool(KEY_IS_LOCKED, true);
        preferences.putString(KEY_LAST_ADDR, lastAddr);
    }
    
    // Проверяем сохранение пароля
    String saved = preferences.getString(KEY_PASSWORD, "");  // И здесь тоже
    Serial.printf("Immediately after save, read password: '%s'\n", saved.c_str());
    
    if (saved == password) {
        Serial.println("Password saved successfully!");
    } else {
        Serial.println("Error saving password!");
    }
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
    Disbuff->setTextSize(2);
    
    Serial.println("=== Initial HID Setup ===");
    
    // Инициализация BLE
    Serial.println("1. Initializing BLE...");
    NimBLEDevice::init("M5 BLE KB");
    
    // Настраиваем BLE для лучшей производительности
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(185);  // Оптимальный MTU
    
    // Настраиваем сканер согласно документации
    pScan = NimBLEDevice::getScan();
    if (scanCallbacks != nullptr) {
        delete scanCallbacks;
    }
    scanCallbacks = new MyScanCallbacks();
    pScan->setScanCallbacks(scanCallbacks, true);
    pScan->setActiveScan(true);
    pScan->setInterval(40);    // 25ms
    pScan->setWindow(20);      // 12.5ms
    pScan->clearResults();                    // Очищаем предыдущие результаты
    pScan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);  // Отключаем фильтр
    pScan->setDuplicateFilter(false);        // Разрешаем дубликаты
    
    Serial.printf("BLE initialized, MTU: %d\n", NimBLEDevice::getMTU());
    
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
    
    // Ждем инициализации колбэков
    delay(100);
    
    // Проверяем состояние
    Serial.printf("Server ready: %s\n", bleServer->getConnectedCount() == 0 ? "Yes" : "No");
    
    // Только потом создаем HID
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
    
    // Инициализируем NVS
    preferences.begin(PREF_NAMESPACE, false);
    
    // Проверяем предыдущее состояние
    bool wasLocked = preferences.getBool(KEY_IS_LOCKED, false);
    String lastAddress = preferences.getString(KEY_LAST_ADDR, "");
    
    // Явно устанавливаем начальное состояние как NORMAL
    currentState = NORMAL;  
    
    // Только если было заблокировано и есть адрес - восстанавливаем состояние
    if (wasLocked && !lastAddress.isEmpty()) {
        currentState = LOCKED;
        connectedDeviceAddress = lastAddress.c_str();
        Serial.println("\n=== Previous state was LOCKED ===");
        Serial.printf("Last connected device: %s\n", lastAddress.c_str());
    } else {
        // Сбрасываем флаг блокировки если что-то не так
        preferences.putBool(KEY_IS_LOCKED, false);
    }
    
    // Начинаем в режиме HID
    scanMode = false;
}

void loop() {
    static unsigned long lastUpdate = 0;
    static unsigned long lastCheck = 0;
    static unsigned long lastRssiCheck = 0;
    static bool lastRealState = false;
    static unsigned long lastDebugCheck = 0;
    
    M5.update();
    
    // Оставляем только важные сообщения
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
    
    // Обновляем экран каждые 100мс
    if (millis() - lastUpdate >= 100) {
        lastUpdate = millis();
        updateDisplay();
    }
    
    // Убираем обновление экрана из режима сканирования
    if (scanMode) {
        static unsigned long lastResultsCheck = 0;
        if (millis() - lastResultsCheck >= 200) {
            lastResultsCheck = millis();
            
            // Сначала останавливаем
            pScan->stop();
            delay(10);  // Даем время на остановку
            
            // Получаем результаты
            NimBLEScanResults results = NimBLEDevice::getScan()->getResults();
            if (results.getCount() > 0) {
                const NimBLEAdvertisedDevice* device = results.getDevice(0);
                if (device && device->getAddress().toString() == connectedDeviceAddress) {
                    lastAverageRssi = device->getRSSI();
                    
                    // Добавляем в массив для усреднения
                    addRssiValue(lastAverageRssi);
                    
                    // Вычисляем и выводим среднее
                    int avgRssi = getAverageRssi();
                    
                    updateDisplay();
                }
            }
            
            // Очищаем и перезапускаем
            pScan->clearResults();
            pScan->start(0, false);
        }
    }
    
    // Обработка кнопки A для теста
    if (M5.BtnA.wasPressed() && M5.BtnB.wasPressed()) {  // Нажаты обе кнопки
        if (connected) {
            setPasswordFromSerial();  // Входим в режим настройки пароля
        }
    }
    else if (M5.BtnA.wasPressed()) {
        if (currentState == LOCKED) {
            Serial.println("Manual unlock requested");
            unlockComputer();
        }
    }
    else if (M5.BtnB.wasPressed()) {
        if (connected && currentState == NORMAL) {
            Serial.println("Manual lock requested");
            lockComputer();
            currentState = LOCKED;
        }
    }
    
    // Проверяем статус подключения
    if (bleServer && bleServer->getConnectedCount() > 0) {
        if (!connection_info.connected) {
            // Получаем информацию о подключении
            ble_gap_conn_desc desc;
            if (ble_gap_conn_find(0, &desc) == 0) {
                connection_info.connected = true;
                connection_info.conn_handle = desc.conn_handle;
                connection_info.address = NimBLEAddress(desc.peer_ota_addr).toString();
                
                connectedDeviceAddress = connection_info.address;
            }
        }
    } else {
        connection_info.connected = false;
        connection_info.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        connection_info.address = "";
    }
    
    // Добавляем отладочную информацию каждые 5 секунд
    if (millis() - lastDebugCheck >= 5000) {
        lastDebugCheck = millis();
        
        Serial.printf("\n=== Status Update ===\n");
        Serial.printf("Connected: %d\n", connected);
        Serial.printf("Current RSSI: %d\n", lastAverageRssi);
        Serial.printf("State: %s\n", 
            currentState == NORMAL ? "NORMAL" :
            currentState == MOVING_AWAY ? "MOVING_AWAY" :
            currentState == LOCKED ? "LOCKED" :
            currentState == APPROACHING ? "APPROACHING" : "UNKNOWN");
        Serial.printf("Password: %s\n", 
            getPasswordForDevice(connectedDeviceAddress.c_str()).length() > 0 ? "SET" : "NOT SET");
        Serial.println("===================\n");
    }
    
    // Проверяем статус сканирования каждые 5 секунд
    static unsigned long lastScanCheck = 0;
    if (scanMode && millis() - lastScanCheck >= 5000) {
        lastScanCheck = millis();
        
        NimBLEScan* pScan = NimBLEDevice::getScan();
        if (!pScan->isScanning()) {
            Serial.println("Scan stopped, restarting...");
            pScan->setActiveScan(true);
            pScan->setInterval(0x50);
            pScan->setWindow(0x30);
            
            if(pScan->start(0)) {
                Serial.println("Scan restarted successfully");
            } else {
                Serial.println("Failed to restart scan!");
            }
        }
        Serial.println("========================\n");
    }
    
    // В loop() добавляем обработку состояний
    if (scanMode) {
        static unsigned long lastResultsCheck = 0;
        if (millis() - lastResultsCheck >= 200) {
            lastResultsCheck = millis();
            
            int avgRssi = getAverageRssi();
            
            // Отображаем статус на экране
            Disbuff->setCursor(5, 85);
            if (avgRssi > RSSI_NEAR_THRESHOLD) {
                Disbuff->setTextColor(GREEN);
                Disbuff->print("NEAR PC");
            } else if (avgRssi > RSSI_LOCK_THRESHOLD) {
                Disbuff->setTextColor(YELLOW);
                Disbuff->print("WARNING");
            } else {
                Disbuff->setTextColor(RED);
                Disbuff->print("FAR");
            }
            
            // Проверяем необходимость блокировки
            if (shouldLockComputer(avgRssi)) {
                Serial.printf("\n!!! DISTANCE THRESHOLD REACHED !!!\n");
                Serial.printf("Average RSSI: %d (Threshold: %d)\n", 
                    avgRssi, RSSI_LOCK_THRESHOLD);
                Serial.println("Sending lock command...");
                
                lockComputer();
                currentState = LOCKED;
            }
        }
    }
    
    // Проверяем состояние блокировки
    if (currentState == LOCKED) {
        if (getAverageRssi() > RSSI_NEAR_THRESHOLD) {
            // Проверяем, прошло ли достаточно времени с последней попытки
            if (millis() - lastUnlockAttempt >= UNLOCK_ATTEMPT_INTERVAL) {
                lastUnlockAttempt = millis();
                
                // Проверяем наличие пароля перед попыткой разблокировки
                if (getPasswordForDevice(connectedDeviceAddress.c_str()).length() > 0) {
                    Serial.println("Device is near, attempting to unlock...");
                    unlockComputer();
                } else {
                    Serial.println("Cannot unlock: no password stored. Use 'setpwd' command to set password.");
                    // Показываем сообщение на экране
                    Disbuff->fillSprite(BLACK);
                    Disbuff->setTextColor(RED);
                    Disbuff->setCursor(5, 40);
                    Disbuff->print("NO PWD!");
                    Disbuff->pushSprite(0, 0);
                    delay(1000);
                }
            }
        }
    }
    
    // Проверяем команды Serial
    checkSerialCommands();
    
    delay(1);
}

void lockComputer() {
    // Сохраняем текущую мощность
    int8_t currentPower = NimBLEDevice::getPower();
    
    // Устанавливаем максимальную мощность
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
    
    // Возвращаем исходную мощность
    delay(100);
    NimBLEDevice::setPower((esp_power_level_t)currentPower);
    
    if (success) {
        // Сохраняем состояние блокировки и адрес устройства
        preferences.putBool(KEY_IS_LOCKED, true);
        preferences.putString(KEY_LAST_ADDR, connectedDeviceAddress.c_str());
        Serial.println("Lock state saved to NVS");
    }
    
    if (!success) {
        Serial.println("Failed to send lock command!");
    }
}

// Добавляем функцию разблокировки
void unlockComputer() {
    String password = getPasswordForDevice(connectedDeviceAddress.c_str());
    if (password.length() == 0) {
        Serial.println("No password stored for this device!");
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
            preferences.putBool(KEY_IS_LOCKED, false);
            preferences.remove(KEY_LAST_ADDR);
            currentState = NORMAL;
            
            Serial.println("Computer unlocked successfully!");
            break;
        }
        delay(100);
    }
    
    // Возвращаем исходную мощность
    NimBLEDevice::setPower((esp_power_level_t)currentPower);
} 