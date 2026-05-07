#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <functional>

enum class BambuState
{
    UNKNOWN,
    IDLE,
    PREPARE,
    RUNNING,
    PAUSE,
    FINISH,
    FAILED,
    INIT,
    SLICING,
    OFFLINE
};

class BambuLabController
{
public:
    std::function<void(BambuState)> onStateChange;
    std::function<void()>           onIdleTimeout;

    BambuLabController() : _client(_wifiClient) { _initDefaultColors(); }

    bool loadConfig()
    {
        _initDefaultColors();
        if (!LittleFS.exists("/bambu.json"))
            return false;
        File f = LittleFS.open("/bambu.json", "r");
        if (!f)
            return false;
        JsonDocument doc;
        if (deserializeJson(doc, f))
        {
            f.close();
            return false;
        }
        f.close();
        _ip = doc["ip"] | "";
        _serial = doc["serial"] | "";
        _accessCode = doc["accessCode"] | "";
        _enabled = doc["enabled"] | false;
        if (doc["stateColors"].is<JsonObject>())
        {
            for (JsonPair kv : doc["stateColors"].as<JsonObject>())
            {
                BambuState s = stateFromString(kv.key().c_str());
                if (!kv.value().is<JsonArray>())
                    continue;
                JsonArray arr = kv.value().as<JsonArray>();
                if (arr.size() != 9)
                    continue;
                int si = _stateToIndex(s);
                for (int led = 0; led < 3; led++)
                    for (int c = 0; c < 3; c++)
                        _stateColors[si][led][c] = arr[led * 3 + c] | 0;
            }
        }
        return true;
    }

    bool saveConfig()
    {
        JsonDocument doc;
        doc["ip"] = _ip;
        doc["serial"] = _serial;
        doc["accessCode"] = _accessCode;
        doc["enabled"] = _enabled;
        _serializeStateColors(doc);
        File f = LittleFS.open("/bambu.json", "w");
        if (!f)
            return false;
        serializeJsonPretty(doc, f);
        f.close();
        return true;
    }

    void begin()
    {
        _instance = this;
        _wifiClient.setInsecure();
        _wifiClient.setTimeout(15000);
        
        _client.setBufferSize(_maxMessageSize);
        _client.setCallback(_onMessage);


        if (!_enabled || _ip.isEmpty() || _serial.isEmpty())
            return;
        _client.setServer(_ip.c_str(), 8883);
        _reconnect();
    }

    void loop()
    {
        if (!_enabled || _ip.isEmpty())
            return;
        if (!_client.connected() && !_reconnecting)
        {
            unsigned long now = millis();
            if (now - _lastReconnect >= 10000)
            {
                _lastReconnect = now;
                _reconnecting  = true;
                xTaskCreate(_reconnectTaskFn, "bambu_rc", 10240, this, 1, NULL);
            }
        }
        else if (_client.connected())
        {
            _client.loop();
        }

        if (_pendingLen > 0)
        {
            _parsePayload();
            _pendingLen = 0;
        }

        if (_bambuMode && _client.connected() && _idleSince > 0 && !_idleLedOff)
        {
            if (_idleTimeoutMin > 0 && millis() - _idleSince >= (unsigned long)_idleTimeoutMin * 60000UL)
            {
                _idleLedOff = true;
                if (onIdleTimeout) onIdleTimeout();
            }
        }
    }

    void resetIdle()
    {
        _idleSince  = 0;
        _idleLedOff = false;
    }

    void markIdle()
    {
        if (_idleSince == 0) _idleSince = millis();
    }

    long getIdleSec() const
    {
        return (_idleSince > 0) ? (long)((millis() - _idleSince) / 1000) : -1L;
    }

    void applyConfig(const String &ip, const String &serial,
                     const String &accessCode, bool enabled)
    {
        _ip = ip;
        _serial = serial;
        _accessCode = accessCode;
        _enabled = enabled;
        _client.disconnect();
        _state = BambuState::UNKNOWN;
        _lastReconnect = 0;
        if (_enabled && !_ip.isEmpty())
        {
            _client.setServer(_ip.c_str(), 8883);
        }
    }

