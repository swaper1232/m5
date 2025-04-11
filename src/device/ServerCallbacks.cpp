#include "ServerCallbacks.h"
#include "../rssi/RSSIManager.h"

extern bool connected;
extern std::string connectedDeviceAddress;

void ServerCallbacks::onConnect(NimBLEServer* pServer) {
    connected = true;
    connectedDeviceAddress = pServer->getPeerAddress().toString();
}

void ServerCallbacks::onDisconnect(NimBLEServer* pServer) {
    connected = false;
    connectedDeviceAddress = "";
}

void ServerCallbacks::onRssiUpdate(NimBLEServer* pServer, int rssi) {
    RSSIManager::getInstance().addMeasurement(rssi);
} 