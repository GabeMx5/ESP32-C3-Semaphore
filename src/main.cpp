#define FIRMWARE_VERSION "1.1.0"

#include "teeSerial.h"
TeeSerial teeSerial;
#include <vector>
#include <set>
#include <esp_log.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_ota_ops.h>
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
#include "geoController.h"
#include "otaController.h"
#include "serialConsole.h"
#include "alexaController.h"
#include "bambulabController.h"
#ifdef IMPROV_ENABLED
#include "improvController.h"
#endif

AsyncWebServer webServer(80);
AsyncWebSocket ws("/ws");
static std::set<uint32_t> _consoleViewerIds;  // client IDs currently on the console tab
static std::set<uint32_t> _infoViewerIds;     // client IDs currently on the info or settings tab
static std::set<uint32_t> _bambuViewerIds;    // client IDs currently on the bambu tab
WiFiConfigManager wifiManager;
LEDController ledController;
NetworkManager networkManager;
MonitorController monitorController;
MQTTController mqttController;
TimerController timerController;
ConfigController configController;
GeoController    geoController;
OTAController    otaController;
SerialConsole    serialConsole(wifiManager, mqttController, FIRMWARE_VERSION);
AlexaController  alexaController;
BambuLabController bambuController;

void weatherTempToRgb(float temp, uint8_t& outR, uint8_t& outG, uint8_t& outB);
void conditionToRgb(WeatherCondition cond, bool isDay, uint8_t& r, uint8_t& g, uint8_t& b);
void humidityToRgb(float humidity, uint8_t& r, uint8_t& g, uint8_t& b);
static void aqValueToRgb(float v, float tGood, float tModerate, float tPoor, uint8_t& r, uint8_t& g, uint8_t& b);

static const char* conditionToString(WeatherCondition c)
{
    switch (c) {
        case WeatherCondition::CLEAR:         return "Clear";
        case WeatherCondition::PARTLY_CLOUDY: return "Partly Cloudy";
        case WeatherCondition::FOGGY:         return "Foggy";
        case WeatherCondition::DRIZZLE:       return "Drizzle";
        case WeatherCondition::RAINY:         return "Rainy";
        case WeatherCondition::SNOWY:         return "Snowy";
        case WeatherCondition::STORMY:        return "Stormy";
        default:                              return "Unknown";
    }
}

// ─── Broadcast ────────────────────────────────────────────────────────────────

static String _buildLedStatus()
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
    String msg; serializeJson(doc, msg); return msg;
}

void sendLedStatus(AsyncWebSocketClient *client) { client->text(_buildLedStatus()); }

