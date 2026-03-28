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

// Global Improv object — created in improvEarlyInit() so it can process bytes
// buffered during USB re-enumeration, before LittleFS is even mounted.
static ImprovWiFi* s_improv = nullptr;

// ─── Helper: send an immediate CURRENT_STATE broadcast ───────────────────────
// Bypasses the library's internal timer so the web installer detects the device
// immediately. State 0x02 = AUTHORIZED (no physical access required).
static void improvSendCurrentState()
{
    // Packet: IMPROV v1, type=CURRENT_STATE(1), len=1, data=AUTHORIZED(2)
    // Checksum = sum of all bytes (from 'I') mod 256
    // Sum: 0x49+0x4D+0x50+0x52+0x4F+0x56+0x01+0x01+0x01+0x02 = 482 → 482%256 = 226 = 0xE2
    const uint8_t pkt[] = {0x49,0x4D,0x50,0x52,0x4F,0x56, 0x01, 0x01, 0x01, 0x02, 0xE2, 0x0A};
    Serial.write(pkt, sizeof(pkt));
    Serial.flush();
}

// ─── Step 1: call immediately after Serial.begin(), before LittleFS ──────────
// Creates the ImprovWiFi object, sends an immediate CURRENT_STATE broadcast,
// and processes any REQUEST_CURRENT_STATE already in the serial buffer.
inline void improvEarlyInit(const char* firmwareVersion)
{
    s_improv = new ImprovWiFi(&Serial);
    s_improv->setDeviceInfo(
        ImprovTypes::ChipFamily::CF_ESP32_C3,
        "ESP32-C3-Semaphore",
        firmwareVersion ? firmwareVersion : "unknown",
        "ESP32-C3 Semaphore"
    );
    s_improv->setCustomConnectWiFi(improvConnectWifi);

    // Immediately broadcast current state so the web installer detects us
    // without waiting for the library's internal timer.
    improvSendCurrentState();

    // Also handle any bytes already in the buffer (REQUEST_CURRENT_STATE
    // that arrived during USB re-enumeration before setup() started).
    s_improv->handleSerial();
}

// ─── Step 2: call after LittleFS check, only when wifi.json is absent ────────
// Blocking loop — never returns. Saves wifi.json and reboots on success.
// Serial console is available for non-Improv bytes (first byte != 0x49).
inline void improvRun(const char* firmwareVersion)
{
    Serial.println("[Improv] No WiFi config — entering Improv setup");

    // Re-broadcast current state now that we are fully in the Improv loop.
    improvSendCurrentState();

    String consoleBuf;

    while (!s_improvProvisioned) {
        if (Serial.available() && Serial.peek() != 0x49) {
            // Non-Improv byte → lightweight serial console
            char c = Serial.read();
            if (c == '\r') {
            } else if (c == '\n') {
                Serial.println();
                consoleBuf.trim();
                if (consoleBuf.length() > 0) {
                    Serial.printf("CMD: %s\n", consoleBuf.c_str());
                    if (consoleBuf == "help") {
                        Serial.println("RST: status  — device info");
                        Serial.println("RST: reboot  — restart device");
                        Serial.println("RST: (full console available after WiFi setup)");
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
                        Serial.println("RST: Unknown command (WiFi not configured yet)");
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
        } else if (Serial.available()) {
            // Improv packet incoming (first byte == 0x49).
            // Immediately send CURRENT_STATE so the web installer's read loop
            // receives it before timing out, then let the library process the
            // full packet (needed for SEND_WIFI_SETTINGS / connect flow).
            improvSendCurrentState();
            s_improv->handleSerial();
        } else {
            s_improv->handleSerial(); // periodic broadcasts / idle tick
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
