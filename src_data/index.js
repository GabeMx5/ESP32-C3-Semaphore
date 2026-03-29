
let ws;
let reconnectDelay = 1000;
let timers        = [];
let timerNextId   = 0;
let infoRefreshTimer = null;
const MAX_RECONNECT_DELAY = 30000;

const WS_MAX_CALLS_PER_SEC = 10;
const wsSendTimestamps = [];
let wsPendingPayload = null;
let wsPendingTimer = null;

function wsSend(payload) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  const now = Date.now();
  while (wsSendTimestamps.length > 0 && now - wsSendTimestamps[0] > 1000) {
    wsSendTimestamps.shift();
  }
  if (wsSendTimestamps.length < WS_MAX_CALLS_PER_SEC) {
    clearTimeout(wsPendingTimer);
    wsPendingTimer = null;
    wsPendingPayload = null;
    wsSendTimestamps.push(now);
    ws.send(JSON.stringify(payload));
  } else {
    wsPendingPayload = payload;
    if (!wsPendingTimer) {
      const delay = 1000 - (now - wsSendTimestamps[0]) + 1;
      wsPendingTimer = setTimeout(() => {
        wsPendingTimer = null;
        const p = wsPendingPayload;
        wsPendingPayload = null;
        wsSend(p);
      }, delay);
    }
  }
}

let pingInterval = null;
let pongTimeout  = null;

function startPing() {
  clearInterval(pingInterval);
  pingInterval = setInterval(() => {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    ws.send(JSON.stringify({ type: "ping" }));
    pongTimeout = setTimeout(() => { showOverlay(); ws.close(); }, 2000);
  }, 3000);
}

function stopPing() {
  clearInterval(pingInterval);
  clearTimeout(pongTimeout);
  pingInterval = null;
  pongTimeout  = null;
}

let reloadPoller = null;
let otaFirmwareFlashing = false;

function startReloadPoller() {
  if (reloadPoller) return;
  reloadPoller = setInterval(async () => {
    try {
      const res = await fetch('/ping', { cache: 'no-store' });
      if (res.ok) location.reload();
    } catch (_) {}
  }, 2000);
}

function stopReloadPoller() {
  clearInterval(reloadPoller);
  reloadPoller = null;
}

function showOverlay() {
  const overlay = document.getElementById("disconnected-overlay");
  const msg     = document.getElementById("reconnectMsg");
  if (overlay) overlay.classList.add("visible");
  if (msg) msg.textContent = "Reconnecting...";
  startReloadPoller();
}

function hideOverlay() {
  const overlay = document.getElementById("disconnected-overlay");
  if (overlay) overlay.classList.remove("visible");
  stopReloadPoller();
}

function connect() {
  ws = new WebSocket(`ws://${location.hostname}/ws`);

  ws.addEventListener("open", () => {
    console.log("WebSocket connected");
    reconnectDelay = 1000;
    hideOverlay();
    startPing();
    requestInfo();
  });

  ws.addEventListener("message", (event) => {
    const data = JSON.parse(event.data);
    if (data.type === "pong") {
      clearTimeout(pongTimeout);
      pongTimeout = null;
    } else if (data.type === "ledStatus") {
      onLedStatus(data.leds);
    } else if (data.type === "cycleStatus") {
      onCycleStatus(data);
    } else if (data.type === "partyStatus") {
      onPartyStatus(data);
    } else if (data.type === "rainbowStatus") {
      onRainbowStatus(data);
    } else if (data.type === "sysInfo") {
      onSysInfo(data);
    } else if (data.type === "mqttConfig") {
      onMqttConfig(data);
    } else if (data.type === "wifiConfig") {
      onWifiConfig(data);
    } else if (data.type === "timerConfig") {
      onTimerConfig(data);
    } else if (data.type === "guessResult") {
      onGuessResult(data);
    } else if (data.type === "configStatus") {
      document.getElementById("makeChangesPersistent").checked = data.makeChangesPersistent;
      if (data.latitude)  document.getElementById("latitude").value  = data.latitude;
      if (data.longitude) document.getElementById("longitude").value = data.longitude;
      updateLocationLabel();
    } else if (data.type === "otaStatus") {
      onOtaStatus(data.step);
    } else if (data.type === "otaProgress") {
      onOtaProgress(data.step, data.pct);
    } else if (data.type === "console") {
      appendConsoleLine(data.text);
    } else if (data.type === "status") {
      if (data.reboot) {
        document.getElementById("wifiSaveText").textContent = "Configuration saved. Rebooting...";
        setTimeout(closeRestoreOverlay, 2000);
      }
    }
  });

  ws.addEventListener("close", () => {
    stopPing();
    if (otaFirmwareFlashing) {
      document.getElementById("ota-reconnect-msg").style.display = "block";
      startReloadPoller();
      return;
    }
    console.warn(`WebSocket disconnected. Reconnecting in ${reconnectDelay / 1000}s...`);
    showOverlay();
    setTimeout(connect, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, MAX_RECONNECT_DELAY);
  });

  ws.addEventListener("error", () => {
    ws.close();
  });
}

connect();

// ─── LED blink ────────────────────────────────────────────────────────────────

const BLINK_INTERVAL_MS = 500;
const ledStatus = Array.from({ length: 3 }, () => ({ r: 0, g: 0, b: 0, on: false, blink: false }));
let blinkPhase = false;

setInterval(() => {
  blinkPhase = !blinkPhase;
  ledStatus.forEach((led, index) => {
    if (!led.blink) return;
    const ledCircle = document.querySelector(`#svgContainer svg #led${index}`);
    if (!ledCircle) return;
    ledCircle.style.fill = (led.on && blinkPhase) ? `rgb(${led.r},${led.g},${led.b})` : "#444";
  });
}, BLINK_INTERVAL_MS);

// ─── LED ──────────────────────────────────────────────────────────────────────

let lastLedStatus = null;

function onLedStatus(leds) {
  lastLedStatus = leds;
  leds.forEach((led, index) => {
    Object.assign(ledStatus[index], led);
    const card = document.querySelector(`.led-card[data-led="${index}"]`);
    if (!card) return;
    const ledCircle  = document.querySelector(`#svgContainer svg #led${index}`);
    const toggleBtn  = card.querySelector(".toggle");
    const colorPicker = card.querySelector(".colorPicker");
    const blinkBtn   = card.querySelector(".blink");
    const isOn    = led.on    || false;
    const isBlink = led.blink || false;
    toggleBtn.classList.toggle("on", isOn);
    toggleBtn.textContent = isOn ? "ON" : "OFF";
    blinkBtn.classList.toggle("on", isBlink);
    if (isOn) {
      const toHex = (v) => v.toString(16).padStart(2, "0");
      colorPicker.value = `#${toHex(led.r)}${toHex(led.g)}${toHex(led.b)}`;
    }
    if (ledCircle && !isBlink) {
      ledCircle.style.fill = isOn ? `rgb(${led.r},${led.g},${led.b})` : "#444";
    } else if (ledCircle && !isOn) {
      ledCircle.style.fill = "#444";
    }
  });
}

