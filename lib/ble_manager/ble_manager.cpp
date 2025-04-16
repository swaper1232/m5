#include "ble_manager.h"
#include <Arduino.h>

MyScanCallbacks::MyScanCallbacks() {
    // Инициализация, если требуется
}

MyScanCallbacks::~MyScanCallbacks() {
    // Освобождение ресурсов, если необходимо
}

void MyScanCallbacks::onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    Serial.printf("Found device: %s, RSSI: %d\n", advertisedDevice->getAddress().toString().c_str(), advertisedDevice->getRSSI());
}

ServerCallbacks::ServerCallbacks() {
    // Инициализация для серверных колбэков, если требуется
}

ServerCallbacks::~ServerCallbacks() {
    // Освобождение ресурсов
}

void ServerCallbacks::onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
    Serial.println("Client connected");
}

void ServerCallbacks::onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
    Serial.println("Client disconnected");
}

uint32_t ServerCallbacks::onPassKeyRequest() {
    uint32_t passkey = 123456;  // Пример пасскея
    Serial.printf("Passkey requested, returning %d\n", passkey);
    return passkey;
}

void ServerCallbacks::onAuthenticationComplete(uint16_t conn_handle) {
    Serial.printf("Authentication complete for conn_handle: %d\n", conn_handle);
}

bool ServerCallbacks::onConfirmPIN(uint32_t pin) {
    Serial.printf("Confirm PIN: %d\n", pin);
    return true;
} 