const state = {
  steps: 0,
  energyMj: 0,
  voltage: 0,
  securityMode: false,
  chartPaused: false,
  history: [],
  sampleCount: 0,
  dataSource: "Demo",
  lastUpdateLabel: "-",
  ws: null,
  serialPort: null,
  serialWriter: null,
  serialReader: null,
  chartPoints: [],
  lastSampleTs: 0,
  sampleRate: 0,
};

const ui = {
  stepCounter: document.getElementById("stepCounter"),
  energyValue: document.getElementById("energyValue"),
  energyRing: document.getElementById("energyRing"),
  voltageValue: document.getElementById("voltageValue"),
  voltageFill: document.getElementById("voltageFill"),
  sampleRate: document.getElementById("sampleRate"),
  securityToggle: document.getElementById("securityToggle"),
  securityModeLabel: document.getElementById("securityModeLabel"),
  securityStatusBadge: document.getElementById("securityStatusBadge"),
  securityOnBtn: document.getElementById("securityOnBtn"),
  securityOffBtn: document.getElementById("securityOffBtn"),
  commandLog: document.getElementById("commandLog"),
  historyBody: document.getElementById("historyBody"),
  alarmOverlay: document.getElementById("alarmOverlay"),
  wsUrl: document.getElementById("wsUrl"),
  connectionState: document.getElementById("connectionState"),
  connectWsBtn: document.getElementById("connectWsBtn"),
  connectSerialBtn: document.getElementById("connectSerialBtn"),
  disconnectBtn: document.getElementById("disconnectBtn"),
  clearHistoryBtn: document.getElementById("clearHistoryBtn"),
  downloadCsvBtn: document.getElementById("downloadCsvBtn"),
  dataSource: document.getElementById("dataSource"),
  lastUpdate: document.getElementById("lastUpdate"),
  sampleCount: document.getElementById("sampleCount"),
  liveNow: document.getElementById("liveNow"),
  liveMin: document.getElementById("liveMin"),
  liveMax: document.getElementById("liveMax"),
  liveAvg: document.getElementById("liveAvg"),
  autoScaleToggle: document.getElementById("autoScaleToggle"),
  pauseChartBtn: document.getElementById("pauseChartBtn"),
};

const CHART_MAX_POINTS = 120;
const ENERGY_RING_CIRC = 2 * Math.PI * 50;
const ENERGY_TARGET_MJ = 15000;
let chart;

