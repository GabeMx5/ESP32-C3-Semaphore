#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <time.h>
#include <functional>

#define TIMERS_MAX  50
#define TIMERS_FILE "/timers.json"

struct TimerEntry {
    int      id;
    bool     enabled;
    uint8_t  days;          // bitmask: bit0=Mon, bit1=Tue, ... bit6=Sun
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    String   action;        // "all_off","led0","led1","led2","cycle","party","rainbow","random_yes_no","morse"
    String   morseText;
    uint8_t  guessLed  = 0;
    uint8_t  ledR, ledG, ledB;
    uint32_t duration = 0;  // seconds; 0 = no limit
    // runtime
    int           _lastDay  = -1;
    uint8_t       _lastHour = 255;
    uint8_t       _lastMin  = 255;
    uint8_t       _lastSec  = 255;
    bool          _active   = false;
    unsigned long _firedAt  = 0;
};

class TimerController {
public:
    // ── Callbacks ─────────────────────────────────────────────────────────────
    std::function<void(int, uint8_t, uint8_t, uint8_t)> onLed;
    std::function<void()>     onAllOff;
    std::function<void(bool)> onCycle;
    std::function<void(bool)> onParty;
    std::function<void(bool)> onRainbow;
    std::function<void()>              onRandomYesNo;
    std::function<void(const String&)> onMorse;
    std::function<void(int)>           onGuess;
    std::function<void()>              onWeatherColor;

private:
    TimerEntry entries[TIMERS_MAX];
    int        count = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────

    static uint8_t parseDaysMask(JsonVariant v) {
        uint8_t mask = 0;
        if (v.is<JsonArray>())
            for (int d : v.as<JsonArray>())
                if (d >= 0 && d <= 6) mask |= (1 << d);
        return mask;
    }

    static void parseTime(const char* s, uint8_t& h, uint8_t& m, uint8_t& sec) {
        h = m = sec = 0;
        if (s) sscanf(s, "%hhu:%hhu:%hhu", &h, &m, &sec);
    }

    static void parseColor(const char* hex, uint8_t& r, uint8_t& g, uint8_t& b) {
        r = g = b = 255;
        if (!hex || hex[0] != '#') return;
        unsigned int c = 0;
        sscanf(hex + 1, "%06x", &c);
        r = (c >> 16) & 0xFF;
        g = (c >> 8)  & 0xFF;
        b =  c        & 0xFF;
    }

    void fromJson(JsonObject obj, TimerEntry& t) {
        t.id       = obj["id"]       | 0;
        t.enabled  = obj["enabled"]  | true;
        t.days     = parseDaysMask(obj["days"]);
        parseTime(obj["time"] | "00:00:00", t.hour, t.minute, t.second);
        t.action    = obj["action"]    | "all_off";
        t.morseText = obj["morseText"] | "SOS";
        t.guessLed  = obj["guessLed"]  | (uint8_t)0;
        parseColor(obj["ledColor"]    | "#ffffff", t.ledR, t.ledG, t.ledB);
        t.duration = obj["duration"] | (uint32_t)0;
        t._lastDay  = -1;
        t._lastHour = 255;
        t._lastMin  = 255;
        t._lastSec  = 255;
        t._active   = false;
        t._firedAt  = 0;
    }

    void toJson(const TimerEntry& t, JsonObject obj) const {
        obj["id"]       = t.id;
        obj["enabled"]  = t.enabled;
        JsonArray days  = obj["days"].to<JsonArray>();
        for (int d = 0; d < 7; d++)
            if (t.days & (1 << d)) days.add(d);
        char timeBuf[9];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", t.hour, t.minute, t.second);
        obj["time"]     = timeBuf;
        obj["action"]    = t.action;
        obj["morseText"] = t.morseText;
        obj["guessLed"]  = t.guessLed;
        char colBuf[8];
        snprintf(colBuf, sizeof(colBuf), "#%02x%02x%02x", t.ledR, t.ledG, t.ledB);
        obj["ledColor"] = colBuf;
        obj["duration"] = t.duration;
    }

