#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

/* =============================
   WiFi Configuration Manager
   ============================= */
class WiFiConfigManager
{
public:
    String deviceName = "semaphore";
    String ntpServer  = "pool.ntp.org";
    String timezone   = "CET-1CEST,M3.5.0,M10.5.0/3";
    String wifiSSID;
    String wifiPassword;
    bool  dhcp      = true;
    IPAddress localIP;
    IPAddress subnet;
    IPAddress gateway;
    IPAddress dns;

    /* =============================
       LOAD CONFIG
       ============================= */
    bool loadConfig()
    {
        Serial.println("Loading WiFi config...");
        if (!LittleFS.exists("/wifi.json"))
            return false;

        File file = LittleFS.open("/wifi.json", "r");
        if (!file)
            return false;

        JsonDocument doc;

        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error)
            return false;

        deviceName   = doc["deviceName"] | "semaphore";
        ntpServer    = doc["ntpServer"]  | "pool.ntp.org";
        timezone     = doc["timezone"]   | "CET-1CEST,M3.5.0,M10.5.0/3";
        wifiSSID     = doc["ssid"]     | "";
        wifiPassword = doc["password"] | "";
        dhcp         = doc["dhcp"]     | true;
        Serial.println("WiFi config loaded:");
        Serial.printf("SSID: %s\n", wifiSSID.c_str());
        Serial.printf("DHCP: %s\n", dhcp ? "true" : "false");
        
        if (!dhcp)
        {
            localIP.fromString(doc["ip"]      | "");
            subnet.fromString(doc["subnet"]   | "");
            gateway.fromString(doc["gateway"] | "");
            dns.fromString(doc["dns"]        | "");
            Serial.printf("IP: %s\n", localIP.toString().c_str());
            Serial.printf("Subnet: %s\n", subnet.toString().c_str());
            Serial.printf("Gateway: %s\n", gateway.toString().c_str());
            Serial.printf("DNS: %s\n", dns.toString().c_str());
        }

        return wifiSSID.length() > 0;
    }

    /* =============================
       SAVE CONFIG
       ============================= */
    void saveConfig(String name,
                    String ntp,
                    String tz,
                    String ssid,
                    String password,
                    bool dhcpMode,
                    IPAddress ip,
                    IPAddress subnetMask,
                    IPAddress gatewayAddr,
                    IPAddress dnsAddr)
    {
        JsonDocument doc;

        doc["deviceName"] = name;
        doc["ntpServer"]  = ntp;
        doc["timezone"]   = tz;
        doc["ssid"]       = ssid;
        doc["password"]   = password;
        doc["dhcp"]       = dhcpMode;

        if (!dhcpMode)
        {
            doc["ip"]      = ip.toString();
            doc["subnet"]  = subnetMask.toString();
            doc["gateway"] = gatewayAddr.toString();
            doc["dns"]     = dnsAddr.toString();
        }

        File file = LittleFS.open("/wifi.json", "w");
        serializeJsonPretty(doc, file);
        file.close();

        // aggiorna variabili runtime
        deviceName   = name;
        ntpServer    = ntp;
        timezone     = tz;
        wifiSSID     = ssid;
        wifiPassword = password;
        dhcp         = dhcpMode;
        localIP      = ip;
        subnet       = subnetMask;
        gateway      = gatewayAddr;
        dns          = dnsAddr;
    }

    /* =============================
       APPLY CONFIG
       ============================= */
    void applyConfig()
    {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(deviceName.c_str());

        if (!dhcp)
        {
            WiFi.config(localIP, gateway, subnet, dns);
        }

        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    }
};