function prepareLedCards() {
  const ledCards = document.querySelectorAll(".led-card");

  ledCards.forEach((card) => {
    const ledIndex = parseInt(card.dataset.led);
    const colorPicker = card.querySelector(".colorPicker");
    const toggleBtn   = card.querySelector(".toggle");
    const blinkBtn    = card.querySelector(".blink");
    const ledCircle   = document.querySelector(`#svgContainer svg #led${ledIndex}`);

    ledCircle.style.fill = "#444";

    // Click sul cerchio → apre color picker
    ledCircle.addEventListener("click", () => {
      colorPicker.click();
    });

    function sendLed(r, g, b, on, blink) {
      Object.assign(ledStatus[ledIndex], { r, g, b, on, blink });
      wsSend({ type: "setLed", led: ledIndex, r, g, b, on, blink });
      if (!on) ledCircle.style.fill = "#444";
      else if (!blink) ledCircle.style.fill = `rgb(${r},${g},${b})`;
    }

    function currentRGB() {
      const color = colorPicker.value;
      return {
        r: parseInt(color.substr(1, 2), 16),
        g: parseInt(color.substr(3, 2), 16),
        b: parseInt(color.substr(5, 2), 16),
      };
    }

    colorPicker.addEventListener("input", () => {
      const { r, g, b } = currentRGB();
      sendLed(r, g, b, toggleBtn.classList.contains("on"), blinkBtn.classList.contains("on"));
    });

    toggleBtn.addEventListener("click", () => {
      toggleBtn.classList.toggle("on");
      toggleBtn.textContent = toggleBtn.classList.contains("on") ? "ON" : "OFF";
      const { r, g, b } = currentRGB();
      sendLed(r, g, b, toggleBtn.classList.contains("on"), blinkBtn.classList.contains("on"));
    });

    blinkBtn.addEventListener("click", () => {
      blinkBtn.classList.toggle("on");
      const { r, g, b } = currentRGB();
      sendLed(r, g, b, toggleBtn.classList.contains("on"), blinkBtn.classList.contains("on"));
    });
  });
}

// ─── Others ───────────────────────────────────────────────────────────────────

const cycleBtn        = document.getElementById("cycleBtn");
const topLedTimeInput    = document.getElementById("topLedTime");
const middleLedTimeInput = document.getElementById("middleLedTime");
const bottomLedTimeInput = document.getElementById("bottomLedTime");

function onCycleStatus(data) {
  const cycle = typeof data === "object" ? data.cycle : data;
  cycleBtn.classList.toggle("on", cycle);
  cycleBtn.textContent = cycle ? "ON" : "OFF";
  if (typeof data === "object") {
    if (data.topLedTime    != null) topLedTimeInput.value    = data.topLedTime;
    if (data.middleLedTime != null) middleLedTimeInput.value = data.middleLedTime;
    if (data.bottomLedTime != null) bottomLedTimeInput.value = data.bottomLedTime;
  }
}

function sendCycleConfig() {
  wsSend({
    type:          "setCycle",
    cycle:         cycleBtn.classList.contains("on"),
    topLedTime:    parseFloat(topLedTimeInput.value)    || 5,
    middleLedTime: parseFloat(middleLedTimeInput.value) || 2,
    bottomLedTime: parseFloat(bottomLedTimeInput.value) || 5,
  });
}

cycleBtn.addEventListener("click", () => {
  const newState = !cycleBtn.classList.contains("on");
  onCycleStatus(newState);
  if (newState) {
    onPartyStatus(false);
    onRainbowStatus(false);
  }
  sendCycleConfig();
});

[topLedTimeInput, middleLedTimeInput, bottomLedTimeInput].forEach(input => {
  input.addEventListener("change", sendCycleConfig);
});

const partyBtn        = document.getElementById("partyBtn");
const partyMadnessVal = document.getElementById("partyMadnessVal");
const madnessLabel    = document.querySelector(".madness-label");
let madnessColorTimer = null;

function startMadnessLabel() {
  clearInterval(madnessColorTimer);
  madnessLabel.classList.add("party-on");
  madnessColorTimer = setInterval(() => {
    madnessLabel.style.color = `hsl(${Math.random() * 360 | 0}, 100%, 65%)`;
  }, 120);
}

function stopMadnessLabel() {
  clearInterval(madnessColorTimer);
  madnessColorTimer = null;
  madnessLabel.classList.remove("party-on");
  madnessLabel.style.color = "";
  madnessLabel.style.transform = "";
}

partyMadnessVal.addEventListener("change", () => {
  const v = Math.min(10, Math.max(1, parseInt(partyMadnessVal.value) || 1));
  partyMadnessVal.value = v;
  wsSend({ type: "setParty", party: partyBtn.classList.contains("on"), partyMadness: v });
});

function onPartyStatus(data) {
  const party = typeof data === "object" ? data.party : data;
  partyBtn.classList.toggle("on", party);
  partyBtn.textContent = party ? "ON" : "OFF";
  party ? startMadnessLabel() : stopMadnessLabel();
  if (typeof data === "object" && data.partyMadness != null) {
    partyMadnessVal.value = data.partyMadness;
  }
}

function spawnPartyParticles() {
  const emojis = ["🎉","🎊","✨","🌈","⚡","💥","🎶","🔥","🍭","🎸"];
  const rect   = partyBtn.getBoundingClientRect();
  const cx     = rect.left + rect.width  / 2;
  const cy     = rect.top  + rect.height / 2;
  for (let i = 0; i < 12; i++) {
    const el  = document.createElement("span");
    el.className   = "party-particle";
    el.textContent = emojis[Math.floor(Math.random() * emojis.length)];
    const angle = (Math.PI * 2 / 12) * i + (Math.random() - 0.5) * 0.5;
    const dist  = 60 + Math.random() * 80;
    el.style.left  = `${cx}px`;
    el.style.top   = `${cy}px`;
    el.style.setProperty("--dx", `${Math.cos(angle) * dist}px`);
    el.style.setProperty("--dy", `${Math.sin(angle) * dist}px`);
    el.style.setProperty("--dr", `${(Math.random() - 0.5) * 360}deg`);
    el.style.animationDelay = `${Math.random() * 0.15}s`;
    document.body.appendChild(el);
    el.addEventListener("animationend", () => el.remove());
  }
}