    void fire(TimerEntry& t) {
        Serial.printf("[Timer] Firing id=%d action=%s duration=%us\n", t.id, t.action.c_str(), t.duration);
        if      (t.action == "all_off" && onAllOff)  onAllOff();
        else if (t.action == "led0"    && onLed)     onLed(0, t.ledR, t.ledG, t.ledB);
        else if (t.action == "led1"    && onLed)     onLed(1, t.ledR, t.ledG, t.ledB);
        else if (t.action == "led2"    && onLed)     onLed(2, t.ledR, t.ledG, t.ledB);
        else if (t.action == "cycle"          && onCycle)        onCycle(true);
        else if (t.action == "party"          && onParty)        onParty(true);
        else if (t.action == "rainbow"        && onRainbow)      onRainbow(true);
        else if (t.action == "random_yes_no"  && onRandomYesNo)   onRandomYesNo();
        else if (t.action == "morse"          && onMorse)         onMorse(t.morseText);
        else if (t.action == "guess"          && onGuess)         onGuess(t.guessLed);
        else if (t.action == "weather_color"  && onWeatherColor)  onWeatherColor();
        if (t.duration > 0) {
            t._active  = true;
            t._firedAt = millis();
        }
    }

    void fireOff(TimerEntry& t) {
        Serial.printf("[Timer] Duration expired id=%d action=%s\n", t.id, t.action.c_str());
        if      (t.action == "cycle"   && onCycle)   onCycle(false);
        else if (t.action == "party"   && onParty)   onParty(false);
        else if (t.action == "rainbow" && onRainbow) onRainbow(false);
        else if (onAllOff)                           onAllOff();
        t._active = false;
    }

public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────

    void begin() {
        load();
    }

    void loop() {
        unsigned long now = millis();

        // Check duration expirations
        for (int i = 0; i < count; i++) {
            TimerEntry& e = entries[i];
            if (e._active && now - e._firedAt >= (unsigned long)e.duration * 1000UL)
                fireOff(e);
        }

        struct tm t;
        if (!getLocalTime(&t, 0)) return;
        // tm_wday: 0=Sun…6=Sat → convert to 0=Mon…6=Sun
        int nowDay  = (t.tm_wday + 6) % 7;
        int nowHour = t.tm_hour;
        int nowMin  = t.tm_min;
        int nowSec  = t.tm_sec;
        for (int i = 0; i < count; i++) {
            TimerEntry& e = entries[i];
            if (!e.enabled) continue;
            if (!(e.days & (1 << nowDay))) continue;
            if (e.hour != nowHour || e.minute != nowMin || e.second != nowSec) continue;
            if (e._lastDay == nowDay && e._lastHour == nowHour && e._lastMin == nowMin && e._lastSec == nowSec) continue;
            e._lastDay  = nowDay;
            e._lastHour = nowHour;
            e._lastMin  = nowMin;
            e._lastSec  = nowSec;
            fire(e);
        }
    }

    // ── Persistence ───────────────────────────────────────────────────────────

    void load() {
        count = 0;
        if (!LittleFS.exists(TIMERS_FILE)) return;
        File file = LittleFS.open(TIMERS_FILE, "r");
        if (!file) return;
        JsonDocument doc;
        if (deserializeJson(doc, file)) { file.close(); return; }
        file.close();
        for (JsonObject obj : doc["timers"].as<JsonArray>()) {
            if (count >= TIMERS_MAX) break;
            fromJson(obj, entries[count++]);
        }
        Serial.printf("[Timer] Loaded %d timers\n", count);
    }

    void save() {
        JsonDocument doc;
        JsonArray arr = doc["timers"].to<JsonArray>();
        for (int i = 0; i < count; i++)
            toJson(entries[i], arr.add<JsonObject>());
        File file = LittleFS.open(TIMERS_FILE, "w");
        serializeJsonPretty(doc, file);
        file.close();
        Serial.printf("[Timer] Saved %d timers\n", count);
    }

    // ── Commands ──────────────────────────────────────────────────────────────

    void setTimers(JsonArray arr) {
        count = 0;
        for (JsonObject obj : arr) {
            if (count >= TIMERS_MAX) break;
            fromJson(obj, entries[count++]);
        }
        save();
    }

    void sendTimers(AsyncWebSocketClient* client) {
        JsonDocument doc;
        doc["type"]   = "timerConfig";
        JsonArray arr = doc["timers"].to<JsonArray>();
        for (int i = 0; i < count; i++)
            toJson(entries[i], arr.add<JsonObject>());
        String msg;
        serializeJson(doc, msg);
        client->text(msg);
    }

    int getCount() { return count; }
};
