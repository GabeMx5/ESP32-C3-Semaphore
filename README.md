# ESP32-C3 Semaphore

A smart RGB traffic light based on the **Seeed XIAO ESP32-C3**, controllable via browser with a PWA web interface, MQTT/Home Assistant integration, and scheduled timers.

---

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32-C3 with built-in SSD1306 OLED 128×64 |
| LED | 3× WS2812B (NeoPixel) on GPIO 10 |
| Storage | LittleFS on internal flash |

The three LEDs are arranged vertically: **top = LED 2**, **middle = LED 1**, **bottom = LED 0**.

---

## Features

### LED Control
- Individual RGB color per LED
- ON / OFF / BLINK state (500 ms blink interval)
- Automatic configuration save to flash with 10 s debounce

### Effects (FX)

| Effect | Description |
|---|---|
| **Cycle** | Activates LEDs sequentially top→middle→bottom with configurable durations |
| **Party** | Random flashing on all LEDs with adjustable "madness" intensity (1–10) |
| **Rainbow** | Continuous HSV spectrum scroll with configurable cycle time |
| **Random Yes/No** | Middle LED blinks with increasing speed and reveals green (yes) or red (no) |
| **Guess** | Game: the user picks a LED, a 24-step 500 ms→50 ms animation runs, then the firmware reveals the winning LED |
| **Morse Code** | Flashes all LEDs in Morse code for any text entered by the user (A–Z, spaces supported) |

Effects are mutually exclusive: enabling one automatically disables the others.

### Timers
- Up to 50 timers with weekly scheduling (Monday–Sunday)
- Available actions: `all_off`, `led0/1/2` (with RGB color), `cycle`, `party`, `rainbow`
- **Optional duration** in seconds: when elapsed, the effect is automatically stopped (0 = no limit)
- Execution based on RTC via NTP

### Connectivity
- **WiFi** STA with DHCP or static IP; fallback to AP mode (`192.168.4.1`) after 3 failed attempts
- **mDNS** with configurable hostname (default `semaphore.local`)
- **WebSocket** real-time with application-level ping/pong (3 s interval, 2 s timeout)
- **OTA** (ArduinoOTA) for wireless firmware updates
- **MQTT** optional with Home Assistant auto-discovery support

### OLED Display
- Shows status messages at boot and operation feedback
- Auto-sleep after 10 s of inactivity

---

## Web Interface (PWA)

Accessible from a browser at `http://<ip>` or `http://semaphore.local`. Works as a Progressive Web App installable on iOS and Android.

### LED Tab
![LED](screenshots/LED.png)

Direct control of the three LEDs with color picker, ON/OFF and BLINK toggles. Real-time SVG representation of the traffic light.

### FX Tab
![FX](screenshots/FX.png)

Enable effects with configurable parameters:
- Cycle phase durations
- Party madness level
- Rainbow cycle time
- **Random Yes/No** button with dice animation
- **Guess** button with LED selection, spinning animation during the game, and WINNER/LOOSER card with dedicated icons
- **Morse Code** button: enter any text and all LEDs flash the message in Morse code

### TIMER Tab
![TIMER](screenshots/TIMER.png)

Add, edit and delete timers with day selection, time (HH:MM:SS), action and duration in seconds (0 = no limit). Persistent save to device.

### WIFI Tab
![WIFI](screenshots/WIFI.png)

Configure device name, NTP server, timezone, WiFi credentials and static IP. Saving restarts the device.

### MQTT Tab
![MQTT](screenshots/MQTT.png)

Configure broker, port, credentials, client ID and topic prefix. Real-time connection status.

### INFO Tab
![INFO](screenshots/INFO.png)

System diagnostics: IP, SSID, RSSI, free heap, uptime, MQTT status, MAC address, CPU frequency, chip model, WiFi channel. Includes:
- **Make changes persistent** toggle: when disabled, LED and effect changes are not written to flash (useful for temporary configurations)
- **Backup** button: downloads a `semaphore-backup.json` file containing `config.json`, `wifi.json` and `mqtt.json`
- **Restore** button: uploads a backup file and automatically reboots the device to apply changes

---

## MQTT / Home Assistant

The device automatically publishes discovery topics for Home Assistant:
- **Lights**: state and color/brightness control for each LED
- **Switches**: cycle, party, rainbow

Command topic: `{topicPrefix}/cmd` (JSON format, same protocol as WebSocket).

---

## Data Persistence

| File | Content |
|---|---|
| `/config.json` | LED colors, states and effect parameters |
| `/wifi.json` | WiFi credentials and IP configuration |
| `/mqtt.json` | MQTT broker configuration |
| `/timers.json` | Timer definitions |

---

## Wiring

### Components
- ESP32-C3 with built-in SSD1306 OLED display
- 3× WS2812B LEDs (or a strip of 3)

### Diagram

```
                    ┌─────────────────────┐
                    │     ESP32-C3        │
                    │   (built-in OLED)   │
                    │                     │
               5V  ─┤ 5V              GND ├─ GND
                    │                     │
                    │         D10 (GPIO10)├──────────┐
                    └─────────────────────┘          │
                                                     │
          ┌──────────────────────────────────────────┘
          │         WS2812B chain
          │
          ▼
    ┌──────────┐      ┌──────────┐      ┌──────────┐
    │  LED 2   │      │  LED 1   │      │  LED 0   │
    │  (top)   ├─DO──►│ (middle) ├─DO──►│ (bottom) │
    │ DIN VCC GND    │ DIN VCC GND    │ DIN VCC GND
    └──┬───┬───┘      └──────────┘      └──────────┘
       │   │
      5V  GND
```

### Pin Summary

| Signal | GPIO |
|---|---|
| LED data | GPIO 10 |
| Power (LEDs) | 5V |

---

## Build & Flash

The project uses **PlatformIO**.

```bash
# Build and flash via USB
pio run --target upload

# Upload filesystem (web UI)
pio run --target uploadfs

# OTA (after first flash)
# Enable upload_protocol = espota in platformio.ini
```

### Main Libraries

- `Adafruit NeoPixel`
- `ESPAsyncWebServer` + `AsyncTCP`
- `ArduinoJson`
- `U8g2`
- `PubSubClient`

---

## Project Structure

```
├── src/
│   ├── main.cpp              # Entry point, WebSocket, MQTT, HTTP routing
│   ├── ledController.h       # LED effects and games
│   ├── timerController.h     # Weekly scheduler
│   ├── mqttController.h      # MQTT client + HA discovery
│   ├── networkManager.h      # WiFi STA/AP fallback
│   ├── configController.h    # Load/save config.json, dirty flag
│   ├── wifiConfigManager.h   # Network configuration persistence
│   └── monitorController.h   # OLED display
└── data/                     # Web UI (LittleFS)
    ├── index.html
    ├── index.js
    ├── index.css
    └── manifest.json
```