partyBtn.addEventListener("click", () => {
  const newState = !partyBtn.classList.contains("on");
  if (newState) spawnPartyParticles();
  onPartyStatus(newState);
  if (newState) {
    onCycleStatus(false);
    onRainbowStatus(false);
  }
  wsSend({ type: "setParty", party: newState, partyMadness: parseInt(partyMadnessVal.value) });
});

document.getElementById("randomYNBtn").addEventListener("click", () => {
  wsSend({ type: "randomYesNo" });
  const icon = document.getElementById("diceIcon");
  icon.classList.remove("spinning");
  void icon.offsetWidth; // force reflow to restart animation
  icon.classList.add("spinning");
  setTimeout(() => icon.classList.remove("spinning"), 3000);
});

// ─── Guess game ───────────────────────────────────────────────────────────────

document.getElementById("guessBtn").addEventListener("click", () => {
  document.getElementById("guess-phase-pick").style.display    = "";
  document.getElementById("guess-phase-result").classList.remove("active");
  document.getElementById("guess-overlay").classList.add("visible");
});

document.querySelectorAll(".guess-led-opt").forEach(btn => {
  btn.addEventListener("click", () => {
    const picked = parseInt(btn.dataset.led);
    document.getElementById("guess-overlay").classList.remove("visible");
    document.querySelector("#guessBtn .dice-icon").classList.add("spinning-loop");
    wsSend({ type: "startGuess", led: picked });
  });
});

function closeGuessOverlay() {
  document.getElementById("guess-overlay").classList.remove("visible");
  const card = document.querySelector(".guess-card");
  card.style.background  = "";
  card.style.borderColor = "";
  card.classList.remove("winner", "loser");
}

function onGuessResult(data) {
  document.querySelector("#guessBtn .dice-icon").classList.remove("spinning-loop");
  const win = data.win;
  const el   = document.getElementById("guessResultText");
  const card = document.querySelector(".guess-card");
  el.textContent  = win ? "WINNER" : "LOOSER";
  el.style.color  = win ? "#1a1a1a" : "#fff";
  card.style.background  = win ? "var(--primary)" : "#c0392b";
  card.style.borderColor = win ? "var(--primary)" : "#c0392b";
  card.classList.toggle("winner", win);
  card.classList.toggle("loser", !win);
  const hint = card.querySelector(".guess-tap-hint");
  if (hint) hint.style.color = win ? "#1a1a1a" : "#fff";
  document.getElementById("guess-phase-pick").style.display  = "none";
  document.getElementById("guess-phase-result").classList.add("active");
  document.getElementById("guess-overlay").classList.add("visible");
}

document.getElementById("guess-overlay").addEventListener("click", () => {
  if (document.getElementById("guess-phase-result").classList.contains("active")) closeGuessOverlay();
});

// ─── Morse Code ───────────────────────────────────────────────────────────────

document.getElementById("weatherBtn").addEventListener("click", () => {
  wsSend({ type: "weatherColor" });
});

document.getElementById("airQualityBtn").addEventListener("click", () => {
  wsSend({ type: "airQualityColor" });
});

document.getElementById("morseBtn").addEventListener("click", () => {
  document.getElementById("morse-overlay").classList.add("visible");
  const input = document.getElementById("morseText");
  input.focus();
  input.select();
});

function sendMorse() {
  const text = document.getElementById("morseText").value.trim() || "SOS";
  document.getElementById("morse-overlay").classList.remove("visible");
  wsSend({ type: "morse", text });
}

document.getElementById("morseConfirmBtn").addEventListener("click", sendMorse);

document.getElementById("morseCancelBtn").addEventListener("click", () => {
  document.getElementById("morse-overlay").classList.remove("visible");
});

document.getElementById("morseText").addEventListener("keydown", (e) => {
  if (e.key === "Enter") sendMorse();
  if (e.key === "Escape") document.getElementById("morse-overlay").classList.remove("visible");
});

// ─── Rainbow ──────────────────────────────────────────────────────────────────

const rainbowBtn           = document.getElementById("rainbowBtn");
const rainbowCycleTimeInput = document.getElementById("rainbowCycleTime");

function onRainbowStatus(data) {
  const on = typeof data === "object" ? data.rainbow : data;
  rainbowBtn.classList.toggle("on", on);
  rainbowBtn.textContent = on ? "ON" : "OFF";
  if (typeof data === "object" && data.rainbowCycleTime != null)
    rainbowCycleTimeInput.value = data.rainbowCycleTime;
}

function sendRainbowConfig() {
  wsSend({
    type:            "setRainbow",
    rainbow:         rainbowBtn.classList.contains("on"),
    rainbowCycleTime: parseFloat(rainbowCycleTimeInput.value) || 5,
  });
}

rainbowBtn.addEventListener("click", () => {
  const newState = !rainbowBtn.classList.contains("on");
  onRainbowStatus(newState);
  if (newState) {
    onPartyStatus(false);
    onCycleStatus(false);
  }
  sendRainbowConfig();
});

rainbowCycleTimeInput.addEventListener("change", sendRainbowConfig);

// ─── Info ─────────────────────────────────────────────────────────────────────

