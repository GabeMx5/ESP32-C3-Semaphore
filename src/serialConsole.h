#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "wifiConfigManager.h"
#include "mqttController.h"

class SerialConsole {
public:
    SerialConsole(WiFiConfigManager& wifi, MQTTController& mqtt, const char* firmwareVersion)
        : _wifi(wifi), _mqtt(mqtt), _version(firmwareVersion) {}

    void begin() {
        Serial.printf("\nSemaphore v%s — type 'help' for commands\n> ", _version);
    }

    void loop() {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                Serial.println();
                _buf.trim();
                if (_buf.length() > 0) {
                    Serial.printf("CMD: %s\n", _buf.c_str());
                    _process(_buf);
                }
                _buf = "";
                Serial.print("> ");
            } else if (c == 127 || c == '\b') { // backspace
                if (_buf.length() > 0) {
                    _buf.remove(_buf.length() - 1);
                    Serial.print("\b \b");
                }
            } else {
                _buf += c;
                Serial.print(c); // echo character
            }
        }
    }

private:
    WiFiConfigManager& _wifi;
    MQTTController&    _mqtt;
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

    void _print(const String& s) { Serial.printf("RST: %s\n", s.c_str()); }

    // Save wifi config using current runtime values (helper to avoid repetition)
    void _saveWifi() {
        _wifi.saveConfig(_wifi.deviceName, _wifi.ntpServer, _wifi.timezone,
                         _wifi.wifiSSID, _wifi.wifiPassword, _wifi.dhcp,
                         _wifi.localIP, _wifi.subnet, _wifi.gateway, _wifi.dns);
    }

    void _process(const String& line) {
        String cmd = _cmd(line);
        String arg = _arg(line);
        cmd.toLowerCase();

        if (cmd == "help") {
            _print("Commands:");
            _print("  status                  — device overview");
            _print("  version                 — firmware version");
            _print("  ip                      — current IP address");
            _print("  ssid                    — get WiFi SSID");
            _print("  ssid <value>            — set WiFi SSID (requires reboot)");
            _print("  password <value>        — set WiFi password (requires reboot)");
            _print("  hostname                — get mDNS hostname");
            _print("  hostname <value>        — set mDNS hostname (requires reboot)");
            _print("  dhcp                    — show DHCP mode");
            _print("  dhcp on|off             — enable/disable DHCP (requires reboot)");
            _print("  staticip <value>        — set static IP (requires reboot)");
            _print("  subnet <value>          — set subnet mask (requires reboot)");
            _print("  gateway <value>         — set gateway (requires reboot)");
            _print("  dns <value>             — set DNS server (requires reboot)");
            _print("  rssi                    — WiFi signal strength");
            _print("  heap                    — free heap memory");
            _print("  uptime                  — device uptime");
            _print("  apmode                  — switch to Access Point mode (192.168.4.1)");
            _print("  reboot                  — restart device");
            _print("  mqtt                    — show MQTT configuration");
            _print("  mqtt broker <value>     — set MQTT broker");
            _print("  mqtt port <value>       — set MQTT port");
            _print("  mqtt user <value>       — set MQTT username");
            _print("  mqtt pass <value>       — set MQTT password");
            _print("  mqtt clientid <value>   — set MQTT client ID");
            _print("  mqtt topic <value>      — set MQTT topic prefix");
            _print("  mqtt enable|disable     — enable/disable MQTT");

        } else if (cmd == "status") {
            Serial.printf("RST: Version  : %s\n", _version);
            Serial.printf("RST: IP       : %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("RST: SSID     : %s\n", WiFi.SSID().c_str());
            Serial.printf("RST: RSSI     : %d dBm\n", WiFi.RSSI());
            Serial.printf("RST: Hostname : %s.local\n", _wifi.deviceName.c_str());
            Serial.printf("RST: Heap     : %u bytes\n", ESP.getFreeHeap());
            Serial.printf("RST: Uptime   : %lu s\n", millis() / 1000);

        } else if (cmd == "version") {
            _print(_version);

        } else if (cmd == "ip") {
            _print(WiFi.localIP().toString());

        } else if (cmd == "rssi") {
            Serial.printf("RST: %d dBm\n", WiFi.RSSI());

        } else if (cmd == "heap") {
            Serial.printf("RST: %u bytes\n", ESP.getFreeHeap());

        } else if (cmd == "uptime") {
            Serial.printf("RST: %lu s\n", millis() / 1000);

        } else if (cmd == "ssid") {
            if (arg.isEmpty()) {
                _print(_wifi.wifiSSID);
            } else {
                _wifi.wifiSSID = arg;
                _saveWifi();
                _print("SSID saved. Reboot to apply.");
            }

        } else if (cmd == "password") {
            if (arg.isEmpty()) {
                _print("Usage: password <value>");
            } else {
                _wifi.wifiPassword = arg;
                _saveWifi();
                _print("Password saved. Reboot to apply.");
            }

        } else if (cmd == "hostname") {
            if (arg.isEmpty()) {
                Serial.printf("%s.local\n", _wifi.deviceName.c_str());
            } else {
                _wifi.deviceName = arg;
                _saveWifi();
                _print("Hostname saved. Reboot to apply.");
            }

        } else if (cmd == "dhcp") {
            if (arg.isEmpty()) {
                _print(_wifi.dhcp ? "on" : "off");
            } else {
                arg.toLowerCase();
                if (arg == "on") {
                    _wifi.dhcp = true;
                    _saveWifi();
                    _print("DHCP enabled. Reboot to apply.");
                } else if (arg == "off") {
                    _wifi.dhcp = false;
                    _saveWifi();
                    _print("DHCP disabled. Reboot to apply.");
                } else {
                    _print("Usage: dhcp on|off");
                }
            }

        } else if (cmd == "staticip") {
            if (arg.isEmpty()) {
                _print(_wifi.localIP.toString());
            } else {
                if (!_wifi.localIP.fromString(arg)) {
                    _print("Invalid IP address.");
                } else {
                    _saveWifi();
                    _print("Static IP saved. Reboot to apply.");
                }
            }

        } else if (cmd == "subnet") {
            if (arg.isEmpty()) {
                _print(_wifi.subnet.toString());
            } else {
                if (!_wifi.subnet.fromString(arg)) {
                    _print("Invalid subnet mask.");
                } else {
                    _saveWifi();
                    _print("Subnet saved. Reboot to apply.");
                }
            }

        } else if (cmd == "gateway") {
            if (arg.isEmpty()) {
                _print(_wifi.gateway.toString());
            } else {
                if (!_wifi.gateway.fromString(arg)) {
                    _print("Invalid gateway address.");
                } else {
                    _saveWifi();
                    _print("Gateway saved. Reboot to apply.");
                }
            }

        } else if (cmd == "dns") {
            if (arg.isEmpty()) {
                _print(_wifi.dns.toString());
            } else {
                if (!_wifi.dns.fromString(arg)) {
                    _print("Invalid DNS address.");
                } else {
                    _saveWifi();
                    _print("DNS saved. Reboot to apply.");
                }
            }

        } else if (cmd == "mqtt") {
            String sub = _cmd(arg);
            String val = _arg(arg);
            sub.toLowerCase();

            if (sub.isEmpty()) {
                // Show MQTT config
                Serial.printf("RST: Enabled  : %s\n", _mqtt.getEnabled() ? "yes" : "no");
                Serial.printf("RST: Connected : %s\n", _mqtt.isConnected() ? "yes" : "no");
                Serial.printf("RST: Broker    : %s\n", _mqtt.getBroker().c_str());
                Serial.printf("RST: Port      : %d\n", _mqtt.getPort());
                Serial.printf("RST: Username  : %s\n", _mqtt.getUsername().c_str());
                Serial.printf("RST: Client ID : %s\n", _mqtt.getClientId().c_str());
                Serial.printf("RST: Topic     : %s\n", _mqtt.getTopicPrefix().c_str());
            } else if (sub == "broker") {
                if (val.isEmpty()) { _print("Usage: mqtt broker <value>"); return; }
                _mqtt.applyConfig(val, _mqtt.getPort(), _mqtt.getUsername(),
                                  _mqtt.getPassword(), _mqtt.getClientId(),
                                  _mqtt.getTopicPrefix(), _mqtt.getEnabled());
                _mqtt.saveConfig();
                _print("Broker saved.");
            } else if (sub == "port") {
                if (val.isEmpty()) { _print("Usage: mqtt port <value>"); return; }
                int p = val.toInt();
                if (p <= 0 || p > 65535) { _print("Invalid port."); return; }
                _mqtt.applyConfig(_mqtt.getBroker(), p, _mqtt.getUsername(),
                                  _mqtt.getPassword(), _mqtt.getClientId(),
                                  _mqtt.getTopicPrefix(), _mqtt.getEnabled());
                _mqtt.saveConfig();
                _print("Port saved.");
            } else if (sub == "user") {
                if (val.isEmpty()) { _print("Usage: mqtt user <value>"); return; }
                _mqtt.applyConfig(_mqtt.getBroker(), _mqtt.getPort(), val,
                                  _mqtt.getPassword(), _mqtt.getClientId(),
                                  _mqtt.getTopicPrefix(), _mqtt.getEnabled());
                _mqtt.saveConfig();
                _print("Username saved.");
            } else if (sub == "pass") {
                if (val.isEmpty()) { _print("Usage: mqtt pass <value>"); return; }
                _mqtt.applyConfig(_mqtt.getBroker(), _mqtt.getPort(), _mqtt.getUsername(),
                                  val, _mqtt.getClientId(),
                                  _mqtt.getTopicPrefix(), _mqtt.getEnabled());
                _mqtt.saveConfig();
                _print("Password saved.");
            } else if (sub == "clientid") {
                if (val.isEmpty()) { _print("Usage: mqtt clientid <value>"); return; }
                _mqtt.applyConfig(_mqtt.getBroker(), _mqtt.getPort(), _mqtt.getUsername(),
                                  _mqtt.getPassword(), val,
                                  _mqtt.getTopicPrefix(), _mqtt.getEnabled());
                _mqtt.saveConfig();
                _print("Client ID saved.");
            } else if (sub == "topic") {
                if (val.isEmpty()) { _print("Usage: mqtt topic <value>"); return; }
                _mqtt.applyConfig(_mqtt.getBroker(), _mqtt.getPort(), _mqtt.getUsername(),
                                  _mqtt.getPassword(), _mqtt.getClientId(),
                                  val, _mqtt.getEnabled());
                _mqtt.saveConfig();
                _print("Topic prefix saved.");
            } else if (sub == "enable") {
                _mqtt.applyConfig(_mqtt.getBroker(), _mqtt.getPort(), _mqtt.getUsername(),
                                  _mqtt.getPassword(), _mqtt.getClientId(),
                                  _mqtt.getTopicPrefix(), true);
                _mqtt.saveConfig();
                _print("MQTT enabled.");
            } else if (sub == "disable") {
                _mqtt.applyConfig(_mqtt.getBroker(), _mqtt.getPort(), _mqtt.getUsername(),
                                  _mqtt.getPassword(), _mqtt.getClientId(),
                                  _mqtt.getTopicPrefix(), false);
                _mqtt.saveConfig();
                _print("MQTT disabled.");
            } else {
                Serial.printf("Unknown mqtt subcommand: %s\n", sub.c_str());
            }

        } else if (cmd == "apmode") {
            _print("Switching to Access Point mode...");
            JsonDocument apDoc;
            apDoc["apmode"] = true;
            File apf = LittleFS.open("/wifi.json", "w");
            if (apf) { serializeJson(apDoc, apf); apf.close(); }
            _print("Saved. Connect to \"Semaphore\" WiFi, then open 192.168.4.1");
            delay(500);
            ESP.restart();

        } else if (cmd == "reboot") {
            _print("Rebooting...");
            delay(200);
            ESP.restart();

        } else {
            Serial.printf("Unknown command: %s (type 'help' for list)\n", cmd.c_str());
        }
    }
};
