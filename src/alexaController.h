#pragma once
#define ESPALEXA_ASYNC
#include <Espalexa.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include "ledController.h"

// ─── AlexaController ─────────────────────────────────────────────────────────
// Emulates a Philips Hue Bridge via Espalexa so Alexa can discover and control
// each LED individually on the local network — no cloud account required.
//
// Usage:
//   alexaController.begin(webServer, ledController);  // before setupWebServer()
//   alexaController.loop();                           // in loop()
//
// Alexa device names (say "Alexa, turn on Semaphore top"):
//   "Semaphore top"    → LED 2 (top)
//   "Semaphore middle" → LED 1 (middle)
//   "Semaphore bottom" → LED 0 (bottom)

class AlexaController {
    Espalexa      _espalexa;
    LEDController* _leds = nullptr;

    void onDevice(uint8_t index, EspalexaDevice* dev) {
        bool    on = dev->getValue() > 0;
        uint8_t r  = dev->getR();
        uint8_t g  = dev->getG();
        uint8_t b  = dev->getB();

        // If just an on/off toggle with no color info, preserve the existing color
        if (dev->getColorMode() == EspalexaColorMode::none && on) {
            uint32_t cur = _leds->getLEDColor(index);
            r = (cur >> 16) & 0xFF;
            g = (cur >> 8)  & 0xFF;
            b = cur & 0xFF;
        }

        _leds->setLED(index, r, g, b, on, false);
        if (onChanged) onChanged();
    }

public:
    // Called after every LED change (use to broadcast status + mark config dirty)
    std::function<void()> onChanged;

    void begin(AsyncWebServer& server, LEDController& leds) {
        _leds = &leds;

        // Register one Hue "extendedcolor" bulb per LED (must be done before begin())
        _espalexa.addDevice("Semaphore top",
            [this](EspalexaDevice* d) { onDevice(2, d); },
            EspalexaDeviceType::extendedcolor);

        _espalexa.addDevice("Semaphore middle",
            [this](EspalexaDevice* d) { onDevice(1, d); },
            EspalexaDeviceType::extendedcolor);

        _espalexa.addDevice("Semaphore bottom",
            [this](EspalexaDevice* d) { onDevice(0, d); },
            EspalexaDeviceType::extendedcolor);

        // Route Alexa API HTTP requests to Espalexa.
        // These handlers must be registered BEFORE serveStatic so they are
        // checked first (ESPAsyncWebServer processes handlers in FIFO order).
        // /description.xml is also registered by begin() but we add it explicitly
        // first so it is not shadowed by the serveStatic catch-all.
        server.on("/description.xml", HTTP_GET, [this](AsyncWebServerRequest* req) {
            _espalexa.handleAlexaApiCall(req);
        });
        server.on("/api/*", HTTP_ANY, [this](AsyncWebServerRequest* req) {
            if (!_espalexa.handleAlexaApiCall(req))
                req->send(404);
        });

        // begin(&server) registers the onRequestBody handler (stores body
        // internally so handleAlexaApiCall(req) can read it) and onNotFound.
        _espalexa.begin(&server);
    }

    void loop() {
        _espalexa.loop();
    }
};