function formatUptime(seconds) {
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  if (d > 0) return `${d}d ${h}h ${m}m ${s}s`;
  if (h > 0) return `${h}h ${m}m ${s}s`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

function updateMqttStatusLabel(mqttBroker, mqttConnected) {
  const statusEl = document.getElementById("mqttStatus");
  if (!statusEl) return;
  if (mqttBroker) {
    statusEl.textContent = mqttConnected ? "Connected" : "Disconnected";
    statusEl.style.color = mqttConnected ? "#b1ff42" : "#f44336";
  } else {
    statusEl.textContent = "Not configured";
    statusEl.style.color = "#aaa";
  }
}

let lastSysInfo = null;

function onSysInfo(data) {
  lastSysInfo = data;
  document.getElementById("infoVersion").textContent = data.version  ? `v${data.version}` : "—";
  if (data.version && data.version !== _deviceVersion) {
    _deviceVersion = data.version;
    checkFirmwareUpdate(false, true);
  }
  document.getElementById("infoIp").textContent     = data.ip       || "—";
  document.getElementById("infoSsid").textContent   = data.ssid     || "—";
  document.getElementById("infoRssi").textContent   = data.rssi != null ? `${data.rssi} dBm` : "—";
  document.getElementById("infoDatetime").textContent = data.datetime || "—";
  document.getElementById("infoUptime").textContent   = data.uptime != null ? formatUptime(data.uptime) : "—";
  document.getElementById("infoHeap").textContent   = data.freeHeap != null ? `${(data.freeHeap / 1024).toFixed(1)} KB` : "—";
  const mqttEl = document.getElementById("infoMqtt");
  if (data.mqttBroker) {
    mqttEl.textContent = data.mqttConnected ? `Connected (${data.mqttBroker})` : `Disconnected (${data.mqttBroker})`;
    mqttEl.style.color = data.mqttConnected ? "#b1ff42" : "#f44336";
  } else {
    mqttEl.textContent = "Not configured";
    mqttEl.style.color = "#aaa";
  }
  updateMqttStatusLabel(data.mqttBroker, data.mqttConnected);
  document.getElementById("infoMac").textContent     = data.mac        || "—";
  document.getElementById("infoCpu").textContent     = data.cpuFreq != null ? `${data.cpuFreq} MHz` : "—";
  document.getElementById("infoChip").textContent    = data.chipModel ? `${data.chipModel} rev${data.chipRevision}` : "—";
  document.getElementById("infoChannel").textContent = data.wifiChannel != null ? `${data.wifiChannel}` : "—";
  const conditionLabels = ["—", "Clear", "Partly cloudy", "Foggy", "Drizzle", "Rainy", "Snowy", "Stormy"];
  document.getElementById("weatherBtn").disabled    = data.weatherCode == null || !data.latitude;
  document.getElementById("airQualityBtn").disabled = data.aqPm25 == null || !data.latitude;
  if (data.weatherCode != null) {
    const cond = conditionLabels[data.weatherCondition] || "—";
    document.getElementById("infoWeather").textContent     = `${cond} (code ${data.weatherCode})`;
    document.getElementById("infoWeatherTemp").textContent     = `${data.weatherTemp.toFixed(1)} °C`;
    document.getElementById("infoWeatherHumidity").textContent = `${data.weatherHumidity.toFixed(0)} %`;
    document.getElementById("conditionDot").style.background = `rgb(${data.conditionR},${data.conditionG},${data.conditionB})`;
    document.getElementById("weatherDot").style.background   = `rgb(${data.temperatureR},${data.temperatureG},${data.temperatureB})`;
    document.getElementById("humidityDot").style.background  = `rgb(${data.humidityR},${data.humidityG},${data.humidityB})`;
  } else {
    const hasLocation = !!parseFloat(document.getElementById("latitude").value);
    document.getElementById("infoWeather").textContent         = hasLocation ? "Fetching..." : "No location set";
    document.getElementById("infoWeatherTemp").textContent     = "—";
    document.getElementById("infoWeatherHumidity").textContent = "—";
    document.getElementById("conditionDot").style.background = "transparent";
    document.getElementById("weatherDot").style.background   = "transparent";
    document.getElementById("humidityDot").style.background  = "transparent";
  }
  if (data.aqPm25 != null) {
    document.getElementById("infoPm25").textContent  = `${data.aqPm25.toFixed(1)} µg/m³`;
    document.getElementById("infoPm10").textContent  = `${data.aqPm10.toFixed(1)} µg/m³`;
    document.getElementById("infoNo2").textContent   = `${data.aqNo2.toFixed(1)} µg/m³`;
    document.getElementById("pm25Dot").style.background   = `rgb(${data.aqPm25R},${data.aqPm25G},${data.aqPm25B})`;
    document.getElementById("pm10Dot").style.background   = `rgb(${data.aqPm10R},${data.aqPm10G},${data.aqPm10B})`;
    document.getElementById("no2Dot").style.background    = `rgb(${data.aqNo2R},${data.aqNo2G},${data.aqNo2B})`;
  } else {
    ["infoPm25","infoPm10","infoNo2"].forEach(id => {
      document.getElementById(id).textContent = data.latitude ? "Fetching..." : "—";
    });
    ["pm25Dot","pm10Dot","no2Dot"].forEach(id => {
      document.getElementById(id).style.background = "transparent";
    });
  }
}

function requestInfo() {
  wsSend({ type: "getInfo" });
}

function backupConfig() {
  const a = document.createElement("a");
  a.href = "/backup";
  a.download = "semaphore-backup.json";
  a.click();
}

let _restoreFileContent = null;

function restoreConfig(input) {
  const file = input.files[0];
  if (!file) return;
  input.value = "";
  const reader = new FileReader();
  reader.onload = (e) => {
    _restoreFileContent = e.target.result;
    showRestorePhase("confirm");
    document.getElementById("restore-overlay").classList.add("visible");
  };
  reader.readAsText(file);
}

function showRestorePhase(phase) {
  ["confirm", "progress", "result", "wifi"].forEach(p =>
    document.getElementById("restore-phase-" + p).style.display = p === phase ? "" : "none"
  );
}

function closeRestoreOverlay() {
  document.getElementById("restore-overlay").classList.remove("visible");
  _restoreFileContent = null;
}

function confirmRestore() {
  if (!_restoreFileContent) return;
  showRestorePhase("progress");
  fetch("/restore", { method: "POST", headers: { "Content-Type": "application/json" }, body: _restoreFileContent })
    .then(r => {
      if (!r.ok) throw new Error();
      document.getElementById("restoreResultText").textContent = "Restore completed. Rebooting...";
      showRestorePhase("result");
      fetch("/restart", { method: "POST" }).catch(() => {});
      setTimeout(closeRestoreOverlay, 2000);
    })
    .catch(() => {
      document.getElementById("restoreResultText").textContent = "Restore failed.";
      showRestorePhase("result");
      setTimeout(closeRestoreOverlay, 2000);
    });
}

document.getElementById("makeChangesPersistent").addEventListener("change", (e) => {
  wsSend({ type: "setConfig", makeChangesPersistent: e.target.checked });
});

// ─── MQTT ─────────────────────────────────────────────────────────────────────

function onMqttConfig(data) {
  document.getElementById("mqttEnabled").checked = data.enabled ?? false;
  document.getElementById("mqttBroker").value   = data.broker   || "";
  document.getElementById("mqttPort").value     = data.port     ?? 1883;
  document.getElementById("mqttUsername").value = data.username || "";
  document.getElementById("mqttPassword").value = data.password || "";
  document.getElementById("mqttClientId").value = data.clientId || "semaphore";
  document.getElementById("mqttTopic").value    = data.topic    || "semaphore";
  updateMqttStatusLabel(data.broker, data.connected);
}

function animateSaveIcon(id) {
  const icon = document.getElementById(id);
  icon.classList.remove("saving");
  void icon.offsetWidth;
  icon.classList.add("saving");
  setTimeout(() => icon.classList.remove("saving"), 450);
}

function saveMqtt() {
  animateSaveIcon("saveMqttIcon");
  wsSend({
    type:     "setMqtt",
    enabled:  document.getElementById("mqttEnabled").checked,
    broker:   document.getElementById("mqttBroker").value,
    port:     parseInt(document.getElementById("mqttPort").value) || 1883,
    username: document.getElementById("mqttUsername").value,
    password: document.getElementById("mqttPassword").value,
    clientId: document.getElementById("mqttClientId").value,
    topic:    document.getElementById("mqttTopic").value,
  });
}

// ─── WiFi ─────────────────────────────────────────────────────────────────────

const dhcpCheckbox = document.getElementById("dhcpMode");
const staticFields = document.querySelectorAll("#staticFields input");

function updateStaticFields() {
  staticFields.forEach((field) => {
    field.disabled = dhcpCheckbox.checked;
  });
}

function onWifiConfig(data) {
  document.getElementById("deviceName").value = data.deviceName || "";
  document.getElementById("ntpServer").value  = data.ntpServer  || "";
  if (data.timezone) document.getElementById("timezone").value = data.timezone;
  document.getElementById("ssid").value       = data.ssid       || "";
  document.getElementById("password").value   = data.password   || "";
  dhcpCheckbox.checked                        = data.dhcp       ?? true;
  document.getElementById("ip").value         = data.ip         || "";
  document.getElementById("subnet").value     = data.subnet     || "";
  document.getElementById("gateway").value    = data.gateway    || "";
  document.getElementById("dns").value        = data.dns        || "";
  updateStaticFields();
}

function saveWifi() {
  document.getElementById("wifiSaveText").textContent = "";
  showRestorePhase("wifi");
  document.getElementById("restore-overlay").classList.add("visible");
  wsSend({
    type:       "setWifi",
    deviceName: document.getElementById("deviceName").value,
    ntpServer:  document.getElementById("ntpServer").value,
    timezone:   document.getElementById("timezone").value,
    ssid:       document.getElementById("ssid").value,
    password:   document.getElementById("password").value,
    dhcp:       dhcpCheckbox.checked,
    ip:         document.getElementById("ip").value,
    subnet:     document.getElementById("subnet").value,
    gateway:    document.getElementById("gateway").value,
    dns:        document.getElementById("dns").value,
  });
}

let _map = null;
let _mapMarker = null;
let _pendingLat = null;
let _pendingLon = null;

function openMapOverlay() {
  document.getElementById("map-overlay").classList.add("visible");
  _pendingLat = null;
  _pendingLon = null;
  document.getElementById("mapConfirmBtn").disabled = true;
  document.getElementById("mapCoordsDisplay").textContent = "";

  const existingLat = parseFloat(document.getElementById("latitude").value);
  const existingLon = parseFloat(document.getElementById("longitude").value);
  const center = (existingLat && existingLon) ? [existingLat, existingLon] : [45, 9];
  const zoom   = (existingLat && existingLon) ? 12 : 5;

  if (!_map) {
    _map = L.map("leaflet-map", { zoomControl: true }).setView(center, zoom);
    L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
      attribution: "© OpenStreetMap contributors",
      maxZoom: 19,
    }).addTo(_map);
    _map.on("click", (e) => {
      _pendingLat = +e.latlng.lat.toFixed(6);
      _pendingLon = +e.latlng.lng.toFixed(6);
      if (_mapMarker) _mapMarker.setLatLng(e.latlng);
      else _mapMarker = L.marker(e.latlng).addTo(_map);
      document.getElementById("mapCoordsDisplay").textContent = `${_pendingLat}, ${_pendingLon}`;
      document.getElementById("mapConfirmBtn").disabled = false;
    });
  } else {
    _map.setView(center, zoom);
  }

  if (existingLat && existingLon) {
    _pendingLat = existingLat;
    _pendingLon = existingLon;
    if (_mapMarker) _mapMarker.setLatLng([existingLat, existingLon]);
    else _mapMarker = L.marker([existingLat, existingLon]).addTo(_map);
    document.getElementById("mapCoordsDisplay").textContent = `${existingLat}, ${existingLon}`;
    document.getElementById("mapConfirmBtn").disabled = false;
  }

  setTimeout(() => _map.invalidateSize(), 120);
}

