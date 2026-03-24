#pragma once
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "ledController.h"

#define CONFIG_FILE            "/config.json"
#define CONFIG_SAVE_DELAY_MS   10000

class ConfigController
{
public:
    void begin(LEDController &leds)
    {
        _leds = &leds;
        load();
    }

    void loop()
    {
        if (_dirtyTime > 0 && millis() - _dirtyTime >= CONFIG_SAVE_DELAY_MS)
        {
            save();
            _dirtyTime = 0;
        }
    }

    void markDirty()
    {
        if (_makeChangesPersistent) _dirtyTime = millis();
    }

    bool getMakeChangesPersistent() { return _makeChangesPersistent; }

    void setMakeChangesPersistent(bool value)
    {
        _makeChangesPersistent = value;
        save();
    }

    void save()
    {
        JsonDocument doc;
        JsonArray leds = doc["leds"].to<JsonArray>();
        for (int i = 0; i < LED_COUNT; i++)
        {
            JsonObject led  = leds.add<JsonObject>();
            uint32_t color  = _leds->getLEDColor(i);
            led["r"]        = (color >> 16) & 0xFF;
            led["g"]        = (color >> 8)  & 0xFF;
            led["b"]        = color & 0xFF;
            led["on"]       = _leds->getLEDOn(i);
            led["blink"]    = _leds->getLEDBlink(i);
        }
        doc["cycle"]            = _leds->getCycleEnabled();
        doc["topLedTime"]       = _leds->getTopLedTime();
        doc["middleLedTime"]    = _leds->getMiddleLedTime();
        doc["bottomLedTime"]    = _leds->getBottomLedTime();
        doc["party"]            = _leds->getPartyEnabled();
        doc["partyMadness"]     = _leds->getPartyMadness();
        doc["rainbow"]                = _leds->getRainbowEnabled();
        doc["rainbowCycleTime"]       = _leds->getRainbowCycleTime();
        doc["makeChangesPersistent"]  = _makeChangesPersistent;
        File file = LittleFS.open(CONFIG_FILE, "w");
        serializeJsonPretty(doc, file);
        file.close();
        Serial.println("[Config] config.json saved");
    }

private:
    LEDController   *_leds                  = nullptr;
    unsigned long    _dirtyTime             = 0;
    bool             _makeChangesPersistent = true;

    void load()
    {
        if (!LittleFS.exists(CONFIG_FILE)) return;
        File file = LittleFS.open(CONFIG_FILE, "r");
        if (!file) return;
        JsonDocument doc;
        if (deserializeJson(doc, file)) { file.close(); return; }
        file.close();
        JsonArray leds = doc["leds"];
        for (int i = 0; i < LED_COUNT && i < (int)leds.size(); i++)
        {
            _leds->setLED(i,
                leds[i]["r"]     | 0,
                leds[i]["g"]     | 0,
                leds[i]["b"]     | 0,
                leds[i]["on"]    | false,
                leds[i]["blink"] | false);
        }
        _leds->setCycle(
            doc["cycle"]         | false,
            doc["topLedTime"]    | 5.0f,
            doc["middleLedTime"] | 2.0f,
            doc["bottomLedTime"] | 5.0f
        );
        _leds->setParty(doc["party"] | false, doc["partyMadness"] | 5);
        _leds->setRainbow(doc["rainbow"] | false, doc["rainbowCycleTime"] | 5.0f);
        _makeChangesPersistent = doc["makeChangesPersistent"] | true;
        Serial.println("[Config] config.json loaded");
    }
};
