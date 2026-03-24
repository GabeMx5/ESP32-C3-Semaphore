#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#define GEO_UPDATE_INTERVAL_MS 60000UL

enum class WeatherCondition {
    UNKNOWN,
    CLEAR,          // 0-1   clear sky, mainly clear
    PARTLY_CLOUDY,  // 2-3   partly cloudy, overcast
    FOGGY,          // 45,48 fog
    DRIZZLE,        // 51-57 drizzle
    RAINY,          // 61-67, 80-82 rain / showers
    SNOWY,          // 71-77, 85-86 snow
    STORMY          // 95,96,99 thunderstorm
};

struct WeatherData {
    int              weatherCode  = -1;
    float            temperature  = 0.0f;
    float            windSpeed    = 0.0f;
    bool             isDay        = true;
    WeatherCondition condition    = WeatherCondition::UNKNOWN;
    bool             valid        = false;
    unsigned long    lastUpdate   = 0;
};

class GeoController
{
public:
    WeatherData weather;

    void begin(float lat, float lon)
    {
        _lat = lat;
        _lon = lon;
    }

    void setLocation(float lat, float lon)
    {
        _lat = lat;
        _lon = lon;
        weather.valid = false;
    }

    void loop()
    {
        // throttle debug to once every 5s
        if (millis() - _lastDebug >= 5000)
        {
            _lastDebug = millis();
            Serial.printf("[Geo] status — lat=%.4f lon=%.4f wifi=%d lastFetch=%lus ago\n",
                          _lat, _lon, WiFi.status(),
                          _lastFetch > 0 ? (millis() - _lastFetch) / 1000 : 0);
        }

        if (_lat == 0.0f && _lon == 0.0f) return;
        if (WiFi.status() != WL_CONNECTED) return;
        if (_lastFetch > 0 && millis() - _lastFetch < GEO_UPDATE_INTERVAL_MS) return;

        Serial.println("[Geo] calling fetch()...");
        fetch();
        _lastFetch = millis();
    }

private:
    float         _lat        = 0.0f;
    float         _lon        = 0.0f;
    unsigned long _lastFetch  = 0;
    unsigned long _lastDebug  = 0;

    void fetch()
    {
        String url = String("https://api.open-meteo.com/v1/forecast") +
                     "?latitude="  + String(_lat, 6) +
                     "&longitude=" + String(_lon, 6) +
                     "&current_weather=true";

        Serial.printf("[Geo] Fetching: %s\n", url.c_str());

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.setTimeout(10000);
        http.begin(client, url);
        http.addHeader("Accept", "application/json");

        int code = http.GET();
        Serial.printf("[Geo] HTTP status: %d\n", code);

        if (code != 200)
        {
            Serial.printf("[Geo] Error: %s\n", http.errorToString(code).c_str());
            http.end();
            return;
        }

        String payload = http.getString();
        http.end();

        Serial.printf("[Geo] Payload (%d bytes): %.120s\n", payload.length(), payload.c_str());

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err)
        {
            Serial.printf("[Geo] JSON error: %s\n", err.c_str());
            return;
        }

        JsonObject cw = doc["current_weather"];
        if (cw.isNull())
        {
            Serial.println("[Geo] current_weather missing in response");
            return;
        }

        weather.weatherCode = cw["weathercode"] | -1;
        weather.temperature = cw["temperature"] | 0.0f;
        weather.windSpeed   = cw["windspeed"]   | 0.0f;
        weather.isDay       = (cw["is_day"]     | 1) == 1;
        weather.condition   = mapCondition(weather.weatherCode);
        weather.valid       = true;
        weather.lastUpdate  = millis();

        Serial.printf("[Geo] OK — code=%d temp=%.1f°C wind=%.1fkm/h condition=%d\n",
                      weather.weatherCode, weather.temperature,
                      weather.windSpeed, (int)weather.condition);
    }

    static WeatherCondition mapCondition(int code)
    {
        if (code == 0 || code == 1)                              return WeatherCondition::CLEAR;
        if (code == 2 || code == 3)                              return WeatherCondition::PARTLY_CLOUDY;
        if (code == 45 || code == 48)                            return WeatherCondition::FOGGY;
        if (code >= 51 && code <= 57)                            return WeatherCondition::DRIZZLE;
        if ((code >= 61 && code <= 67) ||
            (code >= 80 && code <= 82))                          return WeatherCondition::RAINY;
        if ((code >= 71 && code <= 77) ||
            code == 85 || code == 86)                            return WeatherCondition::SNOWY;
        if (code == 95 || code == 96 || code == 99)              return WeatherCondition::STORMY;
        return WeatherCondition::UNKNOWN;
    }
};