function closeMapOverlay() {
  document.getElementById("map-overlay").classList.remove("visible");
}

function confirmMapLocation() {
  if (_pendingLat === null) return;
  document.getElementById("latitude").value  = _pendingLat;
  document.getElementById("longitude").value = _pendingLon;
  updateLocationLabel();
  wsSend({ type: "setLocation", latitude: _pendingLat, longitude: _pendingLon });
  closeMapOverlay();
}

function updateLocationLabel() {
  const lat = parseFloat(document.getElementById("latitude").value);
  const lon = parseFloat(document.getElementById("longitude").value);
  const label = document.getElementById("locationLabel");
  label.textContent = (lat && lon) ? `lat: ${lat}  lon: ${lon}` : "lat: —  lon: —";
}

// ─── Init ─────────────────────────────────────────────────────────────────────

const TAB_ORDER = ["led", "others", "timer", "wifi", "mqtt", "info", "console"];
let currentTabIndex = 0;

dhcpCheckbox.addEventListener("change", updateStaticFields);
updateStaticFields();
wrapNumberInputs();

// Restore last active tab, then set indicator without transition
(function initTabIndicator() {
  const savedTab = localStorage.getItem("activeTab");
  if (savedTab && TAB_ORDER.includes(savedTab) && savedTab !== "led") {
    const btn = document.querySelector(`.tab-button[onclick*="'${savedTab}'"]`);
    if (btn) {
      document.querySelectorAll(".tab-content").forEach(t => t.classList.remove("active"));
      document.querySelectorAll(".tab-button").forEach(b => b.classList.remove("active"));
      document.getElementById(savedTab)?.classList.add("active");
      btn.classList.add("active");
      currentTabIndex = TAB_ORDER.indexOf(savedTab);
      if (savedTab === "info" || savedTab === "mqtt") {
        requestInfo();
        infoRefreshTimer = setInterval(requestInfo, 1000);
      }
    }
  }
  const indicator = document.querySelector(".tab-indicator");
  const active    = document.querySelector(".tab-button.active");
  if (!indicator || !active) return;
  indicator.style.transition = "none";
  moveTabIndicator(active);
  requestAnimationFrame(() => { indicator.style.transition = ""; });
})();

