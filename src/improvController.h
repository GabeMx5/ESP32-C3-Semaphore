#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ImprovWiFiLibrary.h>

// ─── Improv callbacks (static, required by library) ──────────────────────────

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

// ─── Non-blocking ImprovController ───────────────────────────────────────────
//
// Integrates into the main loop() alongside SerialConsole.
// Serial byte routing:
//   first byte == 0x49 ('I', start of "IMPROV" magic) → Improv handler
//   anything else                                       → SerialConsole
//
class ImprovController {
public:
    // Call once in setup(), before serialConsole.begin().
    // Returns true if Improv mode is needed (no wifi.json found).
    bool begin(const char* firmwareVersion) {
        if (LittleFS.exists("/wifi.json")) return false;

        _active  = true;
        _version = firmwareVersion;
        _improv  = new ImprovWiFi(&Serial);
        _improv->setDeviceInfo(
            ImprovTypes::ChipFamily::CF_ESP32_C3,
            "ESP32-C3-Semaphore",
            firmwareVersion ? firmwareVersion : "unknown",
            "ESP32-C3 Semaphore"
        );
        _improv->setCustomConnectWiFi(improvConnectWifi);

        Serial.println("[Improv] No WiFi config — entering Improv setup");
        Serial.println("[Improv] Use the Web Installer or type serial commands normally.");
        return true;
    }

    bool isActive() const { return _active; }

    // Call every loop() iteration.
    // Always ticks the Improv handler so it can send periodic state broadcasts
    // (required for Web Installer detection).
    // Returns false only when incoming serial data is not an Improv packet,
    // allowing SerialConsole to handle it instead.
    // On successful provisioning, saves wifi.json and reboots.
    bool loop() {
        if (!_active || !_improv) return false;

        // If data is available and it's NOT an Improv packet, let the console handle it.
        if (Serial.available() && Serial.peek() != 0x49) return false;

        // Always tick Improv — needed for periodic state broadcasts even with no data.
        _improv->handleSerial();

        if (s_improvProvisioned) {
            JsonDocument doc;
            doc["ssid"]     = s_improvSsid;
            doc["password"] = s_improvPwd;
            File f = LittleFS.open("/wifi.json", "w");
            if (f) { serializeJson(doc, f); f.close(); }
            Serial.println("[Improv] Saved — rebooting");
            delay(1000);
            ESP.restart();
        }

        return true;
    }

private:
    bool        _active  = false;
    const char* _version = nullptr;
    ImprovWiFi* _improv  = nullptr;
};
