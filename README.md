# ESP32-C3 Semaphore

Un semaforo RGB smart basato su **Seeed XIAO ESP32-C3**, controllabile via browser con interfaccia web PWA, integrazione MQTT/Home Assistant e timer schedulati.

---

## Hardware

| Componente | Dettaglio |
|---|---|
| MCU | Seeed XIAO ESP32-C3 (RISC-V) |
| LED | 3× WS2812B (NeoPixel) su GPIO 10 |
| Display | OLED SSD1306 128×64 I²C (SDA GPIO 5, SCL GPIO 6) |
| Storage | LittleFS su flash interna |

I tre LED sono disposti verticalmente: **top = LED 2**, **middle = LED 1**, **bottom = LED 0**.

---

## Funzionalità

### Controllo LED
- Colore RGB individuale per ciascun LED
- Stato ON / OFF / BLINK (lampeggio 500 ms)
- Salvataggio automatico della configurazione su flash con debounce 10 s

### Effetti (FX)

| Effetto | Descrizione |
|---|---|
| **Cycle** | Attiva i LED in sequenza top→middle→bottom con durate configurabili |
| **Party** | Flash casuali su tutti i LED con intensità "madness" regolabile (1–10) |
| **Rainbow** | Scorrimento continuo dello spettro HSV con tempo di ciclo configurabile |
| **Random Yes/No** | Il LED centrale lampeggia ad accelerazione crescente e rivela verde (sì) o rosso (no) |
| **Guess** | Gioco: l'utente sceglie un LED, parte un'animazione 24 step 500 ms→50 ms, poi il firmware rivela il LED vincente |

Gli effetti sono mutuamente esclusivi: attivarne uno disabilita automaticamente gli altri.

### Timer
- Fino a 50 timer con scheduling settimanale (lunedì–domenica)
- Azioni disponibili: `all_off`, `led0/1/2` (con colore RGB), `cycle`, `party`, `rainbow`
- **Durata opzionale** in secondi: allo scadere, l'effetto viene disattivato automaticamente (0 = nessun limite)
- Esecuzione basata su RTC via NTP

### Connettività
- **WiFi** STA con DHCP o IP statico; fallback in modalità AP (`192.168.4.1`) dopo 3 tentativi falliti
- **mDNS** con hostname configurabile (default `semaphore.local`)
- **WebSocket** real-time con ping/pong applicativo (3 s intervallo, 2 s timeout)
- **OTA** (ArduinoOTA) per aggiornamenti firmware senza cavi
- **MQTT** opzionale con supporto Home Assistant auto-discovery

### Display OLED
- Mostra messaggi di stato al boot e feedback delle operazioni
- Sleep automatico dopo 10 s di inattività

---

## Interfaccia Web (PWA)

Accessibile da browser all'indirizzo `http://<ip>` o `http://semaphore.local`. Funziona come Progressive Web App installabile su iOS e Android.

### Tab LED
Controllo diretto dei tre LED con color picker, toggle ON/OFF e BLINK. Rappresentazione SVG del semaforo aggiornata in tempo reale.

### Tab FX
Attivazione degli effetti con parametri configurabili:
- Tempi per fase del Cycle
- Livello di madness del Party
- Tempo di ciclo del Rainbow
- Pulsante **Random Yes/No** con animazione dado
- Pulsante **Guess** con selezione LED, animazione rotante durante il gioco e card WINNER/LOOSER con icone dedicate

### Tab TIMER
Aggiunta, modifica e cancellazione di timer con selezione giorni, orario (HH:MM:SS), azione e durata in secondi (0 = nessun limite). Salvataggio persistente su device.

### Tab INFO
Diagnostica di sistema e checkbox **Make changes persistent**: se disabilitato, le modifiche a LED ed effetti non vengono scritte su flash (utile per configurazioni temporanee).

### Tab WIFI
Configurazione di nome dispositivo, server NTP, timezone, credenziali WiFi e IP statico. Il salvataggio riavvia il dispositivo.

### Tab MQTT
Configurazione broker, porta, credenziali, client ID e topic prefix. Stato connessione in tempo reale.

### Tab INFO
Diagnostica: IP, SSID, RSSI, heap libero, uptime, stato MQTT, MAC address, frequenza CPU, chip model, canale WiFi. Include il toggle **Make changes persistent**.

---

## Connettività MQTT / Home Assistant

Il dispositivo pubblica automaticamente i topic di discovery per Home Assistant:
- **Luci**: stato e controllo colore/luminosità per ciascun LED
- **Switch**: cycle, party, rainbow

Topic di comando: `{topicPrefix}/cmd` (formato JSON, stesso protocollo WebSocket).

---

## Persistenza dati

| File | Contenuto |
|---|---|
| `/config.json` | Colori, stati e parametri effetti LED |
| `/wifi.json` | Credenziali WiFi e configurazione IP |
| `/mqtt.json` | Configurazione broker MQTT |
| `/timers.json` | Definizione timer |

---

## Build & Flash

Il progetto usa **PlatformIO**.

```bash
# Build e flash via USB
pio run --target upload

# Upload filesystem (web UI)
pio run --target uploadfs

# OTA (dopo primo flash)
# Abilitare upload_protocol = espota in platformio.ini
```

### Librerie principali

- `Adafruit NeoPixel`
- `ESPAsyncWebServer` + `AsyncTCP`
- `ArduinoJson`
- `U8g2`
- `PubSubClient`

---

## Struttura del progetto

```
├── src/
│   ├── main.cpp              # Entry point, WebSocket, MQTT, HTTP routing
│   ├── ledController.h       # Effetti LED e giochi
│   ├── timerController.h     # Scheduler settimanale
│   ├── mqttController.h      # Client MQTT + HA discovery
│   ├── networkManager.h      # WiFi STA/AP fallback
│   ├── wifiConfigManager.h   # Persistenza configurazione rete
│   └── monitorController.h   # Display OLED
└── data/                     # Web UI (LittleFS)
    ├── index.html
    ├── index.js
    ├── index.css
    └── manifest.json
```