fetch("semaphore.svg")
  .then((res) => res.text())
  .then((data) => {
    document.getElementById("svgContainer").innerHTML = data;
    const svg = document.querySelector("#svgContainer svg");
    svg.style.height = "400px";
    prepareLedCards();
    if (lastLedStatus) onLedStatus(lastLedStatus);
  });

// ─── Number input wrappers ────────────────────────────────────────────────────

function wrapNumberInputs() {
  document.querySelectorAll('input[type="number"]').forEach(input => {
    const isTime = input.classList.contains('time-input');
    const wrapper = document.createElement('div');
    wrapper.className = 'number-wrapper' + (isTime ? ' is-time' : '');

    const btnMinus = document.createElement('button');
    btnMinus.type = 'button';
    btnMinus.className = 'number-btn';
    btnMinus.textContent = '−';

    const btnPlus = document.createElement('button');
    btnPlus.type = 'button';
    btnPlus.className = 'number-btn';
    btnPlus.textContent = '+';

    btnMinus.addEventListener('click', () => {
      input.stepDown();
      input.dispatchEvent(new Event('change', { bubbles: true }));
    });

    btnPlus.addEventListener('click', () => {
      input.stepUp();
      input.dispatchEvent(new Event('change', { bubbles: true }));
    });

    input.parentNode.insertBefore(wrapper, input);
    wrapper.appendChild(btnMinus);
    wrapper.appendChild(input);
    wrapper.appendChild(btnPlus);
  });
}

// ─── Password toggle ──────────────────────────────────────────────────────────

const EYE_OPEN   = '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>';
const EYE_CLOSED = '<path d="M17.94 17.94A10.07 10.07 0 0112 20c-7 0-11-8-11-8a18.45 18.45 0 015.06-5.94M9.9 4.24A9.12 9.12 0 0112 4c7 0 11 8 11 8a18.5 18.5 0 01-2.16 3.19m-6.72-1.07a3 3 0 11-4.24-4.24M1 1l22 22"/>';

function togglePassword(inputId, btn) {
  const input = document.getElementById(inputId);
  const showing = input.type === 'text';
  input.type = showing ? 'password' : 'text';
  btn.querySelector('svg').innerHTML = showing ? EYE_OPEN : EYE_CLOSED;
}

function moveTabIndicator(btn) {
  const indicator = document.querySelector(".tab-indicator");
  const tabs = document.querySelector(".tabs");
  if (!indicator || !tabs || !btn) return;
  const tabsRect = tabs.getBoundingClientRect();
  const btnRect  = btn.getBoundingClientRect();
  indicator.style.left   = `${btnRect.left - tabsRect.left}px`;
  indicator.style.top    = `${btnRect.top  - tabsRect.top}px`;
  indicator.style.width  = `${btnRect.width}px`;
  indicator.style.height = `${btnRect.height}px`;
}


function openTab(evt, tabName) {
  const newIndex   = TAB_ORDER.indexOf(tabName);
  const toRight    = newIndex > currentTabIndex;
  const inClass    = toRight ? "slide-in-right"  : "slide-in-left";
  const outClass   = toRight ? "slide-out-left"  : "slide-out-right";

  const oldTab = document.querySelector(".tab-content.active");
  const newTab = document.getElementById(tabName);

  document.querySelectorAll(".tab-button").forEach((btn) => btn.classList.remove("active"));
  evt.currentTarget.classList.add("active");
  moveTabIndicator(evt.currentTarget);

  if (oldTab && oldTab !== newTab) {
    const viewport = document.querySelector(".tab-viewport");
    viewport.classList.add("is-transitioning");
    let done = 0;
    const onDone = () => { if (++done === 2) viewport.classList.remove("is-transitioning"); };

    oldTab.classList.add(outClass);
    oldTab.addEventListener("animationend", () => {
      oldTab.classList.remove("active", outClass);
      onDone();
    }, { once: true });

    newTab.classList.add("active", inClass);
    newTab.addEventListener("animationend", () => {
      newTab.classList.remove(inClass);
      onDone();
    }, { once: true });
  } else {
    newTab.classList.add("active");
  }

  currentTabIndex = newIndex;
  localStorage.setItem("activeTab", tabName);

  if (tabName === "timer") renderTimers();
  clearInterval(infoRefreshTimer);
  infoRefreshTimer = null;
  if (tabName === "info") {
    requestInfo();
    infoRefreshTimer = setInterval(requestInfo, 1000);
  } else if (tabName === "mqtt") {
    wsSend({ type: "getMqtt" });
    infoRefreshTimer = setInterval(requestInfo, 1000);
  }
}

// ─── Timer ────────────────────────────────────────────────────────────────────

const DAY_NAMES = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"];

const TIMER_ACTIONS = [
  { value: "all_off",  label: "All OFF"        },
  { value: "led2",     label: "LED Top"        },
  { value: "led1",     label: "LED Middle"     },
  { value: "led0",     label: "LED Bottom"     },
  { value: "cycle",    label: "Cycle ON"       },
  { value: "party",    label: "Party ON"       },
  { value: "rainbow",       label: "Rainbow ON"     },
  { value: "random_yes_no", label: "Random Yes/No"  },
  { value: "morse",         label: "Morse"          },
  { value: "guess",         label: "Guess"          },
  { value: "weather_color", label: "Weather Color"  },
];

function daysSummary(days) {
  if (!days || days.length === 0) return "No days";
  const s = [...days].sort((a, b) => a - b);
  if (s.length === 7) return "Every day";
  if (JSON.stringify(s) === JSON.stringify([0,1,2,3,4])) return "Mon–Fri";
  if (JSON.stringify(s) === JSON.stringify([5,6])) return "Weekend";
  return s.map(d => DAY_NAMES[d]).join(" ");
}

function actionLabel(action) {
  return TIMER_ACTIONS.find(a => a.value === action)?.label || action;
}

function timerSummary(t) {
  return `${daysSummary(t.days)}  ${t.time}  —  ${actionLabel(t.action)}`;
}

function addTimer() {
  timers.push({
    id:       timerNextId++,
    enabled:  true,
    days:     [0,1,2,3,4],
    time:     "08:00:00",
    action:    "all_off",
    ledColor:  "#ffffff",
    morseText: "SOS",
    guessLed:  0,
    duration:  0,
    expanded:  true,
  });
  renderTimers();
}

function removeTimer(id) {
  timers = timers.filter(t => t.id !== id);
  renderTimers();
}

function toggleTimerEnabled(id) {
  const t = timers.find(t => t.id === id);
  if (t) { t.enabled = !t.enabled; renderTimers(); }
}

function toggleTimerCard(id) {
  const t = timers.find(t => t.id === id);
  if (!t) return;
  t.expanded = !t.expanded;
  const card = document.querySelector(`.timer-card[data-id="${id}"]`);
  if (card) card.classList.toggle("expanded", t.expanded);
}