    BambuState getState() { return _state; }
    bool isConnected() { return _client.connected(); }
    bool getEnabled() { return _enabled; }
    String getIp() { return _ip; }
    String getSerial() { return _serial; }
    String getAccessCode() { return _accessCode; }
    bool getBambuMode() { return _bambuMode; }
    void setBambuMode(bool v) { _bambuMode = v; }
    uint8_t getIdleTimeoutMin() { return _idleTimeoutMin; }
    void setIdleTimeoutMin(uint8_t v) { _idleTimeoutMin = v; }

    static const char *stateToString(BambuState s)
    {
        switch (s)
        {
        case BambuState::IDLE:
            return "idle";
        case BambuState::PREPARE:
            return "prepare";
        case BambuState::RUNNING:
            return "running";
        case BambuState::PAUSE:
            return "paused";
        case BambuState::FINISH:
            return "finished";
        case BambuState::FAILED:
            return "failed";
        case BambuState::INIT:
            return "init";
        case BambuState::SLICING:
            return "slicing";
        case BambuState::OFFLINE:
            return "offline";
        default:
            return "unknown";
        }
    }

    static BambuState stateFromString(const char *str)
    {
        if (!str)
            return BambuState::UNKNOWN;
        if (strcmp(str, "idle") == 0)
            return BambuState::IDLE;
        if (strcmp(str, "prepare") == 0)
            return BambuState::PREPARE;
        if (strcmp(str, "running") == 0)
            return BambuState::RUNNING;
        if (strcmp(str, "paused") == 0)
            return BambuState::PAUSE;
        if (strcmp(str, "finished") == 0)
            return BambuState::FINISH;
        if (strcmp(str, "failed") == 0)
            return BambuState::FAILED;
        if (strcmp(str, "init") == 0)
            return BambuState::INIT;
        if (strcmp(str, "slicing") == 0)
            return BambuState::SLICING;
        if (strcmp(str, "offline") == 0)
            return BambuState::OFFLINE;
        return BambuState::UNKNOWN;
    }

    void getStateColor(BambuState s, int led, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        int si = _stateToIndex(s);
        r = _stateColors[si][led][0];
        g = _stateColors[si][led][1];
        b = _stateColors[si][led][2];
    }

    void setStateColor(BambuState s, int led, uint8_t r, uint8_t g, uint8_t b)
    {
        int si = _stateToIndex(s);
        _stateColors[si][led][0] = r;
        _stateColors[si][led][1] = g;
        _stateColors[si][led][2] = b;
    }

    void addStateColorsToJson(JsonDocument &doc)
    {
        _serializeStateColors(doc);
    }

private:
    WiFiClientSecure _wifiClient;
    PubSubClient _client;

    String _ip;
    String _serial;
    String _accessCode;
    bool _enabled = false;
    bool _bambuMode = false;
    uint8_t _idleTimeoutMin = 5;
    unsigned long _idleSince  = 0;
    bool _idleLedOff = false;
    BambuState _state = BambuState::UNKNOWN;
    unsigned long _lastReconnect = 0;
    volatile bool _reconnecting  = false;

    static void _reconnectTaskFn(void* arg)
    {
        BambuLabController* self = static_cast<BambuLabController*>(arg);
        self->_reconnect();
        self->_reconnecting = false;
        vTaskDelete(NULL);
    }
    // _stateColors[stateIndex][ledIndex(0-2)][rgb(0-2)]
    uint8_t _stateColors[10][3][3];

    static const unsigned int _maxMessageSize = 32768;
    static char               _pendingBuf[_maxMessageSize + 1];
    static unsigned int       _pendingLen;

    static int _stateToIndex(BambuState s)
    {
        switch (s)
        {
        case BambuState::IDLE:
            return 1;
        case BambuState::PREPARE:
            return 2;
        case BambuState::RUNNING:
            return 3;
        case BambuState::PAUSE:
            return 4;
        case BambuState::FINISH:
            return 5;
        case BambuState::FAILED:
            return 6;
        case BambuState::INIT:
            return 7;
        case BambuState::SLICING:
            return 8;
        case BambuState::OFFLINE:
            return 9;
        default:
            return 0;
        }
    }

