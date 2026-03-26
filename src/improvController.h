#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Minimal Improv Wi-Fi Serial protocol — https://www.improv-wifi.com/serial/
// No external library needed.

namespace Improv {
    static const uint8_t HEADER[6] = {'I','M','P','R','O','V'};
    static const uint8_t VERSION   = 1;

    enum Type    : uint8_t { CURRENT_STATE = 0x01, ERROR_STATE = 0x02, RPC_COMMAND = 0x03, RPC_RESULT = 0x04 };
    enum State   : uint8_t { AUTHORIZED = 0x02, PROVISIONING = 0x03, PROVISIONED = 0x04 };
    enum Error   : uint8_t { NO_ERROR = 0x00, UNABLE_TO_CONNECT = 0x03, UNKNOWN = 0xFF };
    enum Command : uint8_t { SEND_WIFI_SETTINGS = 0x01 };

    static void sendPacket(Stream& s, Type type, const uint8_t* data, uint8_t len) {
        s.write(HEADER, 6);
        s.write(VERSION);
        s.write((uint8_t)type);
        s.write(len);
        if (len) s.write(data, len);
        uint8_t cs = 0;
        for (int i = 0; i < 6; i++) cs += HEADER[i];
        cs += VERSION + (uint8_t)type + len;
        for (int i = 0; i < len; i++) cs += data[i];
        s.write(cs);
    }

    static void sendState(Stream& s, State state) {
        uint8_t d = (uint8_t)state;
        sendPacket(s, CURRENT_STATE, &d, 1);
    }

    static void sendError(Stream& s, Error error) {
        uint8_t d = (uint8_t)error;
        sendPacket(s, ERROR_STATE, &d, 1);
    }

    static void sendResult(Stream& s, const String& url) {
        uint8_t urlLen = (uint8_t)url.length();
        uint8_t buf[2 + urlLen];
        buf[0] = 0x01; // command echo
        buf[1] = urlLen;
        memcpy(buf + 2, url.c_str(), urlLen);
        sendPacket(s, RPC_RESULT, buf, 2 + urlLen);
    }
}

// Blocks until WiFi credentials are received, device connects,
// saves wifi.json and reboots. Never returns on success.
inline void runImprovSetup(const char* firmwareVersion)
{
    Serial.println("[Improv] No WiFi config — entering Improv setup");
    Improv::sendState(Serial, Improv::AUTHORIZED);

    static uint8_t buf[300];
    int  bufLen    = 0;
    int  headerIdx = 0;
    bool inPacket  = false;
    unsigned long lastState = 0;

    while (true) {
        if (millis() - lastState > 1000) {
            Improv::sendState(Serial, Improv::AUTHORIZED);
            lastState = millis();
        }

        while (Serial.available()) {
            uint8_t b = Serial.read();

            if (!inPacket) {
                // Match IMPROV header byte by byte
                if (b == Improv::HEADER[headerIdx]) {
                    buf[headerIdx++] = b;
                    if (headerIdx == 6) { inPacket = true; bufLen = 6; headerIdx = 0; }
                } else {
                    headerIdx = (b == Improv::HEADER[0]) ? 1 : 0;
                    if (headerIdx) buf[0] = b;
                }
                continue;
            }

            buf[bufLen++] = b;
            if (bufLen < 9) continue; // need header(6) + ver + type + length

            uint8_t dataLen      = buf[8];
            uint8_t expectedLen  = 9 + dataLen + 1; // +1 checksum
            if (bufLen < expectedLen) continue;

            // Verify checksum
            uint8_t cs = 0;
            for (int i = 0; i < expectedLen - 1; i++) cs += buf[i];
            inPacket = false; bufLen = 0;
            if (cs != buf[expectedLen - 1]) { Serial.println("[Improv] Bad checksum"); continue; }

            uint8_t type = buf[7];
            uint8_t* data = buf + 9;

            if (type != (uint8_t)Improv::RPC_COMMAND) continue;

            // GET_CURRENT_STATE (0x02): reply with current state
            if (data[0] == 0x02) {
                uint8_t res[3] = { 0x02, 0x01, (uint8_t)Improv::AUTHORIZED };
                Improv::sendPacket(Serial, Improv::RPC_RESULT, res, 3);
                continue;
            }

            // GET_DEVICE_INFO (0x03): reply with firmware/hardware info
            if (data[0] == 0x03) {
                const char* fwName  = "ESP32-C3 Semaphore";
                const char* fwVer   = firmwareVersion ? firmwareVersion : "unknown";
                const char* hwName  = "Seeed XIAO ESP32-C3";
                const char* chip    = "ESP32-C3";
                uint8_t n1 = strlen(fwName), n2 = strlen(fwVer),
                        n3 = strlen(hwName),  n4 = strlen(chip);
                uint8_t total = n1 + n2 + n3 + n4 + 4; // +4 length bytes
                uint8_t res[2 + total];
                res[0] = 0x03; res[1] = total;
                int p = 2;
                res[p++] = n1; memcpy(res+p, fwName, n1);  p += n1;
                res[p++] = n2; memcpy(res+p, fwVer,  n2);  p += n2;
                res[p++] = n3; memcpy(res+p, hwName,  n3); p += n3;
                res[p++] = n4; memcpy(res+p, chip,    n4);
                Improv::sendPacket(Serial, Improv::RPC_RESULT, res, 2 + total);
                continue;
            }

            if (data[0] != (uint8_t)Improv::SEND_WIFI_SETTINGS) continue;

            // data: [cmd][sub_len][ssid_len][ssid...][pwd_len][pwd...]
            uint8_t ssidLen = data[2];
            String  ssid    = String((char*)(data + 3), ssidLen);
            uint8_t pwdLen  = data[3 + ssidLen];
            String  pwd     = String((char*)(data + 4 + ssidLen), pwdLen);

            Serial.printf("[Improv] Connecting to: %s\n", ssid.c_str());
            Improv::sendState(Serial, Improv::PROVISIONING);

            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid.c_str(), pwd.c_str());
            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
                delay(100);

            if (WiFi.status() == WL_CONNECTED) {
                String url = "http://" + WiFi.localIP().toString() + "/";
                Improv::sendState(Serial, Improv::PROVISIONED);
                Improv::sendResult(Serial, url);

                JsonDocument doc;
                doc["ssid"]     = WiFi.SSID();
                doc["password"] = WiFi.psk();
                File f = LittleFS.open("/wifi.json", "w");
                if (f) { serializeJson(doc, f); f.close(); }
                Serial.printf("[Improv] Saved — rebooting\n");
                delay(1000);
                ESP.restart();
            } else {
                Improv::sendError(Serial, Improv::UNABLE_TO_CONNECT);
                Improv::sendState(Serial, Improv::AUTHORIZED);
            }
        }
        delay(1);
    }
}
