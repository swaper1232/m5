#include "ServerCallbacks.h"
#include "../rssi/RSSIManager.h"

extern bool connected;
extern std::string connectedDeviceAddress;

void ServerCallbacks::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    connected = true;
    connectedDeviceAddress = connInfo.getAddress().toString();
}

void ServerCallbacks::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    connected = false;
    connectedDeviceAddress = "";
}

void ServerCallbacks::onMtuChanged(uint16_t MTU, NimBLEConnInfo& connInfo) {
    // Ничего не делаем, но метод должен быть реализован
}

void ServerCallbacks::onRssiUpdate(NimBLEServer* pServer, int rssi) {
    RSSIManager::getInstance().addMeasurement(rssi);
} 