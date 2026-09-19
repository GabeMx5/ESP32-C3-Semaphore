#pragma once
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <string.h>
#include "wifiConfigManager.h"

#define MAX_WIFI_RETRY          3
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_RETRY_EXTRA_MS     5000    // each retry waits this much longer
#define AP_MODE_TIMEOUT_MS      600000  // 10 minutes, paused while a client is on the AP

// Last STA disconnect reason seen by the event handler, kept here so the
// console and the web UI can explain a failure instead of just reporting one.
static volatile uint8_t g_lastWifiReason = 0;

// Numeric reason codes rather than the WIFI_REASON_* names: the enum has been
// renamed across IDF releases, the numbers have not.
static const char* wifiReasonToString(uint8_t reason)
{
    switch (reason)
    {
        case 0:   return "none";
        case 1:   return "unspecified";
        case 2:   return "auth expired";
        case 4:   return "association expired";
        case 5:   return "AP is full";
        case 6:   return "not authenticated";
        case 7:   return "not associated";
        case 8:   return "association left";
        case 15:  return "4-way handshake timeout - wrong password";
        case 16:  return "group key update timeout";
        case 17:  return "IE differs in 4-way - wrong password";
        case 18:  return "group cipher mismatch";
        case 19:  return "pairwise cipher mismatch";
        case 20:  return "AKMP invalid";
        case 23:  return "802.1X auth failed";
        case 24:  return "cipher suite rejected";
        case 200: return "beacon timeout - signal too weak";
        case 201: return "AP not found - wrong SSID or out of range";
        case 202: return "auth failed - wrong password";
        case 203: return "association failed";
        case 204: return "handshake timeout";
        case 205: return "connection failed";
        default:  return "see esp_wifi_types.h";
    }
}

static const char* wifiAuthToString(int mode)
{
    switch (mode)
    {
        case WIFI_AUTH_OPEN:          return "open";
        case WIFI_AUTH_WEP:           return "WEP";
        case WIFI_AUTH_WPA_PSK:       return "WPA";
        case WIFI_AUTH_WPA2_PSK:      return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK:      return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default:                      return "?";
    }
}

// Arduino only logs WiFi events when the core debug level is raised, which it
// is not in a release build. Without this a failed join prints a row of dots
// and nothing about the cause.
static void wifiEventLogger(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event)
    {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.printf("\n[WiFi] associated on channel %d\n",
                          info.wifi_sta_connected.channel);
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            g_lastWifiReason = 0;
            Serial.printf("\n[WiFi] got IP %s\n", WiFi.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            Serial.println("\n[WiFi] lost IP");
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        {
            uint8_t r = info.wifi_sta_disconnected.reason;
            g_lastWifiReason = r;
            Serial.printf("\n[WiFi] disconnected, reason %u: %s\n", r, wifiReasonToString(r));
            break;
        }
        default:
            break;
    }
}

class NetworkManager
{
private:
    int                retryCounter = 0;
    unsigned long      apStartTime  = 0;
    bool               apModeActive = false;
    WiFiConfigManager* wifiManager  = nullptr;

    /* =============================
       RADIO SETUP
       ============================= */
    // Brings the radio up in a known state before every join attempt. Without
    // the reset, a retried WiFi.begin() on top of a stale connection state can
    // silently fail or take much longer.
    void prepareRadio()
    {
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_STA);

        // Channels 12 and 13 are legal across most of the world, but the
        // default country setting only scans them passively — so an AP sitting
        // on 12 or 13 is invisible to the active scan that WiFi.begin()
        // performs, and the device reports "AP not found" for a network every
        // other device in the house can see. AUTO policy still adopts the AP's
        // own regulatory domain from its beacons once associated.
        wifi_country_t country = {};
        strncpy(country.cc, "01", sizeof(country.cc));
        country.cc[sizeof(country.cc) - 1] = '\0';
        country.schan        = 1;
        country.nchan        = 13;
        country.max_tx_power = 78;
        country.policy       = WIFI_COUNTRY_POLICY_AUTO;
        esp_wifi_set_country(&country);

        // Modem sleep saves a few mA and costs reliability: on the ESP32-C3 it
        // is a common cause of missed beacons and dropped associations.
        WiFi.setSleep(false);

        WiFi.setHostname(wifiManager->deviceName.c_str());
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) esp_netif_set_hostname(netif, wifiManager->deviceName.c_str());

        if (!wifiManager->dhcp)
        {
            WiFi.config(wifiManager->localIP,
                        wifiManager->gateway,
                        wifiManager->subnet,
                        wifiManager->dns);
        }
    }

    bool waitForConnection(unsigned long timeoutMs)
    {
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs)
        {
            delay(500);
            Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.printf("\nWiFi Connected! IP %s, channel %d, %d dBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
            return true;
        }
        Serial.printf("\nWiFi Connection Failed (last reason %u: %s)\n",
                      g_lastWifiReason, wifiReasonToString(g_lastWifiReason));
        return false;
    }

