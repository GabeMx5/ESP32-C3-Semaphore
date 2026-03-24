#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include "wifiConfigManager.h"
#include "ledController.h"
#include "networkManager.h"
#include "monitorController.h"
#include "mqttController.h"
#include "timerController.h"
#include "configController.h"

AsyncWebServer webServer(80);
AsyncWebSocket ws("/ws");
WiFiConfigManager wifiManager;
LEDController ledController;
NetworkManager networkManager;
MonitorController monitorController;
MQTTController mqttController;
TimerController timerController;
ConfigController configController;

// ─── Broadcast ────────────────────────────────────────────────────────────────

void broadcastLedStatus()
{
    JsonDocument doc;
    doc["type"] = "ledStatus";
    JsonArray leds = doc["leds"].to<JsonArray>();
    for (int i = 0; i < LED_COUNT; i++)
    {
        JsonObject led = leds.add<JsonObject>();
        uint32_t color = ledController.getLEDColor(i);
        led["r"]       = (color >> 16) & 0xFF;
        led["g"]       = (color >> 8) & 0xFF;
        led["b"]       = color & 0xFF;
        led["on"]      = ledController.getLEDOn(i);
        led["blink"]   = ledController.getLEDBlink(i);
    }
    String msg;
    serializeJson(doc, msg);
    ws.textAll(msg);
    mqttController.publish(msg);
    for (int i = 0; i < LED_COUNT; i++)
    {
        uint32_t color = ledController.getLEDColor(i);
        mqttController.publishLedState(i,
            (color >> 16) & 0xFF,
            (color >> 8)  & 0xFF,
            color & 0xFF,
            ledController.getLEDOn(i));
    }
}

void broadcastCycleStatus()
{
    JsonDocument doc;
    doc["type"]          = "cycleStatus";
    doc["cycle"]         = ledController.getCycleEnabled();
    doc["topLedTime"]    = ledController.getTopLedTime();
    doc["middleLedTime"] = ledController.getMiddleLedTime();
    doc["bottomLedTime"] = ledController.getBottomLedTime();
    String msg;
    serializeJson(doc, msg);
    ws.textAll(msg);
    mqttController.publish(msg);
    mqttController.publishSwitchState("cycle", ledController.getCycleEnabled());
}

void broadcastPartyStatus()
{
    JsonDocument doc;
    doc["type"]         = "partyStatus";
    doc["party"]        = ledController.getPartyEnabled();
    doc["partyMadness"] = ledController.getPartyMadness();
    String msg;
    serializeJson(doc, msg);
    ws.textAll(msg);
    mqttController.publish(msg);
    mqttController.publishSwitchState("party", ledController.getPartyEnabled());
}

void broadcastRainbowStatus()
{
    JsonDocument doc;
    doc["type"]             = "rainbowStatus";
    doc["rainbow"]          = ledController.getRainbowEnabled();
    doc["rainbowCycleTime"] = ledController.getRainbowCycleTime();
    String msg;
    serializeJson(doc, msg);
    ws.textAll(msg);
    mqttController.publish(msg);
    mqttController.publishSwitchState("rainbow", ledController.getRainbowEnabled());
}

void broadcastConfigStatus()
{
    JsonDocument doc;
    doc["type"]                  = "configStatus";
    doc["makeChangesPersistent"] = configController.getMakeChangesPersistent();
    String msg;
    serializeJson(doc, msg);
    ws.textAll(msg);
}

void sendMqttConfig(AsyncWebSocketClient *client)
{
    JsonDocument doc;
    doc["type"]      = "mqttConfig";
    doc["broker"]    = mqttController.getBroker();
    doc["port"]      = mqttController.getPort();
    doc["username"]  = mqttController.getUsername();
    doc["password"]  = mqttController.getPassword();
    doc["clientId"]  = mqttController.getClientId();
    doc["topic"]     = mqttController.getTopicPrefix();
    doc["enabled"]   = mqttController.getEnabled();
    doc["connected"] = mqttController.isConnected();
    String response;
    serializeJson(doc, response);
    client->text(response);
}

