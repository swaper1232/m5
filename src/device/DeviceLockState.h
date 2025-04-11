#pragma once

#include <Arduino.h>

class DeviceLockState {
public:
    static DeviceLockState& getInstance() {
        static DeviceLockState instance;
        return instance;
    }

    bool getLockState(const String& deviceAddress);
    bool getLockState();
    void setLockState(const String& deviceAddress, bool locked);
    void setLockState(bool locked);

private:
    DeviceLockState() {} // Приватный конструктор для синглтона
    bool currentLockState = false;
    String currentDevice = "";
}; 