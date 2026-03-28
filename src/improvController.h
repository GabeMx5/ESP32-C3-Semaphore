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

// ─── Improv setup ─────────────────────────────────────────────────────────────
//
// Blocks until WiFi credentials are received via the Web Installer.
// Serial bytes are routed by first byte:
//   0x49 ('I', Improv magic header) → Improv handler
//   anything else                   → lightweight serial console
//
// Saves wifi.json and reboots on success. Never returns.
//
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

    // Lightweight console buffer — full SerialConsole not used here to avoid
    // printing the banner before Improv initialises (confuses the Web Installer).
    String consoleBuf;

    while (!s_improvProvisioned) {
        if (Serial.available() && Serial.peek() != 0x49) {
            // Non-Improv byte — handle as serial console input
            char c = Serial.read();
            if (c == '\r') {
                // ignore
            } else if (c == '\n') {
                Serial.println();
                consoleBuf.trim();
                if (consoleBuf.length() > 0) {
                    Serial.printf("CMD: %s\n", consoleBuf.c_str());
                    if (consoleBuf == "help") {
                        Serial.println("RST: status  — device info");
                        Serial.println("RST: reboot  — restart device");
                        Serial.println("RST: (other serial commands available after WiFi setup)");
                    } else if (consoleBuf == "status") {
                        Serial.printf("RST: Version : %s\n", firmwareVersion);
                        Serial.printf("RST: Heap    : %u bytes\n", ESP.getFreeHeap());
                        Serial.printf("RST: Uptime  : %lu s\n", millis() / 1000);
                        Serial.println("RST: WiFi    : not configured — waiting for Improv setup");
                    } else if (consoleBuf == "reboot") {
                        Serial.println("RST: Rebooting...");
                        delay(200);
                        ESP.restart();
                    } else {
                        Serial.printf("RST: Unknown command (WiFi not configured yet)\n");
                    }
                    consoleBuf = "";
                }
                Serial.print("> ");
            } else if (c == 127 || c == '\b') {
                if (consoleBuf.length() > 0) {
                    consoleBuf.remove(consoleBuf.length() - 1);
                    Serial.print("\b \b");
                }
            } else {
                consoleBuf += c;
                Serial.print(c);
            }
        } else {
            improv.handleSerial();
        }
        delay(1);
    }

    JsonDocument doc;
    doc["ssid"]     = s_improvSsid;
    doc["password"] = s_improvPwd;
    File f = LittleFS.open("/wifi.json", "w");
    if (f) { serializeJson(doc, f); f.close(); }
    Serial.println("[Improv] Saved — rebooting");
    delay(1000);
    ESP.restart();
}