void sendWifiConfig(AsyncWebSocketClient *client)
{
    JsonDocument doc;
    doc["type"]       = "wifiConfig";
    doc["deviceName"] = wifiManager.deviceName;
    doc["ntpServer"]  = wifiManager.ntpServer;
    doc["timezone"]   = wifiManager.timezone;
    doc["ssid"]       = wifiManager.wifiSSID;
    doc["password"]   = wifiManager.wifiPassword;
    doc["dhcp"]     = wifiManager.dhcp;
    if (!wifiManager.dhcp)
    {
        doc["ip"]      = wifiManager.localIP.toString();
        doc["subnet"]  = wifiManager.subnet.toString();
        doc["gateway"] = wifiManager.gateway.toString();
        doc["dns"]     = wifiManager.dns.toString();
    }
    String response;
    serializeJson(doc, response);
    client->text(response);
}

void sendSysInfo(AsyncWebSocketClient *client)
{
    JsonDocument doc;
    doc["type"]          = "sysInfo";
    doc["ip"]            = WiFi.localIP().toString();
    doc["ssid"]          = WiFi.SSID();
    doc["rssi"]          = WiFi.RSSI();
    doc["freeHeap"]      = ESP.getFreeHeap();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        doc["datetime"] = buf;
    }
    doc["uptime"]        = millis() / 1000;
    doc["mqttConnected"] = mqttController.isConnected();
    doc["mqttBroker"]    = mqttController.getBroker();
    doc["mac"]           = WiFi.macAddress();
    doc["cpuFreq"]       = ESP.getCpuFreqMHz();
    doc["chipModel"]     = ESP.getChipModel();
    doc["chipRevision"]  = ESP.getChipRevision();
    doc["wifiChannel"]   = WiFi.channel();
    String response;
    serializeJson(doc, response);
    client->text(response);
}

// ─── Command processing (shared between WebSocket and MQTT) ──────────────────

