#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

typedef void (*MQTTCommandHandler)(uint8_t *, unsigned int);
typedef void (*MQTTConnectedHandler)();

class MQTTController
{
private:
    WiFiClient   wifiClient;
    PubSubClient mqttClient;

    String broker;
    int    port        = 1883;
    String username;
    String password;
    String clientId    = "semaphore";
    String topicPrefix = "semaphore";
    String cmdTopic;
    String statusTopic;
    bool   enabled     = false;

    unsigned long lastReconnectAttempt = 0;
    bool          pendingConnected     = false;

    static MQTTController *_instance;

    static void onMessage(char *topic, uint8_t *payload, unsigned int length)
    {
        if (!_instance || !_instance->commandHandler) return;

        // Per-LED HA commands: translate from HA JSON format to our protocol
        String haCmdPrefix = _instance->topicPrefix + "/cmd/led/";
        if (String(topic).startsWith(haCmdPrefix))
        {
            int ledIdx = String(topic).substring(haCmdPrefix.length()).toInt();
            JsonDocument src;
            if (deserializeJson(src, payload, length)) return;
            JsonDocument out;
            out["type"]  = "setLed";
            out["led"]   = ledIdx;
            out["on"] = (strcmp(src["state"] | "", "ON") == 0);
            int brightness = src["brightness"] | 255;
            if (!src["color"].isNull())
            {
                out["r"] = (int)(src["color"]["r"] | 0) * brightness / 255;
                out["g"] = (int)(src["color"]["g"] | 0) * brightness / 255;
                out["b"] = (int)(src["color"]["b"] | 0) * brightness / 255;
            }
            else if (!src["brightness"].isNull())
            {
                out["brightness"] = brightness;
            }
            out["blink"] = false;
            String translated;
            serializeJson(out, translated);
            _instance->commandHandler((uint8_t *)translated.c_str(), translated.length());
            return;
        }

        _instance->commandHandler(payload, length);
    }

    bool reconnect()
    {
        Serial.printf("MQTT connecting to %s:%d...\n", broker.c_str(), port);
        bool ok = username.isEmpty()
            ? mqttClient.connect(clientId.c_str())
            : mqttClient.connect(clientId.c_str(), username.c_str(), password.c_str());
        if (ok)
        {
            Serial.println("MQTT connected, subscribing to " + cmdTopic);
            mqttClient.subscribe(cmdTopic.c_str());
            String ledCmdWildcard = topicPrefix + "/cmd/led/+";
            mqttClient.subscribe(ledCmdWildcard.c_str());
            publishDiscovery();
            pendingConnected = true;
        }
        else
        {
            Serial.printf("MQTT failed, rc=%d\n", mqttClient.state());
        }
        return ok;
    }

public:
    MQTTCommandHandler  commandHandler  = nullptr;
    MQTTConnectedHandler connectedHandler = nullptr;

    bool loadConfig()
    {
        if (!LittleFS.exists("/mqtt.json")) return false;
        File file = LittleFS.open("/mqtt.json", "r");
        if (!file) return false;
        JsonDocument doc;
        if (deserializeJson(doc, file)) { file.close(); return false; }
        file.close();
        broker      = doc["broker"]   | "";
        port        = doc["port"]     | 1883;
        username    = doc["username"] | "";
        password    = doc["password"] | "";
        clientId    = doc["clientId"] | "semaphore";
        topicPrefix = doc["topic"]    | "semaphore";
        enabled     = doc["enabled"]  | false;
        cmdTopic    = topicPrefix + "/cmd";
        statusTopic = topicPrefix + "/status";
        return !broker.isEmpty();
    }

    void begin(MQTTCommandHandler handler)
    {
        commandHandler = handler;
        _instance      = this;
        if (!loadConfig() || !enabled)
        {
            Serial.println("MQTT: disabled or broker not configured, skipping");
            return;
        }
        mqttClient.setClient(wifiClient);
        mqttClient.setServer(broker.c_str(), port);
        mqttClient.setBufferSize(2048);
        mqttClient.setCallback(onMessage);
        reconnect();
    }

    void loop()
    {
        if (broker.isEmpty() || !enabled) return;
        if (!mqttClient.connected())
        {
            unsigned long now = millis();
            if (now - lastReconnectAttempt >= 5000)
            {
                lastReconnectAttempt = now;
                reconnect();
            }
        }
        else
        {
            mqttClient.loop();
            if (pendingConnected)
            {
                pendingConnected = false;
                if (connectedHandler) connectedHandler();
            }
        }
    }

    void publish(const String &payload)
    {
        if (!mqttClient.connected() || statusTopic.isEmpty()) return;
        mqttClient.publish(statusTopic.c_str(), payload.c_str());
    }

    bool isConnected() { return mqttClient.connected(); }

    String getBroker()      { return broker;      }
    int    getPort()        { return port;        }
    String getUsername()    { return username;    }
    String getPassword()    { return password;    }
    String getClientId()    { return clientId;    }
    String getTopicPrefix() { return topicPrefix; }

    bool saveConfig()
    {
        JsonDocument doc;
        doc["broker"]   = broker;
        doc["port"]     = port;
        doc["username"] = username;
        doc["password"] = password;
        doc["clientId"] = clientId;
        doc["topic"]    = topicPrefix;
        doc["enabled"]  = enabled;
        File file = LittleFS.open("/mqtt.json", "w");
        if (!file) return false;
        serializeJsonPretty(doc, file);
        file.close();
        return true;
    }

