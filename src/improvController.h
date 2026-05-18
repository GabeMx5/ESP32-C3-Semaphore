#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ImprovWiFiLibrary.h>

static bool   s_improvProvisioned = false;
static String s_improvSsid;
static String s_improvPwd;

// setState() is private in ImprovWiFiLibrary, so we send the raw protocol bytes.
// Packet: "IMPROV" magic + version(1) + TYPE_CURRENT_STATE(0x01) + length(1) + STATE_AUTHORIZED(0x02) + checksum
// Checksum = (0x49+0x4D+0x50+0x52+0x4F+0x56+0x01+0x01+0x01+0x02) % 256 = 0xE2
static void improvAnnounceAuthorized() {
    static const uint8_t pkt[] = {
        'I','M','P','R','O','V', 0x01, 0x01, 0x01, 0x02, 0xE2
    };
    Serial.write(pkt, sizeof(pkt));
}

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
    ImprovWiFi improv(&Serial);
    improv.setDeviceInfo(
        ImprovTypes::ChipFamily::CF_ESP32_C3,
        "ESP32-C3-Semaphore",
        firmwareVersion ? firmwareVersion : "unknown",
        "ESP32-C3 Semaphore"
    );
    improv.setCustomConnectWiFi(improvConnectWifi);

    // Proactively announce STATE_AUTHORIZED so ESP Web Tools shows the Configure WiFi button.
    // The library only responds to GET_CURRENT_STATE queries; it never sends state on its own.
    improvAnnounceAuthorized();

    unsigned long startTime    = millis();
    unsigned long lastAnnounce = millis();
    while (!s_improvProvisioned) {
        improv.handleSerial();
        if (millis() - lastAnnounce > 2000) {
            improvAnnounceAuthorized();
            lastAnnounce = millis();
        }
        if (millis() - startTime > 60000) {
            Serial.println("[Improv] Timeout — switching to Access Point mode");
            // Write an empty wifi.json so Improv is not re-entered on next boot;
            // networkManager will fall through to AP mode (empty SSID).
            JsonDocument t;
            t["ssid"] = "";
            File f = LittleFS.open("/wifi.json", "w");
            if (f) { serializeJson(t, f); f.close(); }
            delay(200);
            ESP.restart();
        }
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
