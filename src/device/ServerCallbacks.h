#pragma once

#include <NimBLEServer.h>

class ServerCallbacks : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
    void onMtuChanged(uint16_t MTU, NimBLEConnInfo& connInfo) override;
}; 