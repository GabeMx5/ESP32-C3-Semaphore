#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <functional>
#include "wifiConfigManager.h"
#include "mqttController.h"

class SerialConsole {
public:
    // Optional callback: called for every output line (without "RST: " prefix/newline)
    std::function<void(const String&)> onOutput;

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

    // Execute a command from the web console (echoes it and runs it)
    void executeFromWeb(const String& cmd) {
        String line = cmd;
        line.trim();
        if (line.length() == 0) return;
        _raw(String("> ") + line);
        _process(line);
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

    // Output a line to Serial and invoke onOutput callback
    void _raw(const String& s) {
        Serial.printf("RST: %s\n", s.c_str());
        if (onOutput) onOutput(s);
    }

    // Printf-style output routed through _raw
    void _rawf(const char* fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        _raw(String(buf));
    }

    void _print(const String& s) { _raw(s); }

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
            _rawf("Version  : %s", _version);
            _rawf("IP       : %s", WiFi.localIP().toString().c_str());
            _rawf("SSID     : %s", WiFi.SSID().c_str());
            _rawf("RSSI     : %d dBm", WiFi.RSSI());
            _rawf("Hostname : %s.local", _wifi.deviceName.c_str());
            _rawf("Heap     : %u bytes", ESP.getFreeHeap());
            _rawf("Uptime   : %lu s", millis() / 1000);

        } else if (cmd == "version") {
            _print(_version);

        } else if (cmd == "ip") {
            _print(WiFi.localIP().toString());

        } else if (cmd == "rssi") {
            _rawf("%d dBm", WiFi.RSSI());

        } else if (cmd == "heap") {
            _rawf("%u bytes", ESP.getFreeHeap());

        } else if (cmd == "uptime") {
            _rawf("%lu s", millis() / 1000);

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
                _rawf("%s.local", _wifi.deviceName.c_str());
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
                _rawf("Enabled  : %s", _mqtt.getEnabled() ? "yes" : "no");
                _rawf("Connected : %s", _mqtt.isConnected() ? "yes" : "no");
                _rawf("Broker    : %s", _mqtt.getBroker().c_str());
                _rawf("Port      : %d", _mqtt.getPort());
                _rawf("Username  : %s", _mqtt.getUsername().c_str());
                _rawf("Client ID : %s", _mqtt.getClientId().c_str());
                _rawf("Topic     : %s", _mqtt.getTopicPrefix().c_str());
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
                _rawf("Unknown mqtt subcommand: %s", sub.c_str());
            }

        } else if (cmd == "reboot") {
            _print("Rebooting...");
            delay(200);
            ESP.restart();

        } else {
            _rawf("Unknown command: %s (type 'help' for list)", cmd.c_str());
        }
    }
};
