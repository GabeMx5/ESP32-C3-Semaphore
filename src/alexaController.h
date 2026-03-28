#pragma once
#include <WiFiUdp.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>
#include "ledController.h"

// ─── AlexaController ─────────────────────────────────────────────────────────
// Custom Philips Hue Bridge emulation — no external library required.
//
// Presents itself as a single Hue bridge with 4 devices:
//   - 3 "Extended color light" bulbs (one per LED) with full RGB control
//   - 1 "On/Off light" (key "4") that toggles party mode
//
// Protocol flow:
//   1. SSDP multicast (239.255.255.250:1900) — responds to Alexa M-SEARCH
//   2. GET /desc.xml               — UPnP device description
//   3. POST /api                   — Hue username handshake
//   4. GET /api/{user}/lights      — light list (keys "1"–"4")
//   5. GET /api/{user}/lights/{k}  — individual light state
//   6. PUT /api/{user}/lights/{k}/state — color/on-off control
//
// Light key → function mapping:
//   key "1" = Semaphore top     → LED 2
//   key "2" = Semaphore middle  → LED 1
//   key "3" = Semaphore bottom  → LED 0
//   key "4" = Semaphore party   → party mode on/off
//   key "5" = Semaphore rainbow → rainbow mode on/off
//
// Key design decisions:
//   - Two UDP sockets: _udp for multicast receive, _udpOut (bound once on port
//     1901) for unicast responses. The multicast socket cannot send unicast.
//   - Explicit server.on() GET handlers registered before serveStatic so they
//     are not swallowed. POST/PUT fall through to onNotFound which fires after
//     the full request body is received — critical for reading PUT state bodies.
//   - uniqueid uses EUI-64 format AA:BB:CC:DD:EE:FF:00:0K-0K to satisfy Alexa.

class AlexaController {
    WiFiUDP        _udp;     // multicast receive (239.255.255.250:1900)
    WiFiUDP        _udpOut;  // unicast send — bound once on port 1901, reused
    bool           _udpReady = false;
    LEDController* _leds    = nullptr;
    String         _body;   // accumulated by onRequestBody; consumed by PUT handler

    static const char* _username() { return "2WLEDHardQrI3WHYTHoMcXHgEspsM8ZZRpSKtBQr"; }

    // Hue light key (1–3) → LED index in LEDController
    static uint8_t _led(uint8_t key) {
        static const uint8_t m[] = { 0, 2, 1, 0 };  // key 1→LED2, 2→LED1, 3→LED0
        return (key >= 1 && key <= 3) ? m[key] : 0;
    }

    static const char* _name(uint8_t key) {
        static const char* n[] = { "", "Semaphore top", "Semaphore middle", "Semaphore bottom", "Semaphore party", "Semaphore rainbow" };
        return (key >= 1 && key <= 5) ? n[key] : "";
    }

    // ── Color helpers ─────────────────────────────────────────────────────────

    static void rgbToHueSat(uint8_t r, uint8_t g, uint8_t b,
                             uint16_t& hue, uint8_t& sat, uint8_t& bri) {
        float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
        float mx = max(rf, max(gf, bf));
        float mn = min(rf, min(gf, bf));
        bri = (uint8_t)(mx * 254);
        sat = (mx > 0) ? (uint8_t)((mx - mn) / mx * 254) : 0;
        float h = 0;
        if (mx != mn) {
            float d = mx - mn;
            if      (mx == rf) h = (gf - bf) / d + (gf < bf ? 6.f : 0.f);
            else if (mx == gf) h = (bf - rf) / d + 2.f;
            else               h = (rf - gf) / d + 4.f;
            h /= 6.f;
        }
        hue = (uint16_t)(h * 65535);
    }

    static void hueSatBriToRgb(uint16_t hue, uint8_t sat, uint8_t bri,
                                uint8_t& r, uint8_t& g, uint8_t& b) {
        float h = (hue / 65535.f) * 6.f;
        float s = sat  / 254.f;
        float v = bri  / 254.f;
        int   i = (int)h;
        float f = h - i;
        float p = v * (1.f - s);
        float q = v * (1.f - f * s);
        float t = v * (1.f - (1.f - f) * s);
        float rf, gf, bf;
        switch (i % 6) {
            case 0: rf=v; gf=t; bf=p; break;
            case 1: rf=q; gf=v; bf=p; break;
            case 2: rf=p; gf=v; bf=t; break;
            case 3: rf=p; gf=q; bf=v; break;
            case 4: rf=t; gf=p; bf=v; break;
            default:rf=v; gf=p; bf=q; break;
        }
        r = (uint8_t)(rf * 255);
        g = (uint8_t)(gf * 255);
        b = (uint8_t)(bf * 255);
    }