void processCommand(JsonDocument &doc)
{
    const char *type = doc["type"];
    if (!type) return;

    if (strcmp(type, "setLed") == 0)
    {
        int      ledIndex = doc["led"];
        uint32_t existing = ledController.getLEDColor(ledIndex);
        int er = (existing >> 16) & 0xFF;
        int eg = (existing >> 8)  & 0xFF;
        int eb = existing & 0xFF;
        int red, green, blue;
        if (!doc["brightness"].isNull() && doc["r"].isNull())
        {
            int br   = doc["brightness"];
            int maxC = max({er, eg, eb});
            red   = maxC > 0 ? er * br / maxC : 0;
            green = maxC > 0 ? eg * br / maxC : 0;
            blue  = maxC > 0 ? eb * br / maxC : 0;
        }
        else
        {
            red   = doc["r"].isNull() ? er : (int)doc["r"];
            green = doc["g"].isNull() ? eg : (int)doc["g"];
            blue  = doc["b"].isNull() ? eb : (int)doc["b"];
        }
        bool on    = doc["on"]    | false;
        bool blink = doc["blink"] | false;
        monitorController.displayMessage("LED " + String(ledIndex) + ": \n" +
                                         "R:" + String(red) + "\n" +
                                         "G:" + String(green) + "\n" +
                                         "B:" + String(blue));
        ledController.setLED(ledIndex, red, green, blue, on, blink);
        configController.markDirty();
        broadcastLedStatus();
    }
    else if (strcmp(type, "getLed") == 0)
    {
        broadcastLedStatus();
    }
    else if (strcmp(type, "setWifi") == 0)
    {
        String newName     = doc["deviceName"] | wifiManager.deviceName.c_str();
        String newNtp      = doc["ntpServer"]  | wifiManager.ntpServer.c_str();
        String newTz       = doc["timezone"]   | wifiManager.timezone.c_str();
        String newSSID     = doc["ssid"]     | "";
        String newPassword = doc["password"] | "";
        bool dhcpMode      = doc["dhcp"]     | true;
        String ipStr      = doc["ip"]      | "";
        String subnetStr  = doc["subnet"]  | "";
        String gatewayStr = doc["gateway"] | "";
        String dnsStr     = doc["dns"]     | "";
        IPAddress localIP, subnet, gateway, dns;
        if (!dhcpMode &&
            (!localIP.fromString(ipStr) ||
             !subnet.fromString(subnetStr) ||
             !gateway.fromString(gatewayStr) ||
             !dns.fromString(dnsStr)))
        {
            mqttController.publish("{\"type\":\"status\",\"status\":\"error\",\"message\":\"Invalid IP\"}");
            return;
        }
        wifiManager.saveConfig(newName, newNtp, newTz, newSSID, newPassword, dhcpMode, localIP, subnet, gateway, dns);
        mqttController.publish("{\"type\":\"status\",\"status\":\"saved\",\"reboot\":true}");
        delay(1000);
        ESP.restart();
    }
    else if (strcmp(type, "getWifi") == 0)
    {
        JsonDocument resp;
        resp["type"]       = "wifiConfig";
        resp["deviceName"] = wifiManager.deviceName;
        resp["ntpServer"]  = wifiManager.ntpServer;
        resp["timezone"]   = wifiManager.timezone;
        resp["ssid"]       = wifiManager.wifiSSID;
        resp["password"]   = wifiManager.wifiPassword;
        resp["dhcp"]     = wifiManager.dhcp;
        if (!wifiManager.dhcp)
        {
            resp["ip"]      = wifiManager.localIP.toString();
            resp["subnet"]  = wifiManager.subnet.toString();
            resp["gateway"] = wifiManager.gateway.toString();
            resp["dns"]     = wifiManager.dns.toString();
        }
        String msg;
        serializeJson(resp, msg);
        mqttController.publish(msg);
    }
    else if (strcmp(type, "setCycle") == 0)
    {
        ledController.setCycle(
            doc["cycle"]         | false,
            doc["topLedTime"]    | ledController.getTopLedTime(),
            doc["middleLedTime"] | ledController.getMiddleLedTime(),
            doc["bottomLedTime"] | ledController.getBottomLedTime()
        );
        configController.markDirty();
        broadcastCycleStatus();
    }
    else if (strcmp(type, "setParty") == 0)
    {
        ledController.setParty(doc["party"] | false, doc["partyMadness"] | -1);
        configController.markDirty();
        broadcastPartyStatus();
        broadcastCycleStatus();
        broadcastRainbowStatus();
    }
    else if (strcmp(type, "setRainbow") == 0)
    {
        ledController.setRainbow(
            doc["rainbow"]          | false,
            doc["rainbowCycleTime"] | ledController.getRainbowCycleTime()
        );
        configController.markDirty();
        broadcastRainbowStatus();
        broadcastCycleStatus();
        broadcastPartyStatus();
    }
    else if (strcmp(type, "randomYesNo") == 0)
    {
        ledController.startRandomYesNo();
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    }
    else if (strcmp(type, "morse") == 0)
    {
        ledController.startMorse(doc["text"] | "SOS");
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    }
    else if (strcmp(type, "setConfig") == 0)
    {
        configController.setMakeChangesPersistent(doc["makeChangesPersistent"] | true);
        broadcastConfigStatus();
    }
    else if (strcmp(type, "getInfo") == 0)
    {
        JsonDocument resp;
        resp["type"]          = "sysInfo";
        resp["ip"]            = WiFi.localIP().toString();
        resp["ssid"]          = WiFi.SSID();
        resp["rssi"]          = WiFi.RSSI();
        resp["freeHeap"]      = ESP.getFreeHeap();
        resp["uptime"]        = millis() / 1000;
        resp["mqttConnected"] = mqttController.isConnected();
        resp["mqttBroker"]    = mqttController.getBroker();
        String msg;
        serializeJson(resp, msg);
        mqttController.publish(msg);
    }
    else if (strcmp(type, "getMqtt") == 0)
    {
        JsonDocument resp;
        resp["type"]      = "mqttConfig";
        resp["broker"]    = mqttController.getBroker();
        resp["port"]      = mqttController.getPort();
        resp["username"]  = mqttController.getUsername();
        resp["password"]  = mqttController.getPassword();
        resp["clientId"]  = mqttController.getClientId();
        resp["topic"]     = mqttController.getTopicPrefix();
        resp["enabled"]   = mqttController.getEnabled();
        resp["connected"] = mqttController.isConnected();
        String msg;
        serializeJson(resp, msg);
        mqttController.publish(msg);
    }
    else if (strcmp(type, "setMqtt") == 0)
    {
        mqttController.applyConfig(
            doc["broker"]   | "",
            doc["port"]     | 1883,
            doc["username"] | "",
            doc["password"] | "",
            doc["clientId"] | "semaphore",
            doc["topic"]    | "semaphore",
            doc["enabled"]  | false
        );
        mqttController.saveConfig();
        mqttController.publish("{\"type\":\"status\",\"status\":\"saved\",\"message\":\"MQTT config saved\"}");
    }
}

