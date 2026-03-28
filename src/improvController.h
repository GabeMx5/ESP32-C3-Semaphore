#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ImprovWiFiLibrary.h>

static bool   s_improvProvisioned = false;
static String s_improvSsid;
static String s_improvPwd;

static bool improvConnectWifi(const char* ssid, const char* pwd) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pwd);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
        delay(100);
    if (WiFi.status() == WL_CONNECTED) {
        s_improvSsid        = ssid;
        s_improvPwd         = pwd;
        s_improvProvisioned = true;
        return true;
    }
    return false;
}

// Blocks until WiFi credentials are received, device connects,
// saves wifi.json and reboots. Never returns on success.
inline void runImprovSetup(const char* firmwareVersion)
{
    Serial.println("[Improv] No WiFi config — entering Improv setup");

    ImprovWiFi improv(&Serial);
    improv.setDeviceInfo(
        ImprovTypes::ChipFamily::CF_ESP32_C3,
        "ESP32-C3-Semaphore",
        firmwareVersion ? firmwareVersion : "unknown",
        "ESP32-C3 Semaphore"
    );
    improv.setCustomConnectWiFi(improvConnectWifi);

    while (!s_improvProvisioned) {
        improv.handleSerial();
        delay(1);
    }

    JsonDocument doc;
    doc["ssid"]     = s_improvSsid;
    doc["password"] = s_improvPwd;
    File f = LittleFS.open("/wifi.json", "w");
    if (f) { serializeJson(doc, f); f.close(); }
    Serial.printf("[Improv] Saved — rebooting\n");
    delay(1000);
    ESP.restart();
}
