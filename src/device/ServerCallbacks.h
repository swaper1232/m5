#pragma once

#include <NimBLEServer.h>

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override;
    void onDisconnect(NimBLEServer* pServer) override;
    void onRssiUpdate(NimBLEServer* pServer, int rssi) override;
}; 