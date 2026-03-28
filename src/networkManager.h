#pragma once
#include <WiFi.h>
#include <esp_netif.h>
#include "wifiConfigManager.h"

#define MAX_WIFI_RETRY 3
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define AP_MODE_TIMEOUT_MS 180000 // 3 minutes intelligent fallback

class NetworkManager
{
private:
    int retryCounter = 0;
    unsigned long apStartTime = 0;
    bool apModeActive = false;
    WiFiConfigManager* wifiManager;

public:

    /* =============================
       CONNECT TO WIFI
       ============================= */
    bool connectToWiFi()
    {
        Serial.println("Connecting to WiFi...");

        WiFi.mode(WIFI_STA);
        WiFi.setHostname(wifiManager->deviceName.c_str());
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) esp_netif_set_hostname(netif, wifiManager->deviceName.c_str());

        // Applica DHCP o IP statico
        if (!wifiManager->dhcp)
        {
            WiFi.config(
                wifiManager->localIP,
                wifiManager->gateway,
                wifiManager->subnet,
                wifiManager->dns
            );
        }

        WiFi.begin(
            wifiManager->wifiSSID.c_str(),
            wifiManager->wifiPassword.c_str()
        );

        unsigned long startAttemptTime = millis();

        while (WiFi.status() != WL_CONNECTED &&
               millis() - startAttemptTime < WIFI_CONNECT_TIMEOUT_MS)
        {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("\nWiFi Connected!");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            return true;
        }

        Serial.println("\nWiFi Connection Failed.");
        return false;
    }

    /* =============================
       START ACCESS POINT
       ============================= */
    void startAccessPoint()
    {
        Serial.println("Starting Access Point mode...");

        WiFi.mode(WIFI_AP);

        // Imposta IP AP fisso
        IPAddress apIP(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);

        WiFi.softAPConfig(apIP, gateway, subnet);
        WiFi.softAP("Semaphore");

        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());

        apModeActive = true;
        apStartTime = millis();
    }

    /* =============================
       HANDLE FALLBACK
       ============================= */
    void handleFallbackLogic()
    {
        // Se in AP e timeout scaduto → reboot automatico
        if (apModeActive &&
            millis() - apStartTime > AP_MODE_TIMEOUT_MS)
        {
            Serial.println("AP timeout reached. Rebooting...");
            ESP.restart();
        }
    }

    /* =============================
       BEGIN NETWORK
       ============================= */
    void begin(WiFiConfigManager& configManager) 
    {
        wifiManager = &configManager;
        Serial.println("Initializing Network Manager...");

        if (wifiManager->loadConfig())
        {
            Serial.println("WiFi config loaded.");

            if (wifiManager->apMode)
            {
                Serial.println("AP mode configured — starting Access Point.");
                startAccessPoint();
                return;
            }

            retryCounter = 0;

            while (retryCounter < MAX_WIFI_RETRY)
            {
                if (connectToWiFi())
                {
                    apModeActive = false;
                    return;
                }

                retryCounter++;
                Serial.printf("Retry %d/%d\n", retryCounter, MAX_WIFI_RETRY);
            }
        }
        else
        {
            Serial.println("No WiFi config found.");
        }

        // Se fallisce → AP Mode
        startAccessPoint();
    }

    /* =============================
       STATUS HELPERS
       ============================= */
    bool isAPMode()
    {
        return apModeActive;
    }

    bool isConnected()
    {
        return WiFi.status() == WL_CONNECTED;
    }
};