    // CIE 1931 xy → sRGB (Alexa uses xy color mode for some commands)
    static void xyBriToRgb(float cx, float cy, uint8_t bri,
                            uint8_t& r, uint8_t& g, uint8_t& b) {
        float Y = bri / 254.f;
        float X = (cy > 0) ? (Y / cy) * cx : 0;
        float Z = (cy > 0) ? (Y / cy) * (1.f - cx - cy) : 0;
        float rf =  X * 1.656492f - Y * 0.354851f - Z * 0.255038f;
        float gf = -X * 0.707196f + Y * 1.655397f + Z * 0.036152f;
        float bf =  X * 0.051713f - Y * 0.121364f + Z * 1.011530f;
        float mx = max(rf, max(gf, max(bf, 1.f)));
        auto  gc = [](float v) -> uint8_t {
            float c = v <= 0.0031308f ? 12.92f * v : 1.055f * powf(v, 1.f/2.4f) - 0.055f;
            return (uint8_t)(max(0.f, min(1.f, c)) * 255);
        };
        r = gc(rf / mx); g = gc(gf / mx); b = gc(bf / mx);
    }

    // ── HTTP helpers ──────────────────────────────────────────────────────────

    String _mac() {
        String m = WiFi.macAddress();
        m.replace(":", "");
        m.toLowerCase();
        return m;
    }

    // uniqueid in EUI-64 format required by Alexa: AA:BB:CC:DD:EE:FF:00:0K-0K
    String _uniqueId(uint8_t key) {
        String m = _mac();
        char uid[32];
        snprintf(uid, sizeof(uid),
            "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c:00:%02x-%02x",
            m[0],m[1], m[2],m[3], m[4],m[5], m[6],m[7], m[8],m[9], m[10],m[11],
            key, key);
        return String(uid);
    }

    String _deviceJson(uint8_t key) {
        uint8_t  ledIdx = _led(key);
        bool     on     = _leds->getLEDOn(ledIdx);
        uint32_t color  = _leds->getLEDColor(ledIdx);
        uint8_t  r = (color >> 16) & 0xFF;
        uint8_t  g = (color >>  8) & 0xFF;
        uint8_t  b =  color        & 0xFF;
        uint16_t hue; uint8_t sat, bri;
        rgbToHueSat(r, g, b, hue, sat, bri);

        char buf[512];
        snprintf(buf, sizeof(buf),
            "{\"state\":{\"on\":%s,\"bri\":%u,\"hue\":%u,\"sat\":%u,"
            "\"effect\":\"none\",\"xy\":[0.3,0.3],\"ct\":370,"
            "\"alert\":\"none\",\"colormode\":\"hs\",\"reachable\":true},"
            "\"type\":\"Extended color light\","
            "\"name\":\"%s\","
            "\"modelid\":\"LCT001\","
            "\"manufacturername\":\"Philips\","
            "\"uniqueid\":\"%s\","
            "\"swversion\":\"65003148\"}",
            on ? "true" : "false", bri ? bri : 1, hue, sat,
            _name(key), _uniqueId(key).c_str());
        return String(buf);
    }

