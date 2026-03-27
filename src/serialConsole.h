#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "wifiConfigManager.h"

class SerialConsole {
public:
    SerialConsole(WiFiConfigManager& wifi, const char* firmwareVersion)
        : _wifi(wifi), _version(firmwareVersion) {}

    void loop() {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                _buf.trim();
                if (_buf.length() > 0) _process(_buf);
                _buf = "";
            } else {
                _buf += c;
            }
        }
    }

private:
    WiFiConfigManager& _wifi;
    const char*        _version;
    String             _buf;

    // Split "cmd arg" into cmd and arg
    static String _cmd(const String& line) {
        int i = line.indexOf(' ');
        return i < 0 ? line : line.substring(0, i);
    }
    static String _arg(const String& line) {
        int i = line.indexOf(' ');
        return i < 0 ? "" : line.substring(i + 1);
    }

    void _print(const String& s) { Serial.println(s); }

    void _process(const String& line) {
        String cmd = _cmd(line);
        String arg = _arg(line);
        cmd.toLowerCase();

        if (cmd == "help") {
            _print("Commands:");
            _print("  status              — device overview");
            _print("  version             — firmware version");
            _print("  ip                  — current IP address");
            _print("  ssid                — get WiFi SSID");
            _print("  ssid <value>        — set WiFi SSID (requires reboot)");
            _print("  password <value>    — set WiFi password (requires reboot)");
            _print("  hostname            — get mDNS hostname");
            _print("  hostname <value>    — set mDNS hostname (requires reboot)");
            _print("  rssi                — WiFi signal strength");
            _print("  heap                — free heap memory");
            _print("  uptime              — device uptime");
            _print("  reboot              — restart device");

        } else if (cmd == "status") {
            _print("--- Semaphore Status ---");
            Serial.printf("Version  : %s\n", _version);
            Serial.printf("IP       : %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("SSID     : %s\n", WiFi.SSID().c_str());
            Serial.printf("RSSI     : %d dBm\n", WiFi.RSSI());
            Serial.printf("Hostname : %s.local\n", _wifi.deviceName.c_str());
            Serial.printf("Heap     : %u bytes\n", ESP.getFreeHeap());
            Serial.printf("Uptime   : %lu s\n", millis() / 1000);

        } else if (cmd == "version") {
            _print(_version);

        } else if (cmd == "ip") {
            _print(WiFi.localIP().toString());

        } else if (cmd == "rssi") {
            Serial.printf("%d dBm\n", WiFi.RSSI());

        } else if (cmd == "heap") {
            Serial.printf("%u bytes\n", ESP.getFreeHeap());

        } else if (cmd == "uptime") {
            Serial.printf("%lu s\n", millis() / 1000);

        } else if (cmd == "ssid") {
            if (arg.isEmpty()) {
                _print(_wifi.wifiSSID);
            } else {
                _wifi.wifiSSID = arg;
                _wifi.saveConfig(_wifi.deviceName, _wifi.ntpServer, _wifi.timezone,
                                 _wifi.wifiSSID, _wifi.wifiPassword, _wifi.dhcp,
                                 _wifi.localIP, _wifi.subnet, _wifi.gateway, _wifi.dns);
                _print("SSID saved. Reboot to apply.");
            }

        } else if (cmd == "password") {
            if (arg.isEmpty()) {
                _print("Usage: password <value>");
            } else {
                _wifi.wifiPassword = arg;
                _wifi.saveConfig(_wifi.deviceName, _wifi.ntpServer, _wifi.timezone,
                                 _wifi.wifiSSID, _wifi.wifiPassword, _wifi.dhcp,
                                 _wifi.localIP, _wifi.subnet, _wifi.gateway, _wifi.dns);
                _print("Password saved. Reboot to apply.");
            }

        } else if (cmd == "hostname") {
            if (arg.isEmpty()) {
                Serial.printf("%s.local\n", _wifi.deviceName.c_str());
            } else {
                _wifi.deviceName = arg;
                _wifi.saveConfig(_wifi.deviceName, _wifi.ntpServer, _wifi.timezone,
                                 _wifi.wifiSSID, _wifi.wifiPassword, _wifi.dhcp,
                                 _wifi.localIP, _wifi.subnet, _wifi.gateway, _wifi.dns);
                _print("Hostname saved. Reboot to apply.");
            }

        } else if (cmd == "reboot") {
            _print("Rebooting...");
            delay(200);
            ESP.restart();

        } else {
            Serial.printf("Unknown command: %s (type 'help' for list)\n", cmd.c_str());
        }
    }
};
