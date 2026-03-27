#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#define GEO_UPDATE_INTERVAL_MS  1800000UL
#define GEO_AQ_INTERVAL_MS      1800000UL

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

struct AirQualityData {
    float         pm2_5    = 0.0f;
    float         pm10     = 0.0f;
    float         no2      = 0.0f;
    float         ozone    = 0.0f;
    bool          valid    = false;
    unsigned long lastUpdate = 0;
};

struct WeatherData {
    int              weatherCode  = -1;
    float            temperature  = 0.0f;
    float            humidity     = 0.0f;
    bool             isDay        = true;
    WeatherCondition condition    = WeatherCondition::UNKNOWN;
    bool             valid        = false;
    unsigned long    lastUpdate   = 0;
};

class GeoController
{
public:
    WeatherData    weather;
    AirQualityData airQuality;

    void begin(float lat, float lon)
    {
        _lat = lat;
        _lon = lon;
    }

    void setLocation(float lat, float lon)
    {
        _lat = lat;
        _lon = lon;
        weather.valid    = false;
        airQuality.valid = false;
    }

    void loop()
    {
        if (_lat == 0.0f && _lon == 0.0f) return;
        if (WiFi.status() != WL_CONNECTED) return;
        if (millis() - _lastFetch < GEO_UPDATE_INTERVAL_MS && _lastFetch > 0) return;
        fetch();

        if (millis() - _lastAirFetch < GEO_AQ_INTERVAL_MS && _lastAirFetch > 0) return;
        fetchAirQuality();
    }

private:
    float         _lat         = 0.0f;
    float         _lon         = 0.0f;
    unsigned long _lastFetch   = 0;
    unsigned long _lastAirFetch = 0;

    void fetch()
    {
        String url = "https://api.open-meteo.com/v1/forecast"
                     "?latitude="  + String(_lat, 6) +
                     "&longitude=" + String(_lon, 6) +
                     "&current=temperature_2m,weather_code,relative_humidity_2m,is_day";

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setTimeout(8000);

        if (!http.begin(client, url))
        {
            Serial.println("[Geo] http.begin failed");
            return;
        }

        int code = http.GET();
        if (code != 200)
        {
            Serial.printf("[Geo] HTTP error: %d\n", code);
            http.end();
            return;
        }

        String body = http.getString();
        http.end();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);

        if (err)
        {
            Serial.printf("[Geo] JSON error: %s\n", err.c_str());
            return;
        }

        JsonObject cw = doc["current"];
        weather.weatherCode = cw["weather_code"]           | -1;
        weather.temperature = cw["temperature_2m"]         | 0.0f;
        weather.isDay       = (cw["is_day"]                | 1) == 1;
        weather.humidity    = cw["relative_humidity_2m"]   | 0.0f;
        weather.condition   = mapCondition(weather.weatherCode);
        weather.valid       = true;
        weather.lastUpdate  = millis();
        _lastFetch          = millis();

        Serial.printf("[Geo] Weather updated: code=%d temp=%.1f condition=%d\n",
                      weather.weatherCode, weather.temperature, (int)weather.condition);
    }

    void fetchAirQuality()
    {
        String url = "https://air-quality-api.open-meteo.com/v1/air-quality"
                     "?latitude="  + String(_lat, 6) +
                     "&longitude=" + String(_lon, 6) +
                     "&current=pm10,pm2_5,nitrogen_dioxide,ozone";

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setTimeout(8000);

        if (!http.begin(client, url)) { Serial.println("[AQ] http.begin failed"); return; }

        int code = http.GET();
        if (code != 200)
        {
            Serial.printf("[AQ] HTTP error: %d\n", code);
            http.end();
            return;
        }

        String body = http.getString();
        http.end();

        JsonDocument doc;
        if (deserializeJson(doc, body)) { Serial.println("[AQ] JSON error"); return; }

        JsonObject c = doc["current"];
        airQuality.pm2_5  = c["pm2_5"]             | 0.0f;
        airQuality.pm10   = c["pm10"]               | 0.0f;
        airQuality.no2    = c["nitrogen_dioxide"]   | 0.0f;
        airQuality.ozone  = c["ozone"]              | 0.0f;
        airQuality.valid  = true;
        airQuality.lastUpdate = millis();
        _lastAirFetch         = millis();

        Serial.printf("[AQ] Updated: PM2.5=%.1f PM10=%.1f NO2=%.1f O3=%.1f\n",
                      airQuality.pm2_5, airQuality.pm10, airQuality.no2, airQuality.ozone);
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
