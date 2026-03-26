#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ImprovWiFiLibrary.h>

// Runs the Improv Wi-Fi Serial wizard.
// Blocks until credentials are received and WiFi connects, then saves
// wifi.json and reboots. Never returns on success.
inline void runImprovSetup(const char* firmwareVersion)
{
    Serial.println("[Improv] No WiFi configured — entering Improv Wi-Fi setup");

    ImprovWiFi improv(&Serial);

    improv.setDeviceInfo(
        ImprovWiFiChipFamily::ESP32_C3,
        "ESP32-C3 Semaphore",
        firmwareVersion,
        "Semaphore"
    );

    improv.setCustomConnectWiFi([](String ssid, String password) -> bool {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(100);
        }
        return WiFi.status() == WL_CONNECTED;
    });

    improv.onImprovConnected([](const char*) {
        // Save credentials to wifi.json
        JsonDocument doc;
        doc["ssid"]     = WiFi.SSID();
        doc["password"] = WiFi.psk();
        File f = LittleFS.open("/wifi.json", "w");
        if (f) { serializeJson(doc, f); f.close(); }
        Serial.printf("[Improv] Saved WiFi: %s — rebooting\n", WiFi.SSID().c_str());
        delay(500);
        ESP.restart();
    });

    // Set redirect URL dynamically once connected
    // (improv library sends it when WiFi connects)
    while (true) {
        improv.handleSerial();
        // Update redirect URL as soon as we have an IP
        if (WiFi.status() == WL_CONNECTED) {
            String url = "http://" + WiFi.localIP().toString() + "/";
            improv.setDeviceInfo(
                ImprovWiFiChipFamily::ESP32_C3,
                "ESP32-C3 Semaphore",
                firmwareVersion,
                "Semaphore",
                url.c_str()
            );
        }
        delay(1);
    }
}