function setTimerDay(id, day) {
  const t = timers.find(t => t.id === id);
  if (!t) return;
  const idx = t.days.indexOf(day);
  if (idx >= 0) t.days.splice(idx, 1); else t.days.push(day);
  renderTimers();
}

function setTimerField(id, field, value) {
  const t = timers.find(t => t.id === id);
  if (!t) return;
  t[field] = value;
  if (field === "action") renderTimers();
}

function timeParts(time, idx) {
  const p = (time || "00:00:00").split(":");
  return parseInt(p[idx] || 0, 10);
}

function setTimerTime(id, part, input) {
  const t = timers.find(t => t.id === id);
  if (!t) return;
  const max = part === 'h' ? 23 : 59;
  let val = Math.min(max, Math.max(0, parseInt(input.value) || 0));
  input.value = val;
  const p = (t.time || "00:00:00").split(":").map(Number);
  if (part === 'h') p[0] = val;
  else if (part === 'm') p[1] = val;
  else p[2] = val;
  t.time = p.map(v => String(v).padStart(2, "0")).join(":");
}

function renderTimerCard(t) {
  const actOptions = TIMER_ACTIONS.map(a =>
    `<option value="${a.value}"${t.action === a.value ? " selected" : ""}>${a.label}</option>`
  ).join("");

  const dayChips = DAY_NAMES.map((name, i) =>
    `<button class="day-chip${t.days.includes(i) ? " active" : ""}" onclick="setTimerDay(${t.id},${i})">${name}</button>`
  ).join("");

  const colorRow = ["led0","led1","led2"].includes(t.action)
    ? `<div class="timer-row">
        <label>Color</label>
        <input type="color" value="${t.ledColor}" onchange="setTimerField(${t.id},'ledColor',this.value)">
       </div>`
    : "";

  const guessLedRow = t.action === "guess"
    ? `<div class="timer-row">
        <label>LED</label>
        <select onchange="setTimerField(${t.id},'guessLed',parseInt(this.value))">
          <option value="0"${(t.guessLed||0)===0?" selected":""}>Bottom</option>
          <option value="1"${(t.guessLed||0)===1?" selected":""}>Middle</option>
          <option value="2"${(t.guessLed||0)===2?" selected":""}>Top</option>
        </select>
       </div>`
    : "";

  const morseRow = t.action === "morse"
    ? `<div class="timer-row">
        <label>Text</label>
        <input type="text" value="${t.morseText || "SOS"}" maxlength="32" onchange="setTimerField(${t.id},'morseText',this.value.toUpperCase())">
       </div>`
    : "";

  const durVal = t.duration || 0;

  return `
    <div class="timer-card${t.expanded ? " expanded" : ""}" data-id="${t.id}">
      <div class="timer-header" onclick="toggleTimerCard(${t.id})">
        <svg class="timer-chevron" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><polyline points="9 18 15 12 9 6"/></svg>
        <span class="timer-summary">${timerSummary(t)}</span>
        <button class="toggle-btn${t.enabled ? " on" : ""}" onclick="event.stopPropagation();toggleTimerEnabled(${t.id})">${t.enabled ? "ON" : "OFF"}</button>
      </div>
      <div class="timer-body"><div class="timer-body-inner">
        <div class="day-chips">${dayChips}</div>
        <div class="timer-row">
          <label>Time</label>
          <div class="time-fields">
            <input type="number" class="time-part" min="0" max="23"  value="${timeParts(t.time,0)}" onchange="setTimerTime(${t.id},'h',this)">
            <span>:</span>
            <input type="number" class="time-part" min="0" max="59"  value="${timeParts(t.time,1)}" onchange="setTimerTime(${t.id},'m',this)">
            <span>:</span>
            <input type="number" class="time-part" min="0" max="59"  value="${timeParts(t.time,2)}" onchange="setTimerTime(${t.id},'s',this)">
          </div>
        </div>
        <div class="timer-row">
          <label>Action</label>
          <select onchange="setTimerField(${t.id},'action',this.value)">${actOptions}</select>
        </div>
        ${colorRow}
        ${morseRow}
        ${guessLedRow}
        ${!["morse","random_yes_no","guess","weather_color"].includes(t.action) ? `<div class="timer-row">
          <label>Duration (s)</label>
          <input type="number" class="time-input" min="0" step="1" value="${durVal}" placeholder="0 = no limit" onchange="setTimerField(${t.id},'duration',parseInt(this.value)||0)">
        </div>` : ""}
        <button class="timer-delete-btn" onclick="removeTimer(${t.id})">Delete</button>
      </div></div>
    </div>`;
}

function renderTimers() {
  const list = document.getElementById("timerList");
  if (!list) return;
  list.innerHTML = timers.map(renderTimerCard).join("");
}

function onTimerConfig(data) {
  if (!Array.isArray(data.timers)) return;
  timers = data.timers.map(t => ({
    id:       t.id,
    enabled:  t.enabled ?? true,
    days:     Array.isArray(t.days) ? t.days : [],
    time:     t.time     || "00:00",
    action:    t.action    || "all_off",
    ledColor:  t.ledColor  || "#ffffff",
    morseText: t.morseText || "SOS",
    guessLed:  t.guessLed  || 0,
    duration:  t.duration  || 0,
    expanded:  false,
  }));
  timerNextId = timers.length > 0 ? Math.max(...timers.map(t => t.id)) + 1 : 0;
  renderTimers();
}

function saveTimers() {
  animateSaveIcon("saveTimersIcon");
  wsSend({
    type: "setTimers",
    timers: timers.map(t => ({
      id:       t.id,
      enabled:  t.enabled,
      days:     t.days,
      time:     t.time,
      action:    t.action,
      ledColor:  t.ledColor,
      morseText: t.morseText || "SOS",
      guessLed:  t.guessLed  || 0,
      duration:  t.duration  || 0,
    })),
  });
}

if (localStorage.getItem("activeTab") === "timer") renderTimers();

// ─── Toast ────────────────────────────────────────────────────────────────────

// ─── OTA Update ───────────────────────────────────────────────────────────────

const OTA_STEP_ORDER = ["backup", "filesystem", "restore", "firmware"];
const GITHUB_RELEASES_URL = "https://api.github.com/repos/GabeMx5/ESP32-C3-Semaphore/releases/latest";

let _deviceVersion = null;
let _latestVersion = null;

