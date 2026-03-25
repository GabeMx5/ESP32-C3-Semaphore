#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <functional>

#define OTA_FW_URL  "https://github.com/GabeMx5/ESP32-C3-Semaphore/releases/latest/download/firmware.bin"
#define OTA_FS_URL  "https://github.com/GabeMx5/ESP32-C3-Semaphore/releases/latest/download/filesystem.bin"

static const char* OTA_KEYS[]  = { "config", "wifi", "mqtt", "timers" };
static const char* OTA_PATHS[] = { "/config.json", "/wifi.json", "/mqtt.json", "/timers.json" };
#define OTA_FILE_COUNT 4

class OTAController {
public:
    std::function<void(const char*)> onStatus;

    void start()
    {
        _notify("backup");
        _backupConfigs();

        _notify("filesystem");
        if (!_flashFs()) { _notify("error"); return; }

        _notify("restore");
        _restoreConfigs();

        _notify("firmware");
        _flashFirmware(); // auto-restart on success
        _notify("error");
    }

private:
    void _notify(const char* step)
    {
        if (onStatus) onStatus(step);
        delay(300);
    }

    void _backupConfigs()
    {
        Preferences prefs;
        prefs.begin("ota_bk", false);
        for (int i = 0; i < OTA_FILE_COUNT; i++) {
            if (!LittleFS.exists(OTA_PATHS[i])) continue;
            File f = LittleFS.open(OTA_PATHS[i], "r");
            if (!f) continue;
            size_t sz = f.size();
            uint8_t* buf = (uint8_t*)malloc(sz);
            if (!buf) { f.close(); continue; }
            f.read(buf, sz);
            f.close();
            prefs.putBytes(OTA_KEYS[i], buf, sz);
            free(buf);
            Serial.printf("[OTA] Backed up %s (%u bytes)\n", OTA_PATHS[i], sz);
        }
        prefs.end();
    }

    bool _flashFs()
    {
        LittleFS.end();
        WiFiClientSecure client;
        client.setInsecure();
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        t_httpUpdate_return ret = httpUpdate.updateSpiffs(client, OTA_FS_URL);
        Serial.printf("[OTA] FS result: %d — %s\n", ret, httpUpdate.getLastErrorString().c_str());
        LittleFS.begin();
        return ret == HTTP_UPDATE_OK;
    }

    void _restoreConfigs()
    {
        Preferences prefs;
        prefs.begin("ota_bk", true);
        for (int i = 0; i < OTA_FILE_COUNT; i++) {
            size_t sz = prefs.getBytesLength(OTA_KEYS[i]);
            if (sz == 0) continue;
            uint8_t* buf = (uint8_t*)malloc(sz);
            if (!buf) continue;
            prefs.getBytes(OTA_KEYS[i], buf, sz);
            File f = LittleFS.open(OTA_PATHS[i], "w");
            if (f) { f.write(buf, sz); f.close(); }
            free(buf);
            Serial.printf("[OTA] Restored %s (%u bytes)\n", OTA_PATHS[i], sz);
        }
        prefs.end();
    }

    void _flashFirmware()
    {
        WiFiClientSecure client;
        client.setInsecure();
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        httpUpdate.update(client, OTA_FW_URL);
        // auto-restart on success
    }
};
