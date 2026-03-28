#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ─── WiFi provisioning state ──────────────────────────────────────────────────

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

// ─── Packet builder ──────────────────────────────────────────────────────────
// Packet layout: IMPROV(6) | version(1) | type(1) | length(1) | data(N) | checksum(1) | LF(1)
// Checksum = sum of all bytes from IMPROV to last data byte, mod 256.

static void improvSendPacket(uint8_t type, const uint8_t* data, uint8_t dataLen) {
    static const uint8_t HDR[] = {0x49,0x4D,0x50,0x52,0x4F,0x56,0x01};
    uint8_t cs = 0;
    for (uint8_t b : HDR) cs += b;
    cs += type + dataLen;
    for (uint8_t i = 0; i < dataLen; i++) cs += data[i];
    Serial.write(HDR, 7);
    Serial.write(&type, 1);
    Serial.write(&dataLen, 1);
    if (dataLen) Serial.write(data, dataLen);
    Serial.write(&cs, 1);
    static const uint8_t LF = 0x0A;
    Serial.write(&LF, 1);
    Serial.flush();
}

// CURRENT_STATE (type 0x01): state values: 0x02=AUTHORIZED, 0x03=PROVISIONING, 0x04=PROVISIONED
static void improvSendState(uint8_t state) {
    improvSendPacket(0x01, &state, 1);
}

// Append a length-prefixed string to buf at pos, return updated pos
static int s_improvAppend(uint8_t* buf, int pos, const char* s) {
    uint8_t n = (uint8_t)strlen(s);
    buf[pos++] = n;
    memcpy(buf + pos, s, n);
    return pos + n;
}

// RPC_RESULT (type 0x04) for REQUEST_DEVICE_INFORMATION (cmd 0x03)
// Data: [cmd=0x03, fwName(lp-str), fwVer(lp-str), chip(lp-str), devName(lp-str)]
static void improvSendDeviceInfo(const char* fwName, const char* fwVer) {
    uint8_t d[128]; int p = 0;
    d[p++] = 0x03; // responding to command 0x03
    p = s_improvAppend(d, p, fwName);
    p = s_improvAppend(d, p, fwVer);
    p = s_improvAppend(d, p, "ESP32-C3");
    p = s_improvAppend(d, p, "ESP32-C3 Semaphore");
    improvSendPacket(0x04, d, (uint8_t)p);
}

// RPC_RESULT (type 0x04) for SEND_WIFI_SETTINGS (cmd 0x01)
// Data: [cmd=0x01, url(lp-str)]  — url is the redirect shown in the installer
static void improvSendWifiResult(const char* url) {
    uint8_t d[128]; int p = 0;
    d[p++] = 0x01; // responding to command 0x01
    p = s_improvAppend(d, p, url);
    improvSendPacket(0x04, d, (uint8_t)p);
}

// ─── Packet parser ────────────────────────────────────────────────────────────
// Accumulates serial bytes into a ring buffer and processes complete packets.
// Handles all three RPC commands the web installer sends:
//   0x01 SEND_WIFI_SETTINGS      → connect, reply with URL
//   0x02 REQUEST_CURRENT_STATE   → send CURRENT_STATE
//   0x03 REQUEST_DEVICE_INFO     → send RPC_RESULT with firmware/chip strings

static uint8_t s_impBuf[256];
static int     s_impBufLen = 0;

static void improvFeedAndProcess(const char* fwName, const char* fwVer) {
    // Drain Serial into buffer
    while (Serial.available() && s_impBufLen < (int)sizeof(s_impBuf) - 1)
        s_impBuf[s_impBufLen++] = Serial.read();

    static const uint8_t HDR[] = {0x49,0x4D,0x50,0x52,0x4F,0x56,0x01};

    while (s_impBufLen >= 11) {
        // Align to IMPROV header
        if (memcmp(s_impBuf, HDR, 7) != 0) {
            memmove(s_impBuf, s_impBuf + 1, --s_impBufLen);
            continue;
        }
        uint8_t pktType = s_impBuf[7];
        uint8_t dataLen = s_impBuf[8];
        int     total   = 11 + dataLen; // 7+1+1+dataLen+1+1

        if (s_impBufLen < total) break; // wait for more bytes

        // Verify checksum and LF
        uint8_t cs = 0;
        for (int i = 0; i < total - 2; i++) cs += s_impBuf[i];
        if (cs != s_impBuf[total - 2] || s_impBuf[total - 1] != 0x0A) {
            memmove(s_impBuf, s_impBuf + 1, --s_impBufLen);
            continue;
        }

        // Handle RPC_COMMAND (type 0x03)
        if (pktType == 0x03 && dataLen >= 1) {
            uint8_t cmd        = s_impBuf[9];   // first data byte = command type
            const uint8_t* d   = s_impBuf + 9;  // d[0]=cmd, d[1..]=arguments

            if (cmd == 0x02) {
                // REQUEST_CURRENT_STATE
                improvSendState(0x02); // AUTHORIZED

            } else if (cmd == 0x03) {
                // REQUEST_DEVICE_INFORMATION
                improvSendDeviceInfo(fwName, fwVer);

            } else if (cmd == 0x01 && dataLen >= 2) {
                // SEND_WIFI_SETTINGS: d = [cmd, ssidLen, ssid..., pwdLen, pwd...]
                uint8_t ssidLen = d[1];
                char ssid[65] = {};
                memcpy(ssid, d + 2, min((int)ssidLen, 64));
                uint8_t pwdLen = d[2 + ssidLen];
                char pwd[65] = {};
                memcpy(pwd, d + 3 + ssidLen, min((int)pwdLen, 64));

                improvSendState(0x03); // PROVISIONING
                bool ok = improvConnectWifi(ssid, pwd);
                if (ok) {
                    improvSendState(0x04); // PROVISIONED
                    char url[32] = {};
                    snprintf(url, sizeof(url), "http://%s/",
                             WiFi.localIP().toString().c_str());
                    improvSendWifiResult(url);
                } else {
                    improvSendState(0x02); // back to AUTHORIZED
                    const uint8_t err[] = {0x01}; // UNABLE_TO_CONNECT
                    improvSendPacket(0x02, err, 1); // ERROR_STATE
                }
            }
        }

        // Consume processed packet
        memmove(s_impBuf, s_impBuf + total, s_impBufLen - total);
        s_impBufLen -= total;
    }
}

// ─── Step 1: call immediately after Serial.begin(), before LittleFS ──────────
// Sends an immediate CURRENT_STATE broadcast and handles any bytes already
// buffered during USB re-enumeration.
inline void improvEarlyInit(const char* firmwareVersion)
{
    improvSendState(0x02); // AUTHORIZED — immediate broadcast
    improvFeedAndProcess("ESP32-C3-Semaphore", firmwareVersion);
}

// ─── Step 2: call after LittleFS check, only when wifi.json is absent ────────
// Blocking loop — never returns. Saves wifi.json and reboots on success.
// Serial console is available for non-Improv bytes (first byte != 0x49).
inline void improvRun(const char* firmwareVersion)
{
    Serial.println("[Improv] No WiFi config — entering Improv setup");
    improvSendState(0x02);

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
        } else {
            // Improv bytes (or idle): feed to parser
            improvFeedAndProcess("ESP32-C3-Semaphore", firmwareVersion);
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