    // Simple on/off JSON for effect lights (keys 4–5)
    String _effectJson(uint8_t key, bool on) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"state\":{\"on\":%s,\"bri\":254,\"alert\":\"none\",\"reachable\":true},"
            "\"type\":\"On/Off light\","
            "\"name\":\"%s\","
            "\"modelid\":\"LOM001\","
            "\"manufacturername\":\"Philips\","
            "\"uniqueid\":\"%s\","
            "\"swversion\":\"1.88.1\"}",
            on ? "true" : "false", _name(key), _uniqueId(key).c_str());
        return String(buf);
    }

    void _handleApi(AsyncWebServerRequest* req) {
        String url = req->url();

        // POST /api — Hue username handshake
        if (req->method() == HTTP_POST && (url == "/api" || url == "/api/")) {
            req->send(200, "application/json",
                "[{\"success\":{\"username\":\"" + String(_username()) + "\"}}]");
            return;
        }

        // PUT /api/{user}/lights/{key}/state — set LED color/state or party mode
        if (req->method() == HTTP_PUT && url.indexOf("/state") > 0) {
            int li = url.indexOf("/lights/");
            if (li >= 0) {
                uint8_t key = url.substring(li + 8).toInt();

                // Key 4 = party mode toggle
                if (key == 4) {
                    JsonDocument doc;
                    if (!deserializeJson(doc, _body) && !doc["on"].isNull()) {
                        bool on = doc["on"].as<bool>();
                        _leds->setParty(on, _leds->getPartyMadness());
                        Serial.printf("[Alexa] party → %s\n", on ? "ON" : "OFF");
                        if (onChanged) onChanged();
                    }
                    _body = "";
                    req->send(200, "application/json", "[{\"success\":true}]");
                    return;
                }

                // Key 5 = rainbow mode toggle
                if (key == 5) {
                    JsonDocument doc;
                    if (!deserializeJson(doc, _body) && !doc["on"].isNull()) {
                        bool on = doc["on"].as<bool>();
                        _leds->setRainbow(on, _leds->getRainbowCycleTime());
                        Serial.printf("[Alexa] rainbow → %s\n", on ? "ON" : "OFF");
                        if (onChanged) onChanged();
                    }
                    _body = "";
                    req->send(200, "application/json", "[{\"success\":true}]");
                    return;
                }

                if (key >= 1 && key <= 3) {
                    JsonDocument doc;
                    if (!deserializeJson(doc, _body)) {
                        uint8_t  ledIdx = _led(key);
                        bool     on     = _leds->getLEDOn(ledIdx);
                        uint32_t color  = _leds->getLEDColor(ledIdx);
                        uint8_t  r = (color >> 16) & 0xFF;
                        uint8_t  g = (color >>  8) & 0xFF;
                        uint8_t  b =  color        & 0xFF;
                        uint16_t hue; uint8_t sat, bri;
                        rgbToHueSat(r, g, b, hue, sat, bri);

                        if (!doc["on" ].isNull()) on  = doc["on" ].as<bool>();
                        if (!doc["bri"].isNull()) bri = doc["bri"].as<uint8_t>();
                        if (!doc["hue"].isNull()) hue = doc["hue"].as<uint16_t>();
                        if (!doc["sat"].isNull()) sat = doc["sat"].as<uint8_t>();

                        if (!doc["xy"].isNull()) {
                            float cx = doc["xy"][0].as<float>();
                            float cy = doc["xy"][1].as<float>();
                            xyBriToRgb(cx, cy, bri, r, g, b);
                        } else {
                            hueSatBriToRgb(hue, sat, bri, r, g, b);
                        }

                        Serial.printf("[Alexa] LED %u → %s r=%u g=%u b=%u\n",
                                      ledIdx, on ? "ON" : "OFF", r, g, b);
                        _leds->setLED(ledIdx, r, g, b, on, false);
                        if (onChanged) onChanged();
                    }
                    _body = "";
                    req->send(200, "application/json", "[{\"success\":true}]");
                    return;
                }
            }
        }

        // GET /api/{user}/lights or /api/{user}/lights/{key}
        if (url.indexOf("/lights") > 0) {
            String suffix = url.substring(url.indexOf("/lights") + 7);
            if (suffix.length() <= 1) {
                String resp = "{";
                for (uint8_t k = 1; k <= 3; k++) {
                    resp += "\"" + String(k) + "\":" + _deviceJson(k) + ",";
                }
                resp += "\"4\":" + _effectJson(4, _leds->getPartyEnabled()) + ",";
                resp += "\"5\":" + _effectJson(5, _leds->getRainbowEnabled()) + "}";
                req->send(200, "application/json", resp);
            } else {
                uint8_t key = suffix.substring(1).toInt();
                if (key >= 1 && key <= 3)
                    req->send(200, "application/json", _deviceJson(key));
                else if (key == 4)
                    req->send(200, "application/json", _effectJson(4, _leds->getPartyEnabled()));
                else if (key == 5)
                    req->send(200, "application/json", _effectJson(5, _leds->getRainbowEnabled()));
                else
                    req->send(200, "application/json", "{}");
            }
            return;
        }

        req->send(200, "application/json", "{}");
    }

    void _respondToSearch() {
        IPAddress remoteIP   = _udp.remoteIP();
        uint16_t  remotePort = _udp.remotePort();

        IPAddress ip = WiFi.localIP();
        char ips[16];
        sprintf(ips, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        String uid = _mac();

        char buf[512];
        snprintf(buf, sizeof(buf),
            "HTTP/1.1 200 OK\r\n"
            "EXT:\r\n"
            "CACHE-CONTROL: max-age=100\r\n"
            "LOCATION: http://%s:80/desc.xml\r\n"
            "SERVER: FreeRTOS/6.0.5, UPnP/1.0, IpBridge/1.17.0\r\n"
            "hue-bridgeid: %s\r\n"
            "ST: upnp:rootdevice\r\n"
            "USN: uuid:2f402f80-da50-11e1-9b23-%s::upnp:rootdevice\r\n\r\n",
            ips, uid.c_str(), uid.c_str());

        _udpOut.beginPacket(remoteIP, remotePort);
        _udpOut.write((uint8_t*)buf, strlen(buf));
        _udpOut.endPacket();
    }

public:
    std::function<void()> onChanged;

    void begin(AsyncWebServer& server, LEDController& leds) {
        _leds = &leds;

        _udpReady = _udp.beginMulticast(IPAddress(239, 255, 255, 250), 1900);
        _udpOut.begin(1901);
        Serial.printf("[Alexa] UDP multicast %s\n", _udpReady ? "OK" : "FAILED");

        // GET /desc.xml — UPnP device description (registered before serveStatic)
        server.on("/desc.xml", HTTP_GET, [this](AsyncWebServerRequest* req) {
            IPAddress ip = WiFi.localIP();
            char ips[16];
            sprintf(ips, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            String uid = _mac();
            char buf[768];
            snprintf(buf, sizeof(buf),
                "<?xml version=\"1.0\"?>"
                "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
                "<specVersion><major>1</major><minor>0</minor></specVersion>"
                "<URLBase>http://%s:80/</URLBase>"
                "<device>"
                "<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
                "<friendlyName>Semaphore (%s:80)</friendlyName>"
                "<manufacturer>Royal Philips Electronics</manufacturer>"
                "<modelName>Philips hue bridge 2012</modelName>"
                "<modelNumber>929000226503</modelNumber>"
                "<serialNumber>%s</serialNumber>"
                "<UDN>uuid:2f402f80-da50-11e1-9b23-%s</UDN>"
                "</device></root>",
                ips, ips, uid.c_str(), uid.c_str());
            req->send(200, "text/xml", buf);
        });

        // GET /api and /api/* — explicit handlers registered before serveStatic.
        // GET requests have no body so _handleApi can be called immediately.
        server.on("/api", HTTP_GET,
                  [this](AsyncWebServerRequest* req) { _handleApi(req); });
        server.on("/api/*", HTTP_GET,
                  [this](AsyncWebServerRequest* req) { _handleApi(req); });

        // Accumulate request body — needed for PUT /api/.../state.
        server.onRequestBody([this](AsyncWebServerRequest* req, uint8_t* data,
                                    size_t len, size_t index, size_t total) {
            if (index == 0) _body = "";
            _body += String((char*)data, len);
        });

        // POST /api and PUT /api/.../state are not matched by serveStatic (non-GET)
        // and fall here. onNotFound fires after the full body is received, so
        // _body is always populated before _handleApi reads it.
        server.onNotFound([this](AsyncWebServerRequest* req) {
            String url = req->url();
            if (url == "/api" || url.startsWith("/api/")) {
                _handleApi(req);
                return;
            }
            req->send(404);
        });

        Serial.println("[Alexa] Hue bridge ready (3 lights)");
    }

    void loop() {
        if (!_udpReady) return;
        int sz = _udp.parsePacket();
        if (sz < 1) return;
        char buf[sz + 1];
        _udp.read(buf, sz);
        buf[sz] = 0;
        if (strstr(buf, "M-SEARCH") && strstr(buf, "ssdp:disc")) {
            _respondToSearch();
        }
    }
};
