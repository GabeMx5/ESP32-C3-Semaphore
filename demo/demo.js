// ─── Demo Mode ────────────────────────────────────────────────────────────────
// Replaces WebSocket with a browser-side simulation of the ESP32 firmware.
// No device needed — all state lives in memory.

(function () {

  // ─── Simulated device state ─────────────────────────────────────────────────

  const state = {
    leds: [
      { r: 255, g: 0,   b: 0,   on: true,  blink: false }, // LED 0 bottom — red
      { r: 255, g: 165, b: 0,   on: true,  blink: false }, // LED 1 middle — orange
      { r: 0,   g: 255, b: 0,   on: true,  blink: false }, // LED 2 top    — green
    ],
    cycle: false, topLedTime: 5, middleLedTime: 5, bottomLedTime: 5,
    party: false, partyMadness: 5,
    rainbow: false, rainbowCycleTime: 5,
    makeChangesPersistent: true,
    latitude: 45.4654, longitude: 9.1859, // Milan
    wifi: {
      deviceName: "semaphore", ntpServer: "pool.ntp.org",
      timezone: "CET-1CEST,M3.5.0,M10.5.0/3",
      ssid: "DemoWiFi", password: "••••••••", dhcp: true,
    },
    mqtt: {
      broker: "", port: 1883, username: "", password: "",
      clientId: "semaphore", topic: "semaphore",
      enabled: false, connected: false,
    },
    bambu: {
      ip: "", serial: "", accessCode: "",
      enabled: false, connected: false,
      state: "unknown", bambuMode: false, idleTimeoutMin: 5,
      _idleSince: 0,
    },
    timers: [],
    uptime: 0,
    // tab tracking
    consoleOpen: false, infoOpen: false, bambuOpen: false,
  };

  // ─── Helpers ─────────────────────────────────────────────────────────────────

  function cancelEffects() {
    state.cycle = false;
    state.party = false;
    state.rainbow = false;
  }

  function defaultStateColors() {
    return {
      unknown:  [0,0,0,   0,0,0,     0,0,0],
      idle:     [0,255,0, 0,0,0,     0,0,0],
      prepare:  [0,0,0,   0,0,0,     255,0,0],
      running:  [0,0,0,   255,165,0, 0,0,0],
      paused:   [0,0,0,   0,0,0,     255,0,0],
      finished: [0,255,0, 0,0,0,     0,0,0],
      failed:   [0,0,0,   0,0,0,     255,0,0],
      init:     [0,0,0,   0,0,0,     255,0,0],
      slicing:  [0,0,0,   0,0,0,     255,0,0],
      offline:  [0,0,0,   0,0,0,     0,0,0],
    };
  }

  function buildLedStatus()     { return { type: "ledStatus", leds: state.leds }; }
  function buildCycleStatus()   { return { type: "cycleStatus",   cycle: state.cycle,   topLedTime: state.topLedTime, middleLedTime: state.middleLedTime, bottomLedTime: state.bottomLedTime }; }
  function buildPartyStatus()   { return { type: "partyStatus",   party: state.party,   partyMadness: state.partyMadness }; }
  function buildRainbowStatus() { return { type: "rainbowStatus", rainbow: state.rainbow, rainbowCycleTime: state.rainbowCycleTime }; }

  function buildBambuConfig() {
    return {
      type: "bambuConfig",
      ip: state.bambu.ip, serial: state.bambu.serial, accessCode: state.bambu.accessCode,
      enabled: state.bambu.enabled, connected: state.bambu.connected,
      state: state.bambu.state, bambuMode: state.bambu.bambuMode,
      idleTimeoutMin: state.bambu.idleTimeoutMin,
      stateColors: defaultStateColors(),
    };
  }

  function buildSysInfoStatic() {
    const msg = {
      type: "sysInfoStatic",
      version: "1.0.2",
      ip: "192.168.1.42", ssid: "DemoWiFi",
      mac: "AA:BB:CC:DD:EE:FF",
      cpuFreq: 160, chipModel: "ESP32-C3", chipRevision: 3,
      wifiChannel: 6,
      mqttBroker: state.mqtt.broker,
      latitude: state.latitude, longitude: state.longitude,
      // Simulated Milan weather: Clear, 18 °C, 62 % RH
      weatherCode: 1, weatherTemp: 18.5, weatherHumidity: 62,
      weatherCondition: 1, // CLEAR
      temperatureR: 63,  temperatureG: 255, temperatureB: 0,   // ~green-yellow for 18 °C
      conditionR: 255,   conditionG: 220,   conditionB: 0,     // yellow for clear day
      humidityR: 0,      humidityG: 210,    humidityB: 100,    // green-teal for 62 %
      // Simulated air quality: good
      aqPm25: 8.5,  aqPm10: 12.3, aqNo2: 25.6,
      aqPm25R: 0, aqPm25G: 200, aqPm25B: 80,
      aqPm10R: 0, aqPm10G: 200, aqPm10B: 80,
      aqNo2R:  0, aqNo2G:  200, aqNo2B:  80,
    };
    return msg;
  }

  function buildSysInfo() {
    return {
      type: "sysInfo",
      rssi: -52 - Math.floor(Math.random() * 10),
      freeHeap: 182000 + Math.floor(Math.random() * 8000 - 4000),
      datetime: new Date().toISOString().replace("T", " ").slice(0, 19),
      uptime: state.uptime,
      bambuConnected: state.bambu.connected,
      mqttConnected: state.mqtt.connected,
    };
  }

  // Simulated printer states for the BambuLab demo sequence
  const BAMBU_DEMO_STATES = ["idle", "prepare", "running", "running", "running", "paused", "running", "finished", "idle"];
  let _bambuDemoIndex = 0;
  let _bambuDemoTimer = null;

  function startBambuDemoSequence(sock) {
    _bambuDemoIndex = 0;
    clearTimeout(_bambuDemoTimer);
    function next() {
      if (!state.bambu.connected) return;
      const s = BAMBU_DEMO_STATES[_bambuDemoIndex % BAMBU_DEMO_STATES.length];
      state.bambu.state = s;
      if (s === "idle" || s === "finished") {
        if (!state.bambu._idleSince) state.bambu._idleSince = Date.now();
      } else {
        state.bambu._idleSince = 0;
      }
      sock._push({ type: "bambuStatus", state: s, connected: true });
      _bambuDemoIndex++;
      _bambuDemoTimer = setTimeout(next, 6000);
    }
    _bambuDemoTimer = setTimeout(next, 2000);
  }

  // ─── Mock WebSocket ──────────────────────────────────────────────────────────

  class MockWebSocket {
    constructor(url) {
      this.readyState = WebSocket.CONNECTING;
      this._listeners = {};
      this._intervals = [];
      setTimeout(() => {
        this.readyState = WebSocket.OPEN;
        this._fire("open", new Event("open"));
        this._sendConnectMessages();
        this._startLoops();
      }, 400);
    }

    addEventListener(type, fn) {
      if (!this._listeners[type]) this._listeners[type] = [];
      this._listeners[type].push(fn);
    }

    _fire(type, event) {
      (this._listeners[type] || []).forEach(fn => fn(event));
    }

    _push(obj) {
      this._fire("message", new MessageEvent("message", { data: JSON.stringify(obj) }));
    }

    send(raw) {
      let msg;
      try { msg = JSON.parse(raw); } catch { return; }
      this._handle(msg);
    }

    close() {
      this.readyState = WebSocket.CLOSED;
      this._intervals.forEach(clearInterval);
      clearTimeout(_bambuDemoTimer);
    }

    _sendConnectMessages() {
      this._push(buildLedStatus());
      this._push(buildCycleStatus());
      this._push(buildPartyStatus());
      this._push(buildRainbowStatus());
      this._push({ type: "wifiConfig",   ...state.wifi });
      this._push({ type: "mqttConfig",   ...state.mqtt });
      this._push(buildBambuConfig());
      this._push({ type: "timerConfig",  timers: state.timers });
      this._push({ type: "configStatus", makeChangesPersistent: state.makeChangesPersistent, latitude: state.latitude, longitude: state.longitude });
      this._push(buildSysInfoStatic());
    }

    _startLoops() {
      // Uptime counter
      this._intervals.push(setInterval(() => { state.uptime++; }, 1000));

      // sysInfo push to info/settings viewers
      this._intervals.push(setInterval(() => {
        if (state.infoOpen) this._push(buildSysInfo());
      }, 1000));

      // bambuConfig.idleSec push to bambu/info viewers
      this._intervals.push(setInterval(() => {
        if (state.bambu.connected && state.bambu._idleSince > 0) {
          const idleSec = Math.floor((Date.now() - state.bambu._idleSince) / 1000);
          if (state.bambuOpen || state.infoOpen)
            this._push({ type: "bambuConfig", idleSec });
        }
      }, 1000));

      // Simulated console output
      const log = [
        "[WiFi] Connected to DemoWiFi — IP: 192.168.1.42",
        "[mDNS] semaphore.local",
        "[MQTT] disabled",
        "[Geo] Fetching weather for 45.4654, 9.1859...",
        "[Geo] Weather OK — Clear 18.5 °C 62 %",
        "[Geo] Air quality OK — PM2.5 8.5  PM10 12.3  NO2 25.6",
        "[BambuLab] disabled",
        "[Timer] 0 timers loaded",
        "[WebSocket] client #1 connected from 192.168.1.100",
      ];
      let ci = 0;
      this._intervals.push(setInterval(() => {
        if (state.consoleOpen && ci < log.length)
          this._push({ type: "console", text: log[ci++] });
      }, 700));
    }

    _handle(msg) {
      const push = this._push.bind(this);
      switch (msg.type) {

        // ── Tab tracking ────────────────────────────────────────────────────────
        case "consoleOpen":  state.consoleOpen = true;  break;
        case "consoleClose": state.consoleOpen = false; break;
        case "infoOpen":     state.infoOpen    = true;  push(buildSysInfo()); break;
        case "infoClose":    state.infoOpen    = false; break;
        case "bambuOpen":    state.bambuOpen   = true;  break;
        case "bambuClose":   state.bambuOpen   = false; break;

        // ── LED ─────────────────────────────────────────────────────────────────
        case "setLed": {
          const { led, r, g, b, on, blink } = msg;
          if (led >= 0 && led < 3) Object.assign(state.leds[led], { r, g, b, on, blink });
          push(buildLedStatus());
          break;
        }

        // ── Effects ─────────────────────────────────────────────────────────────
        case "setCycle":
          if (msg.cycle) cancelEffects();
          state.cycle = msg.cycle ?? false;
          state.topLedTime    = msg.topLedTime    ?? state.topLedTime;
          state.middleLedTime = msg.middleLedTime ?? state.middleLedTime;
          state.bottomLedTime = msg.bottomLedTime ?? state.bottomLedTime;
          push(buildCycleStatus()); push(buildPartyStatus()); push(buildRainbowStatus());
          break;

        case "setParty":
          if (msg.party) cancelEffects();
          state.party = msg.party ?? false;
          state.partyMadness = msg.partyMadness ?? state.partyMadness;
          push(buildPartyStatus()); push(buildCycleStatus()); push(buildRainbowStatus());
          break;

        case "setRainbow":
          if (msg.rainbow) cancelEffects();
          state.rainbow = msg.rainbow ?? false;
          state.rainbowCycleTime = msg.rainbowCycleTime ?? state.rainbowCycleTime;
          push(buildRainbowStatus()); push(buildCycleStatus()); push(buildPartyStatus());
          break;

        case "startGuess":
          cancelEffects();
          push(buildCycleStatus()); push(buildPartyStatus()); push(buildRainbowStatus());
          setTimeout(() => push({ type: "guessResult", winner: Math.floor(Math.random() * 3) }), 3000);
          break;

        case "randomYesNo":
          cancelEffects();
          push(buildCycleStatus()); push(buildPartyStatus()); push(buildRainbowStatus());
          break;

        case "morse":
        case "weatherColor":
        case "airQualityColor":
          cancelEffects();
          push(buildCycleStatus()); push(buildPartyStatus()); push(buildRainbowStatus());
          break;

        // ── Config ──────────────────────────────────────────────────────────────
        case "setConfig":
          state.makeChangesPersistent = msg.makeChangesPersistent ?? true;
          push({ type: "configStatus", makeChangesPersistent: state.makeChangesPersistent, latitude: state.latitude, longitude: state.longitude });
          break;

        case "setLocation":
          state.latitude  = msg.latitude  ?? state.latitude;
          state.longitude = msg.longitude ?? state.longitude;
          break;

        // ── WiFi ────────────────────────────────────────────────────────────────
        case "getWifi":
          push({ type: "wifiConfig", ...state.wifi });
          break;

        case "setWifi":
          // Don't actually reboot in demo
          push({ type: "status", status: "saved", reboot: false });
          break;

        // ── MQTT ────────────────────────────────────────────────────────────────
        case "getMqtt":
          push({ type: "mqttConfig", ...state.mqtt });
          break;

        case "setMqtt":
          Object.assign(state.mqtt, {
            broker: msg.broker ?? "", port: msg.port ?? 1883,
            username: msg.username ?? "", password: msg.password ?? "",
            clientId: msg.clientId ?? "semaphore", topic: msg.topic ?? "semaphore",
            enabled: msg.enabled ?? false,
          });
          if (state.mqtt.enabled && state.mqtt.broker) {
            setTimeout(() => {
              state.mqtt.connected = true;
              push({ type: "mqttConfig", ...state.mqtt });
            }, 1200);
          } else {
            state.mqtt.connected = false;
          }
          push({ type: "mqttConfig", ...state.mqtt });
          break;

        // ── BambuLab ────────────────────────────────────────────────────────────
        case "getBambu":
          push(buildBambuConfig());
          break;

        case "setBambu":
          state.bambu.ip = msg.ip ?? "";
          state.bambu.serial = msg.serial ?? "";
          state.bambu.accessCode = msg.accessCode ?? "";
          state.bambu.enabled = msg.enabled ?? false;
          state.bambu.connected = false;
          state.bambu.state = "unknown";
          state.bambu._idleSince = 0;
          clearTimeout(_bambuDemoTimer);
          if (state.bambu.enabled && state.bambu.ip) {
            setTimeout(() => {
              state.bambu.connected = true;
              state.bambu.state = "idle";
              state.bambu._idleSince = Date.now();
              push({ type: "bambuConfig", connected: true });
              push({ type: "bambuStatus", state: "idle", connected: true });
              startBambuDemoSequence(this);
            }, 1500);
          }
          push(buildBambuConfig());
          break;

        case "setBambuMode": {
          const mode = msg.bambuMode ?? false;
          state.bambu.bambuMode = mode;
          state.bambu.idleTimeoutMin = msg.idleTimeoutMin ?? state.bambu.idleTimeoutMin;
          if (mode) cancelEffects();
          push({ type: "bambuConfig", bambuMode: mode });
          push(buildBambuConfig());
          push(buildCycleStatus()); push(buildPartyStatus()); push(buildRainbowStatus());
          break;
        }

        case "setBambuStateColor":
          // Acknowledge silently — colour tracking omitted in demo
          break;

        // ── Timers ──────────────────────────────────────────────────────────────
        case "setTimer": {
          const idx = state.timers.findIndex(t => t.id === msg.id);
          if (idx >= 0) state.timers[idx] = msg; else state.timers.push(msg);
          push({ type: "timerConfig", timers: state.timers });
          break;
        }
        case "deleteTimer":
          state.timers = state.timers.filter(t => t.id !== msg.id);
          push({ type: "timerConfig", timers: state.timers });
          break;
      }
    }
  }

  // Copy constants so index.js can read WebSocket.OPEN etc.
  MockWebSocket.CONNECTING = 0;
  MockWebSocket.OPEN       = 1;
  MockWebSocket.CLOSING    = 2;
  MockWebSocket.CLOSED     = 3;

  window.WebSocket = MockWebSocket;

  // Suppress automatic firmware update checks (no real device in demo)
  setTimeout(() => { window.checkFirmwareUpdate = function () {}; }, 0);

  // ─── Demo banner ─────────────────────────────────────────────────────────────
  const banner = document.createElement("div");
  banner.id = "demo-banner";
  banner.innerHTML = "⚠ Demo — simulated device, no hardware required";
  Object.assign(banner.style, {
    position: "fixed", top: "0", left: "0", right: "0", zIndex: "9999",
    background: "#ffaa00", color: "#000", textAlign: "center",
    fontSize: "12px", fontWeight: "600", padding: "5px 0",
    letterSpacing: "0.03em",
  });
  document.addEventListener("DOMContentLoaded", () => {
    document.body.prepend(banner);
    // Shift the app content down so the banner doesn't overlap
    const app = document.querySelector(".app-wrapper") || document.body.firstElementChild;
    if (app && app.id !== "demo-banner") app.style.paddingTop = "26px";
  });

})();