void broadcastLedStatus()
{
    String msg = _buildLedStatus();
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

static String _buildCycleStatus()
{
    JsonDocument doc;
    doc["type"]          = "cycleStatus";
    doc["cycle"]         = ledController.getCycleEnabled();
    doc["topLedTime"]    = ledController.getTopLedTime();
    doc["middleLedTime"] = ledController.getMiddleLedTime();
    doc["bottomLedTime"] = ledController.getBottomLedTime();
    String msg; serializeJson(doc, msg); return msg;
}

void sendCycleStatus(AsyncWebSocketClient *client) { client->text(_buildCycleStatus()); }

void broadcastCycleStatus()
{
    String msg = _buildCycleStatus();
    ws.textAll(msg);
    mqttController.publish(msg);
    mqttController.publishSwitchState("cycle", ledController.getCycleEnabled());
}

static String _buildPartyStatus()
{
    JsonDocument doc;
    doc["type"]         = "partyStatus";
    doc["party"]        = ledController.getPartyEnabled();
    doc["partyMadness"] = ledController.getPartyMadness();
    String msg; serializeJson(doc, msg); return msg;
}

void sendPartyStatus(AsyncWebSocketClient *client) { client->text(_buildPartyStatus()); }

void broadcastPartyStatus()
{
    String msg = _buildPartyStatus();
    ws.textAll(msg);
    mqttController.publish(msg);
    mqttController.publishSwitchState("party", ledController.getPartyEnabled());
}

static String _buildRainbowStatus()
{
    JsonDocument doc;
    doc["type"]             = "rainbowStatus";
    doc["rainbow"]          = ledController.getRainbowEnabled();
    doc["rainbowCycleTime"] = ledController.getRainbowCycleTime();
    String msg; serializeJson(doc, msg); return msg;
}

void sendRainbowStatus(AsyncWebSocketClient *client) { client->text(_buildRainbowStatus()); }

void broadcastRainbowStatus()
{
    String msg = _buildRainbowStatus();
    ws.textAll(msg);
    mqttController.publish(msg);
    mqttController.publishSwitchState("rainbow", ledController.getRainbowEnabled());
}

static String _buildConfigStatus()
{
    JsonDocument doc;
    doc["type"]                  = "configStatus";
    doc["makeChangesPersistent"] = configController.getMakeChangesPersistent();
    doc["latitude"]              = configController.getLatitude();
    doc["longitude"]             = configController.getLongitude();
    String msg; serializeJson(doc, msg); return msg;
}

void sendConfigStatus(AsyncWebSocketClient *client) { client->text(_buildConfigStatus()); }

void broadcastConfigStatus()
{
    ws.textAll(_buildConfigStatus());
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

void sendBambuConfig(AsyncWebSocketClient *client)
{
    JsonDocument doc;
    doc["type"]           = "bambuConfig";
    doc["ip"]             = bambuController.getIp();
    doc["serial"]         = bambuController.getSerial();
    doc["accessCode"]     = bambuController.getAccessCode();
    doc["enabled"]        = bambuController.getEnabled();
    doc["connected"]      = bambuController.isConnected();
    doc["state"]          = BambuLabController::stateToString(bambuController.getState());
    doc["bambuMode"]      = bambuController.getBambuMode();
    doc["idleTimeoutMin"] = bambuController.getIdleTimeoutMin();
    bambuController.addStateColorsToJson(doc);
    String response;
    serializeJson(doc, response);
    client->text(response);
}

static void applyBambuLedState(BambuState s)
{
    static constexpr unsigned long LONG_MS = 3600000UL * 24;
    uint8_t r0, g0, b0, r1, g1, b1, r2, g2, b2;
    bambuController.getStateColor(s, 0, r0, g0, b0);
    bambuController.getStateColor(s, 1, r1, g1, b1);
    bambuController.getStateColor(s, 2, r2, g2, b2);
    if (!r0 && !g0 && !b0 && !r1 && !g1 && !b1 && !r2 && !g2 && !b2)
        ledController.cancelOverlay();
    else
        ledController.showOverlay(r0, g0, b0, r1, g1, b1, r2, g2, b2, LONG_MS);
}

// Disables BambuLab printer mode and notifies all WS clients.
// Called whenever any other effect is activated.
static void cancelBambuMode()
{
    if (!bambuController.getBambuMode()) return;
    bambuController.setBambuMode(false);
    configController.setBambuMode(false);
    bambuController.resetIdle();
    ledController.cancelOverlay();
    JsonDocument doc;
    doc["type"]      = "bambuConfig";
    doc["bambuMode"] = false;
    String msg; serializeJson(doc, msg);
    ws.textAll(msg);
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
    doc["dhcp"]       = wifiManager.dhcp;
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

// Dynamic fields — sent every second when client is on the info tab
void sendSysInfo(AsyncWebSocketClient *client)
{
    JsonDocument doc;
    doc["type"]          = "sysInfo";
    doc["rssi"]          = WiFi.RSSI();
    doc["freeHeap"]      = ESP.getFreeHeap();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        doc["datetime"] = buf;
    }
    doc["uptime"]         = millis() / 1000;
    doc["bambuConnected"] = bambuController.isConnected();
    doc["mqttConnected"]  = mqttController.isConnected();
    String response;
    serializeJson(doc, response);
    client->text(response);
}

// Static fields — sent once at connect and when weather/AQ data updates
static String _buildSysInfoStatic()
{
    JsonDocument doc;
    doc["type"]         = "sysInfoStatic";
    doc["version"]      = FIRMWARE_VERSION;
    doc["ip"]           = WiFi.localIP().toString();
    doc["ssid"]         = WiFi.SSID();
    doc["mac"]          = WiFi.macAddress();
    doc["cpuFreq"]      = ESP.getCpuFreqMHz();
    doc["chipModel"]    = ESP.getChipModel();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["wifiChannel"]  = WiFi.channel();
    doc["mqttBroker"]   = mqttController.getBroker();
    doc["latitude"]     = configController.getLatitude();
    doc["longitude"]    = configController.getLongitude();
    if (geoController.weather.valid) {
        doc["weatherCode"]      = geoController.weather.weatherCode;
        doc["weatherTemp"]      = geoController.weather.temperature;
        doc["weatherHumidity"]  = geoController.weather.humidity;
        doc["weatherCondition"] = (int)geoController.weather.condition;
        uint8_t wr, wg, wb;
        weatherTempToRgb(geoController.weather.temperature, wr, wg, wb);
        doc["temperatureR"] = wr; doc["temperatureG"] = wg; doc["temperatureB"] = wb;
        uint8_t cr, cg, cb;
        conditionToRgb(geoController.weather.condition, geoController.weather.isDay, cr, cg, cb);
        doc["conditionR"] = cr; doc["conditionG"] = cg; doc["conditionB"] = cb;
        uint8_t hr, hg, hb;
        humidityToRgb(geoController.weather.humidity, hr, hg, hb);
        doc["humidityR"] = hr; doc["humidityG"] = hg; doc["humidityB"] = hb;
    }
    if (geoController.airQuality.valid) {
        doc["aqPm25"] = geoController.airQuality.pm2_5;
        doc["aqPm10"] = geoController.airQuality.pm10;
        doc["aqNo2"]  = geoController.airQuality.no2;
        uint8_t aqr, aqg, aqb;
        aqValueToRgb(geoController.airQuality.pm2_5, 12,  35,  55,  aqr, aqg, aqb);
        doc["aqPm25R"] = aqr; doc["aqPm25G"] = aqg; doc["aqPm25B"] = aqb;
        aqValueToRgb(geoController.airQuality.pm10,  20,  40,  140, aqr, aqg, aqb);
        doc["aqPm10R"] = aqr; doc["aqPm10G"] = aqg; doc["aqPm10B"] = aqb;
        aqValueToRgb(geoController.airQuality.no2,   40,  100, 200, aqr, aqg, aqb);
        doc["aqNo2R"]  = aqr; doc["aqNo2G"]  = aqg; doc["aqNo2B"]  = aqb;
    }
    String msg; serializeJson(doc, msg); return msg;
}

void sendSysInfoStatic(AsyncWebSocketClient *client) { client->text(_buildSysInfoStatic()); }

void broadcastSysInfoStatic() { ws.textAll(_buildSysInfoStatic()); }

// Send a message to clients on the bambu or info tab only
static void _sendToBambuAndInfoViewers(const String& msg)
{
    for (uint32_t id : _bambuViewerIds) {
        AsyncWebSocketClient* c = ws.client(id);
        if (c) c->text(msg);
    }
    for (uint32_t id : _infoViewerIds) {
        AsyncWebSocketClient* c = ws.client(id);
        if (c) c->text(msg);
    }
}

void weatherTempToRgb(float temp, uint8_t& outR, uint8_t& outG, uint8_t& outB)
{
    float t     = max(5.0f, min(30.0f, temp));
    float ratio = (t - 5.0f) / 25.0f;
    float hue   = 210.0f * (1.0f - ratio);
    float h = hue / 60.0f;
    float c = 1.0f;
    float x = c * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if      (h < 1) { r = c; g = x; }
    else if (h < 2) { r = x; g = c; }
    else if (h < 3) { g = c; b = x; }
    else if (h < 4) { g = x; b = c; }
    else if (h < 5) { r = x; b = c; }
    else            { r = c; b = x; }
    outR = (uint8_t)(r * 255);
    outG = (uint8_t)(g * 255);
    outB = (uint8_t)(b * 255);
}

void conditionToRgb(WeatherCondition cond, bool isDay, uint8_t& r, uint8_t& g, uint8_t& b)
{
    switch (cond) {
        case WeatherCondition::CLEAR:
            if (isDay) { r=255; g=200; b=0;   } // golden yellow
            else       { r=0;   g=20;  b=150; } // dark blue
            break;
        case WeatherCondition::PARTLY_CLOUDY: r=150; g=170; b=200; break; // steel blue
        case WeatherCondition::FOGGY:         r=160; g=160; b=160; break; // gray
        case WeatherCondition::DRIZZLE:       r=80;  g=140; b=255; break; // light blue
        case WeatherCondition::RAINY:         r=0;   g=60;  b=220; break; // blue
        case WeatherCondition::SNOWY:         r=200; g=240; b=255; break; // ice white
        case WeatherCondition::STORMY:        r=140; g=0;   b=200; break; // purple
        default:                              r=80;  g=80;  b=80;  break; // dim gray
    }
}

void humidityToRgb(float humidity, uint8_t& r, uint8_t& g, uint8_t& b)
{
    float h = max(0.0f, min(100.0f, humidity)) / 100.0f;
    // 0% = yellow (255,200,0), 50% = green (0,200,80), 100% = blue (0,60,220)
    if (h < 0.5f) {
        float t = h * 2.0f;
        r = (uint8_t)(255 * (1.0f - t));
        g = (uint8_t)(200 * (1.0f - t) + 200 * t);
        b = (uint8_t)(80  * t);
    } else {
        float t = (h - 0.5f) * 2.0f;
        r = 0;
        g = (uint8_t)(200 * (1.0f - t));
        b = (uint8_t)(80  * (1.0f - t) + 220 * t);
    }
}

// Maps a pollutant value to RGB using AQI-style thresholds:
// good→green, moderate→yellow, poor→orange, very poor→red, hazardous→purple
static void aqValueToRgb(float v, float tGood, float tModerate, float tPoor,
                          uint8_t& r, uint8_t& g, uint8_t& b)
{
    if (v < tGood)     { r=0;   g=200; b=0;   return; } // green
    if (v < tModerate) { r=220; g=220; b=0;   return; } // yellow
    if (v < tPoor)     { r=255; g=100; b=0;   return; } // orange
    if (v < tPoor*2)   { r=255; g=0;   b=0;   return; } // red
                         r=160; g=0;   b=160;            // purple
}

void applyAirQualityColor()
{
    if (!geoController.airQuality.valid) return;
    uint8_t r2, g2, b2; // top    — PM2.5  (thresholds: 12 / 35 / 55)
    uint8_t r1, g1, b1; // middle — PM10   (thresholds: 20 / 40 / 140)
    uint8_t r0, g0, b0; // bottom — NO₂    (thresholds: 40 / 100 / 200)
    aqValueToRgb(geoController.airQuality.pm2_5, 12,  35,  55,  r2, g2, b2);
    aqValueToRgb(geoController.airQuality.pm10,  20,  40,  140, r1, g1, b1);
    aqValueToRgb(geoController.airQuality.no2,   40,  100, 200, r0, g0, b0);
    ledController.showOverlay(r0, g0, b0, r1, g1, b1, r2, g2, b2);
}

void applyWeatherColor()
{
    if (!geoController.weather.valid) return;
    uint8_t r2, g2, b2; // top    — condition
    uint8_t r1, g1, b1; // middle — temperature
    uint8_t r0, g0, b0; // bottom — humidity
    conditionToRgb(geoController.weather.condition, geoController.weather.isDay, r2, g2, b2);
    weatherTempToRgb(geoController.weather.temperature, r1, g1, b1);
    humidityToRgb(geoController.weather.humidity, r0, g0, b0);
    ledController.showOverlay(r0, g0, b0, r1, g1, b1, r2, g2, b2);
}

// ─── Command processing (shared between WebSocket and MQTT) ──────────────────

void processCommand(JsonDocument &doc)
{
    const char *type = doc["type"];
    if (!type) return;

    if (strcmp(type, "setLed") == 0)
    {
        if (bambuController.getBambuMode() && bambuController.isConnected()) return;
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
        String newSSID     = doc["ssid"]       | "";
        String newPassword = doc["password"]   | "";
        bool   dhcpMode    = doc["dhcp"]       | true;
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
    else if (strcmp(type, "startOTA") == 0)
    {
        xTaskCreate([](void*) {
            otaController.start();
            vTaskDelete(NULL);
        }, "ota", 8192, NULL, 1, NULL);
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
        bool on = doc["cycle"] | false;
        if (on) cancelBambuMode();
        ledController.setCycle(
            on,
            doc["topLedTime"]    | ledController.getTopLedTime(),
            doc["middleLedTime"] | ledController.getMiddleLedTime(),
            doc["bottomLedTime"] | ledController.getBottomLedTime()
        );
        configController.markDirty();
        broadcastCycleStatus();
    }
    else if (strcmp(type, "setParty") == 0)
    {
        bool on = doc["party"] | false;
        if (on) cancelBambuMode();
        ledController.setParty(on, doc["partyMadness"] | -1);
        configController.markDirty();
        broadcastPartyStatus();
        broadcastCycleStatus();
        broadcastRainbowStatus();
    }
    else if (strcmp(type, "setRainbow") == 0)
    {
        bool on = doc["rainbow"] | false;
        if (on) cancelBambuMode();
        ledController.setRainbow(
            on,
            doc["rainbowCycleTime"] | ledController.getRainbowCycleTime()
        );
        configController.markDirty();
        broadcastRainbowStatus();
        broadcastCycleStatus();
        broadcastPartyStatus();
    }
    else if (strcmp(type, "randomYesNo") == 0)
    {
        cancelBambuMode();
        ledController.startRandomYesNo();
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    }
    else if (strcmp(type, "morse") == 0)
    {
        cancelBambuMode();
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
    else if (strcmp(type, "weatherColor") == 0)
    {
        cancelBambuMode();
        Serial.printf("[weatherColor] valid=%d temp=%.1f\n",
                      geoController.weather.valid, geoController.weather.temperature);
        applyWeatherColor();
    }
    else if (strcmp(type, "airQualityColor") == 0)
    {
        cancelBambuMode();
        Serial.printf("[airQualityColor] valid=%d pm2_5=%.1f\n",
                      geoController.airQuality.valid, geoController.airQuality.pm2_5);
        applyAirQualityColor();
    }
    else if (strcmp(type, "setLocation") == 0)
    {
        float lat = doc["latitude"]  | 0.0f;
        float lon = doc["longitude"] | 0.0f;
        configController.setLocation(lat, lon);
        geoController.setLocation(lat, lon);
        Serial.printf("[Geo] Location updated: %.6f, %.6f\n", lat, lon);
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


    if (strcmp(type, "startGuess") == 0)
    {
        cancelBambuMode();
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
        String newSSID     = doc["ssid"]       | "";
        String newPassword = doc["password"]   | "";
        bool   dhcpMode    = doc["dhcp"]       | true;
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

    if (strcmp(type, "consoleOpen") == 0)
    {
        _consoleViewerIds.insert(client->id());
        return;
    }

    if (strcmp(type, "consoleClose") == 0)
    {
        _consoleViewerIds.erase(client->id());
        return;
    }

    if (strcmp(type, "infoOpen") == 0)
    {
        _infoViewerIds.insert(client->id());
        sendSysInfo(client);  // immediate push, don't wait for the 1 s tick
        return;
    }

    if (strcmp(type, "infoClose") == 0)
    {
        _infoViewerIds.erase(client->id());
        return;
    }

    if (strcmp(type, "bambuOpen") == 0)
    {
        _bambuViewerIds.insert(client->id());
        return;
    }

    if (strcmp(type, "bambuClose") == 0)
    {
        _bambuViewerIds.erase(client->id());
        return;
    }

    // getMqtt needs client-specific response
    if (strcmp(type, "getMqtt") == 0)
    {
        sendMqttConfig(client);
        return;
    }

    if (strcmp(type, "getBambu") == 0)
    {
        sendBambuConfig(client);
        return;
    }

    if (strcmp(type, "setBambu") == 0)
    {
        bambuController.applyConfig(
            doc["ip"]         | "",
            doc["serial"]     | "",
            doc["accessCode"] | "",
            doc["enabled"]    | false
        );
        bambuController.saveConfig();
        sendBambuConfig(client);
        return;
    }

    if (strcmp(type, "setBambuMode") == 0)
    {
        bool    mode    = doc["bambuMode"]      | false;
        uint8_t timeout = doc["idleTimeoutMin"] | (uint8_t)5;
        bambuController.setBambuMode(mode);
        bambuController.setIdleTimeoutMin(timeout);
        configController.setBambuMode(mode);
        configController.setIdleTimeoutMin(timeout);
        bambuController.resetIdle();
        if (mode) {
            ledController.setCycle(false, ledController.getTopLedTime(), ledController.getMiddleLedTime(), ledController.getBottomLedTime());
            ledController.setParty(false, ledController.getPartyMadness());
            ledController.setRainbow(false, ledController.getRainbowCycleTime());
            broadcastCycleStatus();
            broadcastPartyStatus();
            broadcastRainbowStatus();
            if (bambuController.isConnected()) {
                BambuState cur = bambuController.getState();
                applyBambuLedState(cur);
                if (cur == BambuState::IDLE || cur == BambuState::FINISH)
                    bambuController.markIdle();
            }
        } else {
            ledController.cancelOverlay();
        }
        {
            JsonDocument bdoc;
            bdoc["type"]      = "bambuConfig";
            bdoc["bambuMode"] = mode;
            String bmsg; serializeJson(bdoc, bmsg);
            ws.textAll(bmsg);
        }
        sendBambuConfig(client);
        return;
    }

    if (strcmp(type, "setBambuStateColor") == 0)
    {
        const char* stateName = doc["state"] | "";
        int     led = doc["led"] | -1;
        uint8_t r   = doc["r"]   | 0;
        uint8_t g   = doc["g"]   | 0;
        uint8_t b   = doc["b"]   | 0;
        if (led >= 0 && led <= 2) {
            BambuState s = BambuLabController::stateFromString(stateName);
            bambuController.setStateColor(s, led, r, g, b);
            bambuController.saveConfig();
            if (bambuController.getBambuMode() && bambuController.getState() == s)
                applyBambuLedState(s);
        }
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

    if (strcmp(type, "consoleCmd") == 0)
    {
        String cmd = doc["cmd"] | "";
        if (cmd.length() > 0)
            serialConsole.executeFromWeb(cmd);
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
            client->keepAlivePeriod(10);  // native WS ping frame every 10 s
            sendLedStatus(client);
            sendCycleStatus(client);
            sendPartyStatus(client);
            sendRainbowStatus(client);
            sendWifiConfig(client);
            sendMqttConfig(client);
            sendBambuConfig(client);
            timerController.sendTimers(client);
            sendConfigStatus(client);
            sendSysInfoStatic(client);
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected from %s\n", client->id(), client->remoteIP().toString().c_str());
            _consoleViewerIds.erase(client->id());
            _infoViewerIds.erase(client->id());
            _bambuViewerIds.erase(client->id());
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

    static String cmdBody;
    webServer.on("/cmd", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            JsonDocument doc;
            if (deserializeJson(doc, cmdBody)) { request->send(400, "text/plain", "invalid json"); return; }
            processCommand(doc);
            request->send(200, "text/plain", "ok");
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) cmdBody = "";
            cmdBody += String((char*)data).substring(0, len);
        }
    );

    webServer.on("/backup", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        auto readFile = [&](const char* path, const char* key) {
            if (!LittleFS.exists(path)) return;
            File f = LittleFS.open(path, "r");
            if (!f) return;
            JsonDocument tmp;
            if (!deserializeJson(tmp, f)) doc[key] = tmp;
            f.close();
        };
        readFile("/config.json", "config");
        readFile("/wifi.json",   "wifi");
        readFile("/mqtt.json",   "mqtt");
        readFile("/timers.json", "timers");
        readFile("/bambu.json",  "bambu");
        String out;
        serializeJsonPretty(doc, out);
        AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", out);
        resp->addHeader("Content-Disposition", "attachment; filename=\"semaphore-backup.json\"");
        request->send(resp);
    });

    static String restoreBody;
    webServer.on("/restore", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            JsonDocument doc;
            if (deserializeJson(doc, restoreBody)) { request->send(400, "text/plain", "invalid json"); return; }
            auto writeFile = [&](const char* path, const char* key) {
                if (doc[key].isNull()) return;
                File f = LittleFS.open(path, "w");
                if (!f) return;
                serializeJsonPretty(doc[key], f);
                f.close();
            };
            writeFile("/config.json", "config");
            writeFile("/wifi.json",   "wifi");
            writeFile("/mqtt.json",   "mqtt");
            writeFile("/timers.json", "timers");
            writeFile("/bambu.json",  "bambu");
            Serial.println("[Backup] Restore completed.");
            request->send(200, "text/plain", "restore completed");
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0) restoreBody = "";
            restoreBody += String((char*)data).substring(0, len);
        }
    );

    webServer.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "restarting");
        delay(200);
        ESP.restart();
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
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS mount failed");
        return;
    }
#ifdef IMPROV_ENABLED
    // Improv must run before any other Serial output to avoid confusing ESP Web Tools
    if (!LittleFS.exists("/wifi.json"))
        runImprovSetup(FIRMWARE_VERSION);
#endif

    monitorController.begin();
    monitorController.displayMessage("Startup...");
    monitorController.displayMessage("Connecting to\nWiFi...");
    ledController.begin();
    configController.begin(ledController);
    timerController.begin();
    timerController.onAllOff = []() {
        if (bambuController.getBambuMode()) {
            bambuController.setBambuMode(false);
            configController.setBambuMode(false);
            bambuController.resetIdle();
            ledController.cancelOverlay();
            JsonDocument bdoc;
            bdoc["type"]      = "bambuConfig";
            bdoc["bambuMode"] = false;
            String bmsg; serializeJson(bdoc, bmsg);
            ws.textAll(bmsg);
        }
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
        if (on) cancelBambuMode();
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
        if (on) cancelBambuMode();
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
        if (on) cancelBambuMode();
        ledController.setRainbow(on, ledController.getRainbowCycleTime());
        if (on) {
            ledController.setCycle(false, ledController.getTopLedTime(), ledController.getMiddleLedTime(), ledController.getBottomLedTime());
            ledController.setParty(false, ledController.getPartyMadness());
        }
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    };
    timerController.onWeatherColor = []() {
        cancelBambuMode();
        applyWeatherColor();
    };
    timerController.onBambuMode = [](bool on) {
        bambuController.setBambuMode(on);
        configController.setBambuMode(on);
        bambuController.resetIdle();
        if (on) {
            ledController.setCycle(false, ledController.getTopLedTime(), ledController.getMiddleLedTime(), ledController.getBottomLedTime());
            ledController.setParty(false, ledController.getPartyMadness());
            ledController.setRainbow(false, ledController.getRainbowCycleTime());
            broadcastCycleStatus();
            broadcastPartyStatus();
            broadcastRainbowStatus();
            if (bambuController.isConnected()) {
                BambuState cur = bambuController.getState();
                applyBambuLedState(cur);
                if (cur == BambuState::IDLE || cur == BambuState::FINISH)
                    bambuController.markIdle();
            }
        } else {
            ledController.cancelOverlay();
        }
        JsonDocument doc;
        doc["type"]      = "bambuConfig";
        doc["bambuMode"] = on;
        String msg; serializeJson(doc, msg);
        ws.textAll(msg);
    };
    timerController.onGuess = [](int led) {
        cancelBambuMode();
        ledController.startGuess(led);
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    };
    timerController.onMorse = [](const String& text) {
        cancelBambuMode();
        ledController.startMorse(text.c_str());
        broadcastCycleStatus();
        broadcastPartyStatus();
        broadcastRainbowStatus();
    };
    timerController.onRandomYesNo = []() {
        cancelBambuMode();
        ledController.startRandomYesNo();
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
    geoController.begin(configController.getLatitude(), configController.getLongitude());
    geoController.onWeatherUpdate = []() {
        mqttController.publishWeather(
            geoController.weather.temperature,
            geoController.weather.humidity,
            conditionToString(geoController.weather.condition)
        );
        broadcastSysInfoStatic();
    };
    geoController.onAirQualityUpdate = []() {
        mqttController.publishAirQuality(
            geoController.airQuality.pm2_5,
            geoController.airQuality.pm10,
            geoController.airQuality.no2
        );
        broadcastSysInfoStatic();
    };
    configTzTime(wifiManager.timezone.c_str(), wifiManager.ntpServer.c_str());
    MDNS.begin(wifiManager.deviceName.c_str());
    alexaController.begin(webServer, ledController);
    alexaController.onChanged = []() {
        broadcastLedStatus();
        configController.markDirty();
    };
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

    bambuController.loadConfig();
    bambuController.setBambuMode(configController.getBambuMode());
    bambuController.setIdleTimeoutMin(configController.getIdleTimeoutMin());
    bambuController.onStateChange = [](BambuState state) {
        JsonDocument doc;
        doc["type"]      = "bambuStatus";
        doc["state"]     = BambuLabController::stateToString(state);
        doc["connected"] = bambuController.isConnected();
        String msg;
        serializeJson(doc, msg);
        ws.textAll(msg);
        if (state == BambuState::IDLE || state == BambuState::FINISH) {
            bambuController.markIdle();
        } else {
            bambuController.resetIdle();
        }
        if (bambuController.getBambuMode()) {
            applyBambuLedState(state);
        }
    };
    bambuController.onIdleTimeout = []() {
        ledController.showColor(0, 0, 0, 3600000UL * 24);
    };
    // Suppress low-level TLS error spam from the ESP32 core.
    // BambuLab occasionally sends large records; errors are handled by
    // our own reconnect logic and logged as "[BambuLab] connect failed".
    esp_log_level_set("ssl_client", ESP_LOG_NONE);
    bambuController.begin();
    serialConsole.setBambu(bambuController);

    serialConsole.begin();

    // Mark firmware as valid — cancels automatic rollback.
    // If setup() never reaches this point (crash, watchdog, panic),
    // the bootloader will revert to the previous firmware on next boot.
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[OTA] Firmware validated — rollback cancelled");

    otaController.onBeforeStart = []() {
        // Disconnect BambuLab and MQTT to free their TLS/TCP heap before OTA
        // opens its own HTTPS connections — prevents SSL memory allocation failures.
        bambuController.applyConfig("", "", "", false);
        mqttController.applyConfig("", 1883, "", "", "", "", false);
    };
    otaController.onStatus = [](const char* step) {
        JsonDocument doc;
        doc["type"] = "otaStatus";
        doc["step"] = step;
        String msg;
        serializeJson(doc, msg);
        ws.textAll(msg);
    };
    otaController.onProgress = [](const char* step, int pct) {
        JsonDocument doc;
        doc["type"] = "otaProgress";
        doc["step"] = step;
        doc["pct"]  = pct;
        String msg;
        serializeJson(doc, msg);
        ws.textAll(msg);
    };
}

void loop()
{
    ArduinoOTA.handle();
    alexaController.loop();
    networkManager.handleFallbackLogic();
    monitorController.loop();
    ledController.update();
    timerController.loop();
    ws.cleanupClients();
    if (!_consoleViewerIds.empty()) {
        String msg = teeSerial.drainOne();
        if (msg.length()) {
            for (uint32_t id : _consoleViewerIds) {
                AsyncWebSocketClient* c = ws.client(id);
                if (c) c->text(msg);
            }
        }
    }
    {
        static unsigned long _lastInfoPush = 0;
        if (!_infoViewerIds.empty() && millis() - _lastInfoPush >= 1000) {
            _lastInfoPush = millis();
            for (uint32_t id : _infoViewerIds) {
                AsyncWebSocketClient* c = ws.client(id);
                if (c) sendSysInfo(c);
            }
        }
    }
    {
        static bool _lastBambuConnected = false;
        bool nowConnected = bambuController.isConnected();
        if (nowConnected != _lastBambuConnected) {
            _lastBambuConnected = nowConnected;
            JsonDocument doc;
            doc["type"]      = "bambuConfig";
            doc["connected"] = nowConnected;
            if (!nowConnected) doc["state"] = "offline";
            String msg; serializeJson(doc, msg);
            _sendToBambuAndInfoViewers(msg);
        }
    }
    {
        static unsigned long _lastIdlePush = 0;
        if (millis() - _lastIdlePush >= 1000) {
            _lastIdlePush = millis();
            int32_t idleSec = bambuController.getIdleSec();
            if (idleSec >= 0) {
                JsonDocument doc;
                doc["type"]    = "bambuConfig";
                doc["idleSec"] = idleSec;
                String msg; serializeJson(doc, msg);
                _sendToBambuAndInfoViewers(msg);
            }
        }
    }
    mqttController.loop();
    configController.loop();
    geoController.loop();
    serialConsole.loop();
    bambuController.loop();
}
