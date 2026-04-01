#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <functional>

enum class BambuState { UNKNOWN, IDLE, PREPARE, RUNNING, PAUSE, FINISH, FAILED, INIT, SLICING, OFFLINE };

class BambuLabController {
public:
    std::function<void(BambuState)> onStateChange;

    BambuLabController() : _client(_wifiClient) {}

    bool loadConfig() {
        if (!LittleFS.exists("/bambu.json")) return false;
        File f = LittleFS.open("/bambu.json", "r");
        if (!f) return false;
        JsonDocument doc;
        if (deserializeJson(doc, f)) { f.close(); return false; }
        f.close();
        _ip         = doc["ip"]         | "";
        _serial     = doc["serial"]     | "";
        _accessCode = doc["accessCode"] | "";
        _enabled    = doc["enabled"]    | false;
        return true;
    }

    bool saveConfig() {
        JsonDocument doc;
        doc["ip"]         = _ip;
        doc["serial"]     = _serial;
        doc["accessCode"] = _accessCode;
        doc["enabled"]    = _enabled;
        File f = LittleFS.open("/bambu.json", "w");
        if (!f) return false;
        serializeJsonPretty(doc, f);
        f.close();
        return true;
    }

    void begin() {
        _instance = this;
        _wifiClient.setInsecure();
        // Bambu status payloads are ~17 KB; 20000 gives margin without wasting heap
        _client.setBufferSize(20000);
        _client.setCallback(_onMessage);
        if (!_enabled || _ip.isEmpty() || _serial.isEmpty()) return;
        _client.setServer(_ip.c_str(), 8883);
        _reconnect();
    }

    void loop() {
        if (!_enabled || _ip.isEmpty()) return;
        if (!_client.connected()) {
            unsigned long now = millis();
            if (now - _lastReconnect >= 10000) {
                _lastReconnect = now;
                _reconnect();
            }
        } else {
            _client.loop();
        }
    }

    void applyConfig(const String& ip, const String& serial,
                     const String& accessCode, bool enabled) {
        _ip         = ip;
        _serial     = serial;
        _accessCode = accessCode;
        _enabled    = enabled;
        _client.disconnect();
        _state         = BambuState::UNKNOWN;
        _lastReconnect = 0;
        if (_enabled && !_ip.isEmpty()) {
            _client.setServer(_ip.c_str(), 8883);
        }
    }

    BambuState getState()      { return _state;             }
    bool       isConnected()   { return _client.connected(); }
    bool       getEnabled()    { return _enabled;            }
    String     getIp()         { return _ip;                 }
    String     getSerial()     { return _serial;             }
    String     getAccessCode() { return _accessCode;         }
    bool       getBambuMode()             { return _bambuMode;      }
    void       setBambuMode(bool v)       { _bambuMode = v;         }
    uint8_t    getIdleTimeoutMin()        { return _idleTimeoutMin; }
    void       setIdleTimeoutMin(uint8_t v) { _idleTimeoutMin = v; }

    static const char* stateToString(BambuState s) {
        switch (s) {
            case BambuState::IDLE:    return "idle";
            case BambuState::PREPARE: return "prepare";
            case BambuState::RUNNING: return "running";
            case BambuState::PAUSE:   return "paused";
            case BambuState::FINISH:  return "finished";
            case BambuState::FAILED:  return "failed";
            case BambuState::INIT:    return "init";
            case BambuState::SLICING: return "slicing";
            case BambuState::OFFLINE: return "offline";
            default:                  return "unknown";
        }
    }

private:
    WiFiClientSecure _wifiClient;
    PubSubClient     _client;

    String        _ip;
    String        _serial;
    String        _accessCode;
    bool          _enabled        = false;
    bool          _bambuMode      = false;
    uint8_t       _idleTimeoutMin = 5;
    BambuState    _state          = BambuState::UNKNOWN;
    unsigned long _lastReconnect  = 0;

    static BambuLabController* _instance;

    static void _onMessage(char* topic, uint8_t* payload, unsigned int len) {
        if (!_instance) return;
        // Use strstr to extract gcode_state directly — avoids ArduinoJson memory issues
        // on the ~16 KB Bambu payload. PubSubClient null-terminates the buffer.
        const char* pos = strstr((const char*)payload, "\"gcode_state\"");
        if (!pos) return;
        pos = strchr(pos, '"');
        if (!pos) return;
        pos = strchr(pos + 1, '"');
        if (!pos) return;
        pos++;
        pos = strchr(pos, '"');
        if (!pos) return;
        pos++;
        const char* end = strchr(pos, '"');
        if (!end) return;
        size_t stateLen = end - pos;
        if (stateLen == 0 || stateLen > 15) return;
        char raw[16];
        memcpy(raw, pos, stateLen);
        raw[stateLen] = '\0';

        String st(raw);
        BambuState s = BambuState::UNKNOWN;
        if      (st == "IDLE")    s = BambuState::IDLE;
        else if (st == "PREPARE") s = BambuState::PREPARE;
        else if (st == "RUNNING") s = BambuState::RUNNING;
        else if (st == "PAUSE")   s = BambuState::PAUSE;
        else if (st == "FINISH")  s = BambuState::FINISH;
        else if (st == "FAILED")  s = BambuState::FAILED;
        else if (st == "INIT")    s = BambuState::INIT;
        else if (st == "SLICING") s = BambuState::SLICING;
        else if (st == "OFFLINE") s = BambuState::OFFLINE;

        if (s != _instance->_state) {
            _instance->_state = s;
            Serial.printf("[BambuLab] state: %s\n", stateToString(s));
            if (_instance->onStateChange) _instance->onStateChange(s);
        }
    }

    bool _reconnect() {
        String clientId = "semaphore_bambu_" + String(millis() % 100000);
        bool ok = _client.connect(clientId.c_str(), "bblp", _accessCode.c_str());
        if (ok) {
            String reportTopic = "device/" + _serial + "/report";
            _client.subscribe(reportTopic.c_str());
            Serial.printf("[BambuLab] connected, state: %s\n", stateToString(_state));
            String reqTopic = "device/" + _serial + "/request";
            _client.publish(reqTopic.c_str(),
                "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}");
        } else {
            Serial.printf("[BambuLab] connect failed rc=%d\n", _client.state());
        }
        return ok;
    }
};

BambuLabController* BambuLabController::_instance = nullptr;