public:

    /* =============================
       CONNECT TO WIFI
       ============================= */
    bool connectToWiFi(unsigned long timeoutMs = WIFI_CONNECT_TIMEOUT_MS)
    {
        Serial.println("Connecting to WiFi...");
        // Lengths only — never print the password itself. A zero here means the
        // credentials were saved empty, which otherwise looks identical to a
        // wrong password.
        Serial.printf("[WiFi] SSID '%s' (%u chars), password %u chars, hostname '%s', dhcp %s\n",
                      wifiManager->wifiSSID.c_str(),
                      (unsigned)wifiManager->wifiSSID.length(),
                      (unsigned)wifiManager->wifiPassword.length(),
                      wifiManager->deviceName.c_str(),
                      wifiManager->dhcp ? "yes" : "no");

        prepareRadio();
        WiFi.begin(wifiManager->wifiSSID.c_str(), wifiManager->wifiPassword.c_str());
        return waitForConnection(timeoutMs);
    }

    /* =============================
       SCAN-ASSISTED CONNECT
       ============================= */
    // Last resort: find the AP ourselves and join it by explicit channel and
    // BSSID. This works where a plain begin() does not — an AP the active scan
    // skipped, a hidden SSID, or several APs sharing one SSID where the driver
    // keeps picking an unreachable one.
    bool connectViaScan()
    {
        Serial.println("[WiFi] retrying with an explicit channel and BSSID...");
        prepareRadio();
        int n = WiFi.scanNetworks(false, true);
        if (n <= 0)
        {
            Serial.println("[WiFi] scan found nothing at all");
            WiFi.scanDelete();
            return false;
        }

        int best = -1;
        for (int i = 0; i < n; i++)
        {
            if (WiFi.SSID(i) != wifiManager->wifiSSID) continue;
            if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) best = i;
        }
        if (best < 0)
        {
            Serial.printf("[WiFi] '%s' is not among the %d networks in range\n",
                          wifiManager->wifiSSID.c_str(), n);
            WiFi.scanDelete();
            return false;
        }

        uint8_t bssid[6];
        memcpy(bssid, WiFi.BSSID(best), 6);
        int32_t channel = WiFi.channel(best);
        Serial.printf("[WiFi] found on channel %d at %d dBm (%s), joining directly\n",
                      (int)channel, WiFi.RSSI(best), wifiAuthToString(WiFi.encryptionType(best)));
        WiFi.scanDelete();

        WiFi.begin(wifiManager->wifiSSID.c_str(), wifiManager->wifiPassword.c_str(),
                   channel, bssid, true);
        return waitForConnection(WIFI_CONNECT_TIMEOUT_MS + WIFI_RETRY_EXTRA_MS);
    }

    /* =============================
       DIAGNOSTICS
       ============================= */
    // Lists what the radio can actually hear. The channel column is what
    // separates "wrong password" from "AP never seen" from "no RF at all".
    void logVisibleNetworks()
    {
        Serial.println("[WiFi] scanning for visible networks...");
        int n = WiFi.scanNetworks(false, true);
        if (n <= 0)
        {
            Serial.println("[WiFi] scan found nothing at all - antenna or RF problem");
            WiFi.scanDelete();
            return;
        }
        bool found = false;
        for (int i = 0; i < n; i++)
        {
            bool match = (WiFi.SSID(i) == wifiManager->wifiSSID);
            if (match) found = true;
            Serial.printf("[WiFi] %c %-24s ch %-3d %4d dBm  %s\n",
                          match ? '>' : ' ',
                          WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "<hidden>",
                          WiFi.channel(i), WiFi.RSSI(i),
                          wifiAuthToString(WiFi.encryptionType(i)));
        }
        if (!found)
            Serial.printf("[WiFi] '%s' is NOT among the %d networks in range\n",
                          wifiManager->wifiSSID.c_str(), n);
        WiFi.scanDelete();
    }

    uint8_t     getLastFailReason() { return g_lastWifiReason; }
    const char* getLastFailText()   { return wifiReasonToString(g_lastWifiReason); }

    /* =============================
       START ACCESS POINT
       ============================= */
    void startAccessPoint()
    {
        Serial.println("Starting Access Point mode...");

        // On ESP32-C3, switching from a failed STA state to AP without
        // first resetting the WiFi peripheral causes softAP() to silently
        // fail — the AP is never visible to any device.
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.mode(WIFI_AP);

        IPAddress apIP(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);

        WiFi.softAPConfig(apIP, gateway, subnet);
        WiFi.softAP("Semaphore");

        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
        Serial.printf("[WiFi] last STA failure was reason %u: %s\n",
                      g_lastWifiReason, wifiReasonToString(g_lastWifiReason));

        apModeActive = true;
        apStartTime  = millis();
    }

    /* =============================
       HANDLE FALLBACK
       ============================= */
    void handleFallbackLogic()
    {
        if (!apModeActive) return;

        // Rebooting out from under somebody who is typing their password is
        // the worst possible moment, so the timeout only runs while nobody is
        // connected to the access point.
        if (WiFi.softAPgetStationNum() > 0)
        {
            apStartTime = millis();
            return;
        }

        if (millis() - apStartTime > AP_MODE_TIMEOUT_MS)
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
        WiFi.onEvent(wifiEventLogger);

        if (wifiManager->loadConfig())
        {
            Serial.println("WiFi config loaded.");

            retryCounter = 0;
            while (retryCounter < MAX_WIFI_RETRY)
            {
                // Each attempt waits longer: routers with band steering, or a
                // slow DHCP server, regularly need more than ten seconds.
                unsigned long timeout = WIFI_CONNECT_TIMEOUT_MS +
                                        (unsigned long)retryCounter * WIFI_RETRY_EXTRA_MS;
                if (connectToWiFi(timeout))
                {
                    apModeActive = false;
                    return;
                }
                retryCounter++;
                Serial.printf("Retry %d/%d\n", retryCounter, MAX_WIFI_RETRY);
            }

            if (connectViaScan())
            {
                apModeActive = false;
                return;
            }

            // Record what the radio can hear before giving up.
            logVisibleNetworks();
        }
        else
        {
            Serial.println("No WiFi config found.");
        }

        startAccessPoint();
    }

    /* =============================
       STATUS HELPERS
       ============================= */
    bool isAPMode()    { return apModeActive; }
    bool isConnected() { return WiFi.status() == WL_CONNECTED; }
};