    void _initDefaultColors()
    {
        memset(_stateColors, 0, sizeof(_stateColors));
        // IDLE (1): bottom LED green
        _stateColors[1][0][1] = 255;
        // PREPARE (2): top LED red
        _stateColors[2][2][0] = 255;
        // RUNNING (3): middle LED orange
        _stateColors[3][1][0] = 255;
        _stateColors[3][1][1] = 165;
        // PAUSE (4): top LED red
        _stateColors[4][2][0] = 255;
        // FINISH (5): bottom LED green
        _stateColors[5][0][1] = 255;
        // FAILED (6): top LED red
        _stateColors[6][2][0] = 255;
        // INIT (7): top LED red
        _stateColors[7][2][0] = 255;
        // SLICING (8): top LED red
        _stateColors[8][2][0] = 255;
        // UNKNOWN (0) and OFFLINE (9): all off (zero)
    }

    void _serializeStateColors(JsonDocument &doc)
    {
        static const BambuState ALL_STATES[] = {
            BambuState::UNKNOWN, BambuState::IDLE, BambuState::PREPARE, BambuState::RUNNING,
            BambuState::PAUSE, BambuState::FINISH, BambuState::FAILED,
            BambuState::INIT, BambuState::SLICING, BambuState::OFFLINE};
        JsonObject sc = doc["stateColors"].to<JsonObject>();
        for (BambuState s : ALL_STATES)
        {
            JsonArray arr = sc[stateToString(s)].to<JsonArray>();
            int si = _stateToIndex(s);
            for (int led = 0; led < 3; led++)
                for (int c = 0; c < 3; c++)
                    arr.add(_stateColors[si][led][c]);
        }
    }

    static BambuLabController *_instance;

    static void _onMessage(char *topic, uint8_t *payload, unsigned int len)
    {
        if (!_instance || !payload || len == 0)
            return;
        long start = millis();
        
        unsigned int toCopy = len < _maxMessageSize ? len : _maxMessageSize;
        memcpy(_pendingBuf, payload, toCopy);
        _pendingBuf[toCopy] = '\0';
        _pendingLen = toCopy;
    }

    void _parsePayload()
    {
        const char *pos = strstr(_pendingBuf, "\"gcode_state\"");
        if (!pos) return;

        pos = strchr(pos, ':');
        if (!pos) return;
        pos = strchr(pos, '"');
        if (!pos) return;
        pos++;

        const char *end = strchr(pos, '"');
        if (!end) return;

        size_t stateLen = end - pos;
        if (stateLen == 0 || stateLen > 15) return;

        char raw[16];
        memcpy(raw, pos, stateLen);
        raw[stateLen] = '\0';

        BambuState s = BambuState::UNKNOWN;
        if      (strcmp(raw, "IDLE")    == 0) s = BambuState::IDLE;
        else if (strcmp(raw, "PREPARE") == 0) s = BambuState::PREPARE;
        else if (strcmp(raw, "RUNNING") == 0) s = BambuState::RUNNING;
        else if (strcmp(raw, "PAUSE")   == 0) s = BambuState::PAUSE;
        else if (strcmp(raw, "FINISH")  == 0) s = BambuState::FINISH;
        else if (strcmp(raw, "FAILED")  == 0) s = BambuState::FAILED;
        else if (strcmp(raw, "INIT")    == 0) s = BambuState::INIT;
        else if (strcmp(raw, "SLICING") == 0) s = BambuState::SLICING;
        else if (strcmp(raw, "OFFLINE") == 0) s = BambuState::OFFLINE;

        if (s != _state)
        {
            _state = s;
            Serial.printf("[BambuLab] state: %s\n", stateToString(s));
            if (onStateChange) onStateChange(s);
        }
    }

    bool _reconnect()
    {
        String clientId = "semaphore_bambu_" + String(millis() % 100000);
        bool ok = _client.connect(clientId.c_str(), "bblp", _accessCode.c_str());
        if (ok)
        {
            String reportTopic = "device/" + _serial + "/report";
            _client.subscribe(reportTopic.c_str());
            Serial.printf("[BambuLab] connected, state: %s\n", stateToString(_state));
            // pushall response is ~17 KB and overflows the 16 KB mbedTLS receive
            // buffer in the pre-built ESP32-C3 Arduino SDK, causing a heap
            // corruption crash. Periodic status reports from the printer are
            // much smaller and arrive within seconds — no need to request pushall.
        }
        else
        {
            Serial.printf("[BambuLab] connect failed rc=%d\n", _client.state());
        }
        return ok;
    }
};

BambuLabController *BambuLabController::_instance  = nullptr;
char               BambuLabController::_pendingBuf[BambuLabController::_maxMessageSize + 1];
unsigned int       BambuLabController::_pendingLen = 0;