    void publishSwitchState(const char *name, bool on)
    {
        if (!mqttClient.connected()) return;
        String topic = topicPrefix + "/status/switch/" + name;
        mqttClient.publish(topic.c_str(), on ? "ON" : "OFF", true);
    }

    void publishLedState(int index, int r, int g, int b, bool on)
    {
        if (!mqttClient.connected()) return;
        String topic = topicPrefix + "/status/led/" + index;
        JsonDocument doc;
        int maxC = max({r, g, b});
        doc["state"]          = on ? "ON" : "OFF";
        doc["color_mode"]     = "rgb";
        doc["brightness"]     = maxC;
        doc["color"]["r"]     = maxC > 0 ? r * 255 / maxC : 0;
        doc["color"]["g"]     = maxC > 0 ? g * 255 / maxC : 0;
        doc["color"]["b"]     = maxC > 0 ? b * 255 / maxC : 0;
        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(topic.c_str(), payload.c_str(), true);
    }

    void publishDiscovery()
    {
        if (!mqttClient.connected()) return;

        const char *ledNames[3] = {"Bottom LED", "Middle LED", "Top LED"};

        for (int i = 0; i < 3; i++)
        {
            String uid        = clientId + "_led_" + i;
            String discTopic  = "homeassistant/light/" + uid + "/config";
            String stateTopic = topicPrefix + "/status/led/" + i;
            String ledCmdTopic = topicPrefix + "/cmd/led/" + i;

            JsonDocument doc;
            doc["name"]          = ledNames[i];
            doc["unique_id"]     = uid;
            doc["schema"]        = "json";
            doc["state_topic"]   = stateTopic;
            doc["command_topic"] = ledCmdTopic;
            doc["supported_color_modes"][0] = "rgb";

            JsonObject dev        = doc["device"].to<JsonObject>();
            dev["identifiers"][0] = clientId;
            dev["name"]           = "Semaphore";
            dev["model"]          = "ESP32-C3";

            String payload;
            serializeJson(doc, payload);
            mqttClient.publish(discTopic.c_str(), payload.c_str(), true);
        }

        struct SwitchDef { const char *name; const char *id; const char *cmdOn; const char *cmdOff; };
        SwitchDef switches[] = {
            {"Cycle",      "cycle",   "{\"type\":\"setCycle\",\"cycle\":true}",    "{\"type\":\"setCycle\",\"cycle\":false}"},
            {"Party mode", "party",   "{\"type\":\"setParty\",\"party\":true}",    "{\"type\":\"setParty\",\"party\":false}"},
            {"Rainbow",    "rainbow", "{\"type\":\"setRainbow\",\"rainbow\":true}", "{\"type\":\"setRainbow\",\"rainbow\":false}"},
        };

        for (auto &sw : switches)
        {
            String uid        = clientId + "_" + sw.id;
            String discTopic  = "homeassistant/switch/" + uid + "/config";
            String stateTopic = topicPrefix + "/status/switch/" + sw.id;

            JsonDocument doc;
            doc["name"]          = sw.name;
            doc["unique_id"]     = uid;
            doc["state_topic"]   = stateTopic;
            doc["command_topic"] = cmdTopic;
            doc["payload_on"]    = sw.cmdOn;
            doc["payload_off"]   = sw.cmdOff;
            doc["state_on"]      = "ON";
            doc["state_off"]     = "OFF";

            JsonObject dev        = doc["device"].to<JsonObject>();
            dev["identifiers"][0] = clientId;
            dev["name"]           = "Semaphore";
            dev["model"]          = "ESP32-C3";

            String payload;
            serializeJson(doc, payload);
            mqttClient.publish(discTopic.c_str(), payload.c_str(), true);
        }

        {
            String uid   = clientId + "_random_yn";
            String topic = "homeassistant/button/" + uid + "/config";

            JsonDocument doc;
            doc["name"]          = "Random Yes/No";
            doc["unique_id"]     = uid;
            doc["command_topic"] = cmdTopic;
            doc["payload_press"] = "{\"type\":\"randomYesNo\"}";

            JsonObject dev        = doc["device"].to<JsonObject>();
            dev["identifiers"][0] = clientId;
            dev["name"]           = "Semaphore";
            dev["model"]          = "ESP32-C3";

            String payload;
            serializeJson(doc, payload);
            mqttClient.publish(topic.c_str(), payload.c_str(), true);
        }

        Serial.println("MQTT: HA discovery published");
    }

    void applyConfig(const String &newBroker, int newPort, const String &newUsername,
                     const String &newPassword, const String &newClientId, const String &newTopic,
                     bool newEnabled)
    {
        enabled     = newEnabled;
        broker      = newBroker;
        port        = newPort;
        username    = newUsername;
        password    = newPassword;
        clientId    = newClientId;
        topicPrefix = newTopic;
        cmdTopic    = topicPrefix + "/cmd";
        statusTopic = topicPrefix + "/status";
        if (!enabled)
        {
            mqttClient.disconnect();
        }
        else if (!broker.isEmpty())
        {
            mqttClient.setServer(broker.c_str(), port);
            mqttClient.disconnect();
            lastReconnectAttempt = 0;
        }
    }

    bool getEnabled() { return enabled; }
};

MQTTController *MQTTController::_instance = nullptr;