function initChart() {
  const ctx = document.getElementById("liveChart");
  chart = new Chart(ctx, {
    type: "line",
    data: {
      labels: [],
      datasets: [
        {
          label: "Piezo Voltaj",
          data: [],
          borderColor: "#52e3f6",
          backgroundColor: "rgba(82, 227, 246, 0.15)",
          borderWidth: 2.6,
          pointRadius: 0,
          cubicInterpolationMode: "monotone",
          tension: 0.32,
          fill: true,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: {
        duration: 120,
      },
      plugins: {
        legend: {
          labels: {
            color: "#c7eaf5",
          },
        },
        tooltip: {
          callbacks: {
            label: (context) => `Voltaj: ${Number(context.parsed.y).toFixed(2)} V`,
          },
        },
      },
      scales: {
        x: {
          ticks: {
            color: "#9fc6d2",
            maxTicksLimit: 8,
          },
          grid: {
            color: "rgba(159, 198, 210, 0.15)",
          },
        },
        y: {
          min: 0,
          max: 5,
          ticks: {
            color: "#9fc6d2",
          },
          grid: {
            color: "rgba(159, 198, 210, 0.15)",
          },
        },
      },
    },
  });
}

function updateLiveStats(series, currentVoltage) {
  if (!series.length) {
    ui.liveNow.textContent = "0.00 V";
    ui.liveMin.textContent = "0.00 V";
    ui.liveMax.textContent = "0.00 V";
    ui.liveAvg.textContent = "0.00 V";
    return;
  }

  const min = Math.min(...series);
  const max = Math.max(...series);
  const avg = series.reduce((acc, value) => acc + value, 0) / series.length;

  ui.liveNow.textContent = `${currentVoltage.toFixed(2)} V`;
  ui.liveMin.textContent = `${min.toFixed(2)} V`;
  ui.liveMax.textContent = `${max.toFixed(2)} V`;
  ui.liveAvg.textContent = `${avg.toFixed(2)} V`;
}

function applyYAxisScaling(series) {
  if (!ui.autoScaleToggle.checked || !series.length) {
    chart.options.scales.y.min = 0;
    chart.options.scales.y.max = 5;
    return;
  }

  const min = Math.min(...series);
  const max = Math.max(...series);
  const paddedMin = Math.max(0, min - 0.2);
  const paddedMax = Math.min(5, max + 0.2);
  const safeMax = paddedMax - paddedMin < 0.6 ? Math.min(5, paddedMin + 0.6) : paddedMax;

  chart.options.scales.y.min = paddedMin;
  chart.options.scales.y.max = safeMax;
}

function setConnectionState(text, isConnected = false) {
  ui.connectionState.textContent = text;
  ui.connectionState.classList.toggle("connected", isConnected);
}

function setDataSource(source) {
  state.dataSource = source;
  ui.dataSource.textContent = source;
}

function updateUI() {
  ui.stepCounter.textContent = String(state.steps).padStart(6, "0");
  ui.energyValue.textContent = Math.round(state.energyMj).toLocaleString("tr-TR");

  const clampedVoltage = Math.max(0, Math.min(5, state.voltage));
  ui.voltageValue.textContent = `${clampedVoltage.toFixed(2)} V`;
  ui.voltageFill.style.width = `${(clampedVoltage / 5) * 100}%`;

  const ratio = Math.max(0, Math.min(1, state.energyMj / ENERGY_TARGET_MJ));
  const offset = ENERGY_RING_CIRC - ratio * ENERGY_RING_CIRC;
  ui.energyRing.style.strokeDashoffset = String(offset);

  ui.securityModeLabel.textContent = state.securityMode ? "GMOD:1" : "GMOD:0";
  ui.securityModeLabel.classList.toggle("off", !state.securityMode);
  ui.securityToggle.checked = state.securityMode;
  ui.securityStatusBadge.textContent = state.securityMode ? "Aktif" : "Pasif";
  ui.securityStatusBadge.classList.toggle("active", state.securityMode);
  ui.securityStatusBadge.classList.toggle("passive", !state.securityMode);
  ui.sampleRate.textContent = `Ornek: ${state.sampleRate} Hz`;
  ui.sampleCount.textContent = String(state.sampleCount);
  ui.lastUpdate.textContent = state.lastUpdateLabel;
}

function addHistoryRow(ts, steps, voltage) {
  state.history.unshift({ ts, steps, voltage });
  if (state.history.length > 150) {
    state.history.length = 150;
  }

  ui.historyBody.innerHTML = "";
  for (const row of state.history.slice(0, 30)) {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${new Date(row.ts).toLocaleString("tr-TR")}</td>
      <td>${row.steps}</td>
      <td>${row.voltage.toFixed(2)}</td>
    `;
    ui.historyBody.appendChild(tr);
  }
}

function addChartPoint(voltage) {
  const now = Date.now();
  const label = new Date(now).toLocaleTimeString("tr-TR");

  if (state.lastSampleTs > 0) {
    const dt = now - state.lastSampleTs;
    if (dt > 0) {
      state.sampleRate = Math.round(1000 / dt);
    }
  }
  state.lastSampleTs = now;

  if (!state.chartPaused) {
    chart.data.labels.push(label);
    chart.data.datasets[0].data.push(voltage);

    if (chart.data.labels.length > CHART_MAX_POINTS) {
      chart.data.labels.shift();
      chart.data.datasets[0].data.shift();
    }

    const series = chart.data.datasets[0].data;
    applyYAxisScaling(series);
    updateLiveStats(series, voltage);
    chart.update("none");
    return;
  }

  updateLiveStats(chart.data.datasets[0].data, voltage);
}

function showAlarm() {
  ui.alarmOverlay.classList.remove("hidden");
  window.clearTimeout(showAlarm.hideTimer);
  showAlarm.hideTimer = window.setTimeout(() => {
    ui.alarmOverlay.classList.add("hidden");
  }, 5500);
}

function normalizePayload(raw) {
  if (typeof raw === "string") {
    if (raw.trim().toUpperCase() === "ALARM") {
      return { alarm: true };
    }

    try {
      return JSON.parse(raw);
    } catch {
      const data = {};
      raw.split(";").forEach((part) => {
        const [key, value] = part.split(":");
        if (!key || value === undefined) return;
        const k = key.trim().toLowerCase();
        const v = value.trim();
        if (k === "steps") data.steps = Number(v);
        if (k === "voltage") data.voltage = Number(v);
        if (k === "energy_mj" || k === "energy") data.energy_mj = Number(v);
        if (k === "alarm") data.alarm = v === "1" || v.toLowerCase() === "true";
      });
      return data;
    }
  }

  if (typeof raw === "object" && raw !== null) {
    return raw;
  }

  return {};
}

function ingestData(raw) {
  const data = normalizePayload(raw);
  state.sampleCount += 1;
  state.lastUpdateLabel = new Date().toLocaleTimeString("tr-TR");

  if (data.alarm) {
    showAlarm();
  }

  if (Number.isFinite(data.steps)) {
    state.steps = Math.max(0, data.steps);
  }

  if (Number.isFinite(data.energy_mj)) {
    state.energyMj = Math.max(0, data.energy_mj);
  }

  if (Number.isFinite(data.voltage)) {
    state.voltage = data.voltage;
    addChartPoint(state.voltage);
    addHistoryRow(Date.now(), state.steps, state.voltage);
  }

  // Güvenlik modu backend durumunu yansiit
  if (data.security_mode !== undefined) {
    const backendSecMode = Boolean(data.security_mode);
    if (state.securityMode !== backendSecMode) {
      state.securityMode = backendSecMode;
      ui.securityToggle.checked = backendSecMode;
    }
  }

  updateUI();
}

function sendCommand(command) {
  let sent = false;

  if (state.ws && state.ws.readyState === WebSocket.OPEN) {
    state.ws.send(command);
    sent = true;
  }

  if (state.serialWriter) {
    const encoder = new TextEncoder();
    state.serialWriter.write(encoder.encode(`${command}\n`));
    sent = true;
  }

  ui.commandLog.textContent = `Son komut: ${command} ${sent ? "(gonderildi)" : "(baglanti yok)"}`;
}

function setupWebSocket() {
  ui.connectWsBtn.addEventListener("click", () => {
    const url = ui.wsUrl.value.trim();
    if (!url) return;

    if (state.ws && state.ws.readyState === WebSocket.OPEN) {
      state.ws.close();
    }

    setConnectionState("Baglaniyor...");
    const ws = new WebSocket(url);
    state.ws = ws;

    ws.addEventListener("open", () => {
      setConnectionState("WebSocket Bagli", true);
      setDataSource("WebSocket");
    });

    ws.addEventListener("message", (event) => {
      ingestData(event.data);
    });

    ws.addEventListener("close", () => {
      setConnectionState("WebSocket Kapali");
      if (!state.serialPort) {
        setDataSource("Demo");
      }
    });

    ws.addEventListener("error", () => {
      setConnectionState("WebSocket Hata");
    });
  });
}

function getWebSerialErrorMessage(err) {
  const errorName = err && err.name ? err.name : "UnknownError";
  const rawMessage = err && err.message ? err.message : "Bilinmeyen hata";

  if (!window.isSecureContext) {
    return "WebSerial icin guvenli baglam gerekli. Sayfayi localhost/https uzerinden ac.";
  }

  if (errorName === "NotFoundError") {
    return "Port secimi iptal edildi veya uygun seri cihaz bulunamadi.";
  }

  if (errorName === "InvalidStateError") {
    return "Secilen seri port zaten acik veya gecersiz durumda.";
  }

  if (errorName === "NetworkError") {
    return "Seri port acilamadi. Port baska bir uygulama tarafindan kullaniliyor olabilir (ornegin backend/Serial Monitor).";
  }

  if (errorName === "SecurityError") {
    return "Tarayici guvenlik politikasi nedeniyle WebSerial izni vermedi.";
  }

  return `WebSerial hata (${errorName}): ${rawMessage}`;
}

async function setupWebSerial() {
  if (!("serial" in navigator)) {
    ui.commandLog.textContent = "Bu tarayici WebSerial desteklemiyor.";
    return;
  }

  if (!window.isSecureContext) {
    setConnectionState("WebSerial Hata");
    ui.commandLog.textContent =
      "WebSerial sadece guvenli baglamda calisir. localhost veya https kullan.";
    return;
  }

  try {
    if (state.ws && state.ws.readyState === WebSocket.OPEN) {
      ui.commandLog.textContent =
        "Uyari: WebSocket acik. Ayni anda iki baglanti yerine tek kaynagi kullanman onerilir.";
    }

    state.serialPort = await navigator.serial.requestPort();
    await state.serialPort.open({ baudRate: 9600 });

    state.serialWriter = state.serialPort.writable.getWriter();
    state.serialReader = state.serialPort.readable.getReader();

    setConnectionState("WebSerial Bagli", true);
    setDataSource("WebSerial");

    const decoder = new TextDecoder();
    let buffer = "";

    while (state.serialPort.readable) {
      const { value, done } = await state.serialReader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });

      const lines = buffer.split("\n");
      buffer = lines.pop() || "";
      for (const line of lines) {
        if (line.trim()) ingestData(line.trim());
      }
    }
  } catch (err) {
    setConnectionState("WebSerial Hata");
    ui.commandLog.textContent = getWebSerialErrorMessage(err);
    if (!(state.ws && state.ws.readyState === WebSocket.OPEN)) {
      setDataSource("Demo");
    }
  }
}

async function disconnectAll() {
  if (state.ws) {
    state.ws.close();
    state.ws = null;
  }

  if (state.serialReader) {
    try {
      await state.serialReader.cancel();
    } catch {
      // Reader may already be closed.
    }
    state.serialReader.releaseLock();
    state.serialReader = null;
  }

  if (state.serialWriter) {
    try {
      await state.serialWriter.close();
    } catch {
      // Writer may already be closed.
    }
    state.serialWriter.releaseLock();
    state.serialWriter = null;
  }

  if (state.serialPort) {
    try {
      await state.serialPort.close();
    } catch {
      // Port may already be closed.
    }
    state.serialPort = null;
  }

  setConnectionState("Bagli Degil");
  setDataSource("Demo");
}

function clearHistory() {
  state.history.length = 0;
  ui.historyBody.innerHTML = "";
  ui.commandLog.textContent = "Kayitlar temizlendi.";
}

function toggleChartPause() {
  state.chartPaused = !state.chartPaused;
  ui.pauseChartBtn.textContent = state.chartPaused ? "Akisi Devam Ettir" : "Akisi Duraklat";
}

function onAutoScaleChanged() {
  applyYAxisScaling(chart.data.datasets[0].data);
  chart.update("none");
}

function setupSecurityToggle() {
  ui.securityToggle.addEventListener("change", (e) => {
    state.securityMode = e.target.checked;
    updateUI();
    sendCommand(state.securityMode ? "GMOD:1" : "GMOD:0");
  });

  // Güvenlik Modu Aç butonu
  ui.securityOnBtn.addEventListener("click", () => {
    if (!state.securityMode) {
      state.securityMode = true;
      ui.securityToggle.checked = true;
      updateUI();
      sendCommand("GMOD:1");
    }
  });

  // Güvenlik Modu Kapa butonu
  ui.securityOffBtn.addEventListener("click", () => {
    if (state.securityMode) {
      state.securityMode = false;
      ui.securityToggle.checked = false;
      updateUI();
      sendCommand("GMOD:0");
    }
  });
}

function downloadCsv() {
  const rows = ["tarih,adim,voltage"]; 
  for (const row of state.history) {
    rows.push(`${new Date(row.ts).toISOString()},${row.steps},${row.voltage.toFixed(2)}`);
  }

  const blob = new Blob([rows.join("\n")], { type: "text/csv;charset=utf-8;" });
  const url = URL.createObjectURL(blob);

  const a = document.createElement("a");
  a.href = url;
  a.download = `zemin_veri_${new Date().toISOString().slice(0, 10)}.csv`;
  a.click();

  URL.revokeObjectURL(url);
}

function startDemoFeed() {
  // Gercek baglanti gelene kadar arayuzu canli gostermek icin demo akisi.
  let t = 0;
  window.setInterval(() => {
    if ((state.ws && state.ws.readyState === WebSocket.OPEN) || state.serialPort) {
      return;
    }

    t += 1;
    const voltage = Math.max(0, Math.min(5, 2.4 + Math.sin(t / 5) * 1.6 + Math.random() * 0.35));
    const steps = state.steps + (Math.random() > 0.45 ? 1 : 0);
    const energy = state.energyMj + voltage * 4.8;

    ingestData({
      steps,
      voltage,
      energy_mj: energy,
      alarm: Math.random() > 0.992,
    });
  }, 220);
}

function bindEvents() {
  ui.connectSerialBtn.addEventListener("click", setupWebSerial);
  ui.disconnectBtn.addEventListener("click", () => {
    disconnectAll();
  });
  ui.clearHistoryBtn.addEventListener("click", clearHistory);
  ui.downloadCsvBtn.addEventListener("click", downloadCsv);
  ui.pauseChartBtn.addEventListener("click", toggleChartPause);
  ui.autoScaleToggle.addEventListener("change", onAutoScaleChanged);
}

function init() {
  initChart();
  setupWebSocket();
  setupSecurityToggle();
  bindEvents();
  updateUI();
  startDemoFeed();
}

init();