// ─── WebSocket ────────────────────────────────────────────────────────────────

void handleWebSocketMessage(AsyncWebSocketClient *client, uint8_t *data, size_t len)
{
    JsonDocument doc;
    if (deserializeJson(doc, data, len))
    {
        client->text("{\"type\":\"status\",\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    const char *type = doc["type"];

    if (strcmp(type, "ping") == 0)
    {
        client->text("{\"type\":\"pong\"}");
        return;
    }

    if (strcmp(type, "startGuess") == 0)
    {
        ledController.startGuess(doc["led"] | 0);
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
        return;
    }

    // setWifi needs client-specific response before restart
    if (strcmp(type, "setWifi") == 0)
    {
        String newName     = doc["deviceName"] | wifiManager.deviceName.c_str();
        String newNtp      = doc["ntpServer"]  | wifiManager.ntpServer.c_str();
        String newTz       = doc["timezone"]   | wifiManager.timezone.c_str();
        String newSSID     = doc["ssid"]     | "";
        String newPassword = doc["password"] | "";
        bool dhcpMode      = doc["dhcp"]     | true;
        String ipStr      = doc["ip"]      | "";
        String subnetStr  = doc["subnet"]  | "";
        String gatewayStr = doc["gateway"] | "";
        String dnsStr     = doc["dns"]     | "";
        IPAddress localIP, subnet, gateway, dns;
        if (!dhcpMode &&
            (!localIP.fromString(ipStr) ||
             !subnet.fromString(subnetStr) ||
             !gateway.fromString(gatewayStr) ||
             !dns.fromString(dnsStr)))
        {
            client->text("{\"type\":\"status\",\"status\":\"error\",\"message\":\"Invalid IP configuration\"}");
            return;
        }
        wifiManager.saveConfig(newName, newNtp, newTz, newSSID, newPassword, dhcpMode, localIP, subnet, gateway, dns);
        client->text("{\"type\":\"status\",\"status\":\"saved\",\"reboot\":true}");
        delay(1000);
        ESP.restart();
        return;
    }

    // getWifi needs client-specific response
    if (strcmp(type, "getWifi") == 0)
    {
        sendWifiConfig(client);
        return;
    }

    // getInfo needs client-specific response
    if (strcmp(type, "getInfo") == 0)
    {
        sendSysInfo(client);
        return;
    }

    // getMqtt needs client-specific response
    if (strcmp(type, "getMqtt") == 0)
    {
        sendMqttConfig(client);
        return;
    }

    // setMqtt needs client-specific response
    if (strcmp(type, "setMqtt") == 0)
    {
        mqttController.applyConfig(
            doc["broker"]   | "",
            doc["port"]     | 1883,
            doc["username"] | "",
            doc["password"] | "",
            doc["clientId"] | "semaphore",
            doc["topic"]    | "semaphore",
            doc["enabled"]  | false
        );
        mqttController.saveConfig();
        client->text("{\"type\":\"status\",\"status\":\"saved\",\"message\":\"MQTT config saved\"}");
        return;
    }

    // getTimers / setTimers need client-specific response
    if (strcmp(type, "getTimers") == 0)
    {
        timerController.sendTimers(client);
        return;
    }

    if (strcmp(type, "setTimers") == 0)
    {
        timerController.setTimers(doc["timers"].as<JsonArray>());
        client->text("{\"type\":\"status\",\"status\":\"saved\",\"message\":\"Timers saved\"}");
        return;
    }

    processCommand(doc);
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            broadcastLedStatus();
            broadcastCycleStatus();
            broadcastPartyStatus();
            broadcastRainbowStatus();
            sendWifiConfig(client);
            sendMqttConfig(client);
            timerController.sendTimers(client);
            broadcastConfigStatus();
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected from %s\n", client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(client, data, len);
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

// ─── MQTT ────────────────────────────────────────────────────────────────────

void onMQTTMessage(uint8_t *payload, unsigned int length)
{
    JsonDocument doc;
    if (deserializeJson(doc, payload, length))
    {
        mqttController.publish("{\"type\":\"status\",\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }
    processCommand(doc);
}

// ─── Web server ───────────────────────────────────────────────────────────────

void setupWebServer()
{
    ws.onEvent(onWebSocketEvent);
    webServer.addHandler(&ws);
    webServer.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "pong");
    });
    webServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    webServer.begin();
    monitorController.displayMessage("IP:\n" + WiFi.localIP().toString());
}

// ─── Setup / Loop ─────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    delay(200);
    monitorController.begin();
    monitorController.displayMessage("Startup...");
    monitorController.displayMessage("Connecting to\nWiFi...");
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS mount failed");
        return;
    }
    ledController.begin();
    configController.begin(ledController);
    timerController.begin();
    timerController.onAllOff = []() {
        for (int i = 0; i < LED_COUNT; i++) {
            uint32_t c = ledController.getLEDColor(i);
            ledController.setLED(i, (c>>16)&0xFF, (c>>8)&0xFF, c&0xFF, false, false);
        }
        ledController.setCycle(false, ledController.getTopLedTime(), ledController.getMiddleLedTime(), ledController.getBottomLedTime());
        ledController.setParty(false, ledController.getPartyMadness());
        ledController.setRainbow(false, ledController.getRainbowCycleTime());
        broadcastLedStatus();
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    };
    timerController.onLed = [](int idx, uint8_t r, uint8_t g, uint8_t b) {
        ledController.setCycle(false, ledController.getTopLedTime(), ledController.getMiddleLedTime(), ledController.getBottomLedTime());
        ledController.setParty(false, ledController.getPartyMadness());
        ledController.setRainbow(false, ledController.getRainbowCycleTime());
        ledController.setLED(idx, r, g, b, true, false);
        broadcastLedStatus();
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    };
    timerController.onCycle = [](bool on) {
        ledController.setCycle(on, ledController.getTopLedTime(), ledController.getMiddleLedTime(), ledController.getBottomLedTime());
        if (on) {
            ledController.setParty(false, ledController.getPartyMadness());
            ledController.setRainbow(false, ledController.getRainbowCycleTime());
        }
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    };
    timerController.onParty = [](bool on) {
        ledController.setParty(on, ledController.getPartyMadness());
        if (on) {
            ledController.setCycle(false, ledController.getTopLedTime(), ledController.getMiddleLedTime(), ledController.getBottomLedTime());
            ledController.setRainbow(false, ledController.getRainbowCycleTime());
        }
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    };
    timerController.onRainbow = [](bool on) {
        ledController.setRainbow(on, ledController.getRainbowCycleTime());
        if (on) {
            ledController.setCycle(false, ledController.getTopLedTime(), ledController.getMiddleLedTime(), ledController.getBottomLedTime());
            ledController.setParty(false, ledController.getPartyMadness());
        }
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    };
    ledController.onGuessResult = [](bool win, int led) {
        JsonDocument doc;
        doc["type"] = "guessResult";
        doc["win"]  = win;
        doc["led"]  = led;
        String msg;
        serializeJson(doc, msg);
        ws.textAll(msg);
        broadcastLedStatus();
    };
    networkManager.begin(wifiManager);
    configTzTime(wifiManager.timezone.c_str(), wifiManager.ntpServer.c_str());
    MDNS.begin(wifiManager.deviceName.c_str());
    setupWebServer();
    mqttController.connectedHandler = []()
    {
        broadcastLedStatus();
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    };
    mqttController.begin(onMQTTMessage);
    ArduinoOTA
        .onStart([]() { Serial.println("OTA Start"); })
        .onEnd([]() { Serial.println("\nOTA End"); })
        .onProgress([](unsigned int progress, unsigned int total)
                    { Serial.printf("Progress: %u%%\r", (progress * 100) / total); })
        .onError([](ota_error_t error)
                 { Serial.printf("Error[%u]\n", error); });
    ArduinoOTA.begin();
}

void loop()
{
    ArduinoOTA.handle();
    networkManager.handleFallbackLogic();
    monitorController.loop();
    ledController.update();
    timerController.loop();
    ws.cleanupClients();
    mqttController.loop();
    configController.loop();
}
