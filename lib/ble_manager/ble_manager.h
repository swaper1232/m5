#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <NimBLEDevice.h>

// Класс для колбэков при сканировании BLE
class MyScanCallbacks : public NimBLEScanCallbacks {
public:
    MyScanCallbacks();
    virtual ~MyScanCallbacks();
    virtual void onResult(NimBLEAdvertisedDevice* advertisedDevice) override;
};

// Класс для колбэков BLE сервера
class ServerCallbacks : public NimBLEServerCallbacks {
public:
    ServerCallbacks();
    virtual ~ServerCallbacks();

    virtual void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override;
    virtual void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override;
    virtual uint32_t onPassKeyRequest() override;
    virtual void onAuthenticationComplete(uint16_t conn_handle) override;
    virtual bool onConfirmPIN(uint32_t pin) override;
};

#endif // BLE_MANAGER_H 