const SVG_REFRESH = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" width="14" height="14"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/></svg>`;
const SVG_UPLOAD  = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" width="14" height="14"><polyline points="16 16 12 12 8 16"/><line x1="12" y1="12" x2="12" y2="21"/><path d="M20.39 18.39A5 5 0 0 0 18 9h-1.26A8 8 0 1 0 3 16.3"/></svg>`;

function _isNewer(latest, current) {
  const l = latest.replace(/^v/, '').split('.').map(Number);
  const c = (current || '0').split('.').map(Number);
  for (let i = 0; i < Math.max(l.length, c.length); i++) {
    if ((l[i] || 0) > (c[i] || 0)) return true;
    if ((l[i] || 0) < (c[i] || 0)) return false;
  }
  return false;
}

function _setUpdateBtn(state) {
  const btn  = document.getElementById("updateBtn");
  const icon = document.getElementById("updateBtnIcon");
  if (!btn || !icon) return;
  btn.className = "location-btn" + (state === "checking" ? " checking" : state === "available" ? " update-available" : "");
  btn.style.marginTop = "0";
  btn.style.width  = "28px";
  btn.style.height = "28px";
  btn.title = state === "available" ? `Update to ${_latestVersion}` : "Check for updates";
  icon.outerHTML = (state === "available" ? SVG_UPLOAD : SVG_REFRESH).replace('<svg ', '<svg id="updateBtnIcon" ');
}

function checkFirmwareUpdate(notify = false, autoShow = false) {
  if (!_deviceVersion) return;
  _setUpdateBtn("checking");
  fetch(GITHUB_RELEASES_URL)
    .then(r => { if (!r.ok) throw new Error(r.status); return r.json(); })
    .then(data => {
      _latestVersion = data.tag_name || null;
      if (_latestVersion && _isNewer(_latestVersion, _deviceVersion)) {
        _setUpdateBtn("available");
        if (autoShow) {
          document.getElementById("ota-latest-label").textContent =
            `Current: v${_deviceVersion}  →  Latest: ${_latestVersion}`;
          document.getElementById("ota-phase-confirm").style.display  = "";
          document.getElementById("ota-phase-progress").style.display = "none";
          document.getElementById("ota-overlay").classList.add("visible");
        }
      } else {
        _setUpdateBtn("upToDate");
        if (notify) showToast(`v${_deviceVersion} is the latest version`);
      }
    })
    .catch(() => {
      _setUpdateBtn("upToDate");
      if (notify) showToast("Could not reach GitHub", "error");
    });
}

function onUpdateBtnClick() {
  const btn = document.getElementById("updateBtn");
  if (btn && btn.classList.contains("update-available")) {
    document.getElementById("ota-latest-label").textContent =
      `Current: v${_deviceVersion}  →  Latest: ${_latestVersion}`;
    document.getElementById("ota-phase-confirm").style.display  = "";
    document.getElementById("ota-phase-progress").style.display = "none";
    document.getElementById("ota-overlay").classList.add("visible");
  } else {
    checkFirmwareUpdate(true);
  }
}

function confirmOTA() {
  document.getElementById("ota-phase-confirm").style.display  = "none";
  document.getElementById("ota-phase-progress").style.display = "";
  OTA_STEP_ORDER.forEach(s => {
    const el = document.getElementById(`ota-step-${s}`);
    if (!el) return;
    el.className = "ota-step";
    el.textContent = OTA_STEP_LABELS[s] || s;
  });
  document.getElementById("ota-step-error").style.display    = "none";
  document.getElementById("ota-reconnect-msg").style.display = "none";
  wsSend({ type: "startOTA" });
}

function closeOtaOverlay() {
  document.getElementById("ota-overlay").classList.remove("visible");
}

const OTA_STEP_LABELS = {
  backup:     "Backing up config",
  filesystem: "Updating filesystem",
  restore:    "Restoring config",
  firmware:   "Updating firmware",
};

function onOtaStatus(step) {
  if (step === "error") {
    OTA_STEP_ORDER.forEach(s => {
      const el = document.getElementById(`ota-step-${s}`);
      if (el) el.classList.remove("active");
    });
    document.getElementById("ota-step-error").style.display = "block";
    return;
  }
  const idx = OTA_STEP_ORDER.indexOf(step);
  OTA_STEP_ORDER.forEach((s, i) => {
    const el = document.getElementById(`ota-step-${s}`);
    if (!el) return;
    el.textContent = OTA_STEP_LABELS[s] || s;
    if (i < idx)   el.className = "ota-step done";
    if (i === idx) el.className = "ota-step active";
    if (i > idx)   el.className = "ota-step";
  });
  if (step === "firmware") {
    otaFirmwareFlashing = true;
  }
}

function onOtaProgress(step, pct) {
  const el = document.getElementById(`ota-step-${step}`);
  if (!el || !el.classList.contains("active")) return;
  el.textContent = `${OTA_STEP_LABELS[step] || step} ${pct}%`;
}

// ─── Console ──────────────────────────────────────────────────────────────────

let consoleHistory = [];
let consoleHistoryIndex = -1;

function appendConsoleLine(text) {
  const out = document.getElementById("consoleOutput");
  if (!out) return;
  const line = document.createElement("div");
  line.className = "console-line" + (text.startsWith("RST: >") ? " cmd" : "");
  line.textContent = text;
  out.appendChild(line);
  out.scrollTop = out.scrollHeight;
}

function consoleSend() {
  const input = document.getElementById("consoleInput");
  if (!input) return;
  const cmd = input.value.trim();
  if (!cmd) return;
  consoleHistory.unshift(cmd);
  if (consoleHistory.length > 50) consoleHistory.pop();
  consoleHistoryIndex = -1;
  input.value = "";
  wsSend({ type: "consoleCmd", cmd });
}

function consoleKeyDown(e) {
  const input = document.getElementById("consoleInput");
  if (!input) return;
  if (e.key === "Enter") {
    consoleSend();
  } else if (e.key === "ArrowUp") {
    e.preventDefault();
    if (consoleHistoryIndex < consoleHistory.length - 1) {
      consoleHistoryIndex++;
      input.value = consoleHistory[consoleHistoryIndex];
    }
  } else if (e.key === "ArrowDown") {
    e.preventDefault();
    if (consoleHistoryIndex > 0) {
      consoleHistoryIndex--;
      input.value = consoleHistory[consoleHistoryIndex];
    } else {
      consoleHistoryIndex = -1;
      input.value = "";
    }
  }
}

function showToast(msg, type = "info") {
  const el = document.getElementById("toast");
  if (!el) return;
  el.textContent = msg;
  el.classList.toggle("error", type === "error");
  el.classList.add("visible");
  clearTimeout(el._t);
  el._t = setTimeout(() => el.classList.remove("visible"), 2500);
}

