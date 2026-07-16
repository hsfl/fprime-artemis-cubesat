const currentTab = document.getElementById("currentTab");
const historyTab = document.getElementById("historyTab");
const currentView = document.getElementById("currentView");
const historyView = document.getElementById("historyView");
const currentPanel = document.querySelector(".current-panel");
const stateHeading = document.getElementById("stateHeading");
const stateSupporting = document.getElementById("stateSupporting");
const stateAnnouncement = document.getElementById("stateAnnouncement");
const timingBanner = document.getElementById("timingBanner");
const portChooser = document.getElementById("portChooser");
const portSelect = document.getElementById("portSelect");
const connectButton = document.getElementById("connectButton");
const connectionIndicator = document.getElementById("connectionIndicator");
const systemTime = document.getElementById("systemTime");
const productValue = document.getElementById("productValue");
const transferValue = document.getElementById("transferValue");
const bytesValue = document.getElementById("bytesValue");
const packetsValue = document.getElementById("packetsValue");
const progressValue = document.getElementById("progressValue");
const progressPercent = document.getElementById("progressPercent");
const progressBar = document.getElementById("progressBar");
const elapsedValue = document.getElementById("elapsedValue");
const etaValue = document.getElementById("etaValue");
const retryValue = document.getElementById("retryValue");
const integrityValue = document.getElementById("integrityValue");
const transferActions = document.getElementById("transferActions");
const cancelTransferButton = document.getElementById("cancelTransferButton");
const liveLog = document.getElementById("liveLog");
const clearLogButton = document.getElementById("clearLogButton");
const previewContent = document.getElementById("previewContent");
const outputActions = document.getElementById("outputActions");
const outputLinks = document.getElementById("outputLinks");
const openFolderButton = document.getElementById("openFolderButton");
const errorActions = document.getElementById("errorActions");
const reconnectButton = document.getElementById("reconnectButton");
const recentHistoryHeading = document.getElementById("recentHistoryHeading");
const recentHistoryList = document.getElementById("recentHistoryList");
const viewAllHistoryButton = document.getElementById("viewAllHistoryButton");
const fullHistoryList = document.getElementById("fullHistoryList");
const archivedContent = document.getElementById("archivedContent");
const footerConnection = document.getElementById("footerConnection");
const footerPort = document.getElementById("footerPort");

let latestState = null;
let selectedHistoryRun = null;
let clearedLogCount = 0;
let activeTab = "current";
let portSignature = "";
let logSignature = "";
let previewSignature = "";
let historySignature = "";
let stateSignature = "";

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function formatDuration(value) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) return "—";
  const seconds = Math.max(0, Math.round(Number(value)));
  const minutes = Math.floor(seconds / 60);
  return `${String(minutes).padStart(2, "0")}:${String(seconds % 60).padStart(2, "0")}`;
}

function formatNumber(value) {
  if (value === null || value === undefined || value === 0) return value === 0 ? "0" : "—";
  return Number(value).toLocaleString();
}

function formatTimestamp(value) {
  if (!value) return "—";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return `${date.toLocaleString([], { dateStyle: "medium", timeStyle: "medium", timeZone: "UTC" })} UTC`;
}

function headingFor(current) {
  if (current.status === "failed") {
    if (current.crc_ok === false) return "CRC failed";
    return current.message || "Payload failed";
  }
  const headings = {
    starting: "Starting payload receiver",
    select_port: "Select payload port",
    ready: "Ready — awaiting downlink",
    receiving: "Receiving payload",
    retrying: "Retrying missing packets",
    cancelling: "Saving partial payload",
    verifying: "Verifying payload integrity",
    decoding: "Decoding thermal product",
    complete: "Payload complete",
    partial: "Partial — viewable with missing data",
    disconnected: "Payload receiver disconnected",
  };
  return headings[current.status] || "Payload receiver";
}

function supportingFor(current) {
  if (current.status === "ready") return `Listening on ${current.port || "Channel 1"}`;
  if (current.status === "receiving") return `${formatNumber(current.received_packets)} / ${formatNumber(current.total_packets)} packets`;
  if (current.status === "retrying") return `${formatNumber(current.missing_packets)} packets missing · Retry round ${formatNumber(current.retry_rounds)}`;
  if (current.status === "cancelling") return "Preserving received packets; satellite transmission continues";
  if (current.status === "complete") return "CRC passed · Current thermal product decoded";
  if (current.status === "partial") {
    const prefix = current.completion_reason === "operator_cancelled" ? "Stopped by operator · " : "";
    return `${prefix}${formatNumber(current.missing_packets)} packets missing · Unknown pixels shown in white`;
  }
  if (current.status === "failed") return current.failure_reason || "The current transfer could not be completed.";
  if (current.status === "disconnected" || current.status === "select_port") return current.failure_reason || current.message;
  return current.message || "Opening the Channel 1 payload link.";
}

function integrityFor(current) {
  if (current.partial) return ["Partial · CRC unavailable", "is-warning"];
  if (current.crc_ok === true) return ["CRC passed", "is-success"];
  if (current.crc_ok === false) return ["CRC failed", "is-danger"];
  if (["receiving", "retrying", "verifying"].includes(current.status)) return ["CRC pending", "is-warning"];
  return ["Waiting", ""];
}

function showTab(tab) {
  activeTab = tab;
  const currentSelected = tab === "current";
  currentTab.classList.toggle("is-selected", currentSelected);
  currentTab.setAttribute("aria-selected", String(currentSelected));
  currentTab.tabIndex = currentSelected ? 0 : -1;
  historyTab.classList.toggle("is-selected", !currentSelected);
  historyTab.setAttribute("aria-selected", String(!currentSelected));
  historyTab.tabIndex = currentSelected ? -1 : 0;
  currentView.hidden = !currentSelected;
  historyView.hidden = currentSelected;
  if (latestState) renderHistory(latestState.history || []);
}

function renderPorts(ports, current) {
  const nextSignature = JSON.stringify(ports.map((item) => [item.device, item.description, item.likely_payload]));
  if (nextSignature === portSignature) {
    portChooser.hidden = current.status !== "select_port" && current.status !== "disconnected";
    connectButton.disabled = !portSelect.value;
    return;
  }
  portSignature = nextSignature;
  const previous = portSelect.value;
  portSelect.innerHTML = "";
  ports.forEach((item) => {
    const option = document.createElement("option");
    option.value = item.device;
    option.textContent = `${item.device} — ${item.description}${item.likely_payload ? " (payload)" : ""}`;
    portSelect.appendChild(option);
  });
  if (ports.some((item) => item.device === previous)) portSelect.value = previous;
  else {
    const likely = ports.find((item) => item.likely_payload);
    if (likely) portSelect.value = likely.device;
  }
  portChooser.hidden = current.status !== "select_port" && current.status !== "disconnected";
  connectButton.disabled = !portSelect.value;
}

function renderConnection(current) {
  const label = current.connected ? "Receiver connected" : "Receiver disconnected";
  connectionIndicator.classList.toggle("is-connected", current.connected);
  connectionIndicator.querySelector("span:last-child").textContent = label;
  footerConnection.classList.toggle("is-connected", current.connected);
  footerConnection.innerHTML = `<span class="status-dot ${current.connected ? "is-connected" : ""}"></span>${label}`;
  footerPort.textContent = current.port ? `Listening on ${current.port}` : "No payload port selected";
}

function renderTiming(current) {
  if (current.timing_band === "degraded") {
    timingBanner.hidden = false;
    timingBanner.className = "timing-banner";
    timingBanner.textContent = Number(current.elapsed_seconds || 0) >= 90
      ? "Longer than target — transfer remains inside the 120 s live-demo cutoff."
      : "Past the 75 s nominal target — transfer remains within the live-demo window.";
  } else if (current.timing_band === "delayed") {
    timingBanner.hidden = false;
    timingBanner.className = "timing-banner is-delayed";
    timingBanner.textContent = "120 s cutoff reached — operator attention. The receiver will preserve best-effort partial data if CRC repair cannot finish.";
  } else {
    timingBanner.hidden = true;
  }
}

function renderLogs(logs) {
  const visible = logs.slice(clearedLogCount);
  const nextSignature = JSON.stringify(visible.map((entry) => [entry.timestamp, entry.level, entry.message]));
  if (nextSignature === logSignature) return;
  const wasAtBottom = liveLog.scrollHeight - liveLog.scrollTop - liveLog.clientHeight < 24;
  logSignature = nextSignature;
  if (!visible.length) {
    liveLog.innerHTML = '<p class="history-empty">Receiver events will appear here.</p>';
    return;
  }
  liveLog.innerHTML = visible.map((entry) => {
    const stamp = entry.timestamp ? new Date(entry.timestamp).toISOString().slice(11, 19) : "--:--:--";
    return `<div class="log-entry is-${escapeHtml(entry.level)}"><time>${stamp}</time><span>${escapeHtml(entry.message)}</span></div>`;
  }).join("");
  if (wasAtBottom) liveLog.scrollTop = liveLog.scrollHeight;
}

function renderPreview(current) {
  const nextSignature = JSON.stringify([
    current.status,
    current.product_id,
    current.transfer_id,
    current.run_id,
    current.crc_ok,
    current.failure_reason,
    current.outputs || {},
    current.decode || {},
  ]);
  if (nextSignature === previewSignature) return;
  previewSignature = nextSignature;
  const png = current.outputs?.png;
  if (png && ["complete", "partial"].includes(current.status)) {
    const decode = current.decode || {};
    previewContent.innerHTML = `
      <div class="thermal-inspector">
        <img id="thermalImage" src="${escapeHtml(png)}" alt="Current Lepton thermal image for product ${escapeHtml(current.product_id)}, transfer ${escapeHtml(current.transfer_id)}">
        <output id="thermalHover" class="thermal-hover">Move over image to inspect temperature</output>
      </div>
      <div class="thermal-stats">
        <span>Min <strong>${decode.min_c ?? "—"}°C</strong></span>
        <span>Max <strong>${decode.max_c ?? "—"}°C</strong></span>
        <span>Mean <strong>${decode.mean_c ?? "—"}°C</strong></span>
        <span>Range <strong>${decode.min_c ?? "—"}–${decode.max_c ?? "—"}°C</strong></span>
      </div>`;
    attachThermalInspection({
      csvUrl: current.outputs?.csv,
      width: decode.width || 160,
      height: decode.height || 120,
      imageId: "thermalImage",
      outputId: "thermalHover",
    });
  } else {
    let message = "Available after CRC verification";
    if (current.crc_ok === false) message = "Thermal preview unavailable because integrity verification failed.";
    else if (current.status === "failed") message = current.failure_reason || "Thermal preview unavailable.";
    previewContent.innerHTML = `<div class="preview-empty"><span class="thermometer" aria-hidden="true"></span><p>${escapeHtml(message)}</p></div>`;
  }

  const outputs = current.outputs || {};
  const keys = ["fdp", "json", "csv", "png"].filter((key) => outputs[key]);
  outputActions.hidden = keys.length === 0;
  outputLinks.innerHTML = keys.map((key) => `<a href="${escapeHtml(outputs[key])}" target="_blank" rel="noopener">${key === "fdp" ? ".fdp" : key.toUpperCase()}</a>`).join("");
  openFolderButton.hidden = !current.run_id;
  openFolderButton.dataset.runId = current.run_id || "";
}

async function attachThermalInspection({ csvUrl, width, height, imageId, outputId, overlay = false }) {
  const image = document.getElementById(imageId);
  const hover = document.getElementById(outputId);
  if (!image || !hover || !csvUrl) return;
  let grid;
  try {
    const response = await fetch(csvUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`Temperature CSV unavailable: ${response.status}`);
    const text = await response.text();
    grid = text.split(/\r?\n/)
      .filter((line) => line && !line.startsWith("#"))
      .map((line) => line.split(",").map((value) => {
        const normalized = value.trim();
        return normalized === "" || normalized === "NaN" ? null : Number(normalized);
      }));
  } catch (_error) {
    hover.textContent = "Temperature data unavailable";
    if (overlay) hover.classList.add("is-visible");
    return;
  }
  image.addEventListener("mousemove", (event) => {
    const rect = image.getBoundingClientRect();
    const column = Math.min(width - 1, Math.max(0, Math.floor((event.clientX - rect.left) * width / rect.width)));
    const row = Math.min(height - 1, Math.max(0, Math.floor((event.clientY - rect.top) * height / rect.height)));
    const value = grid?.[row]?.[column];
    hover.textContent = value === null || !Number.isFinite(value)
      ? `Column ${column}, row ${row} · No data`
      : `Column ${column}, row ${row} · ${value.toFixed(2)}°C`;
    if (overlay) hover.classList.add("is-visible");
  });
  image.addEventListener("mouseleave", () => {
    hover.textContent = "Move over image to inspect temperature";
    if (overlay) hover.classList.remove("is-visible");
  });
}

function runResult(run) {
  if (run.result === "complete") return ["Complete", "run-complete"];
  if (run.result === "partial" && run.completion_reason === "operator_cancelled") return ["Partial · stopped", "run-partial"];
  if (run.result === "partial") return ["Partial", "run-partial"];
  if (run.result === "decode_failed") return ["Decode failed", "run-failed"];
  if (run.result === "crc_failed") return ["CRC failed", "run-failed"];
  return run.crc_ok ? ["Complete", "run-complete"] : ["CRC failed", "run-failed"];
}

function historyItem(run, compact = false) {
  const png = run.output_urls?.png;
  const [resultLabel, resultClass] = runResult(run);
  const selectedClass = activeTab === "history" && selectedHistoryRun === run.run_id ? "is-selected" : "";
  return `
    <button class="history-item ${selectedClass}" data-run-id="${escapeHtml(run.run_id)}" type="button">
      ${png ? `<img src="${escapeHtml(png)}" alt="Archived thermal preview for product ${escapeHtml(run.product_id)}">` : '<div class="preview-empty">No preview</div>'}
      <span class="history-item-copy">
        <span class="history-item-title"><span>Product ${escapeHtml(run.product_id ?? "—")}</span><span class="${resultClass}">${resultLabel}</span></span>
        <span class="history-item-meta">Transfer ${escapeHtml(run.transfer_id ?? "—")} · ${escapeHtml(formatTimestamp(run.completed_at))}</span>
        <span class="history-item-meta">${formatNumber(run.total_bytes)} bytes · ${formatNumber(run.total_packets)} packets</span>
        <span class="history-item-meta">Duration ${formatDuration(run.elapsed_seconds)} · Retries ${formatNumber(run.retry_rounds)}</span>
      </span>
    </button>`;
}

function bindHistoryButtons(container, history) {
  container.querySelectorAll(".history-item").forEach((button) => {
    button.addEventListener("click", () => {
      selectedHistoryRun = button.dataset.runId;
      showTab("history");
    });
  });
}

function renderHistory(history) {
  if (selectedHistoryRun && !history.some((run) => run.run_id === selectedHistoryRun)) {
    selectedHistoryRun = null;
  }
  if (activeTab === "history" && !selectedHistoryRun && history.length) {
    selectedHistoryRun = history[0].run_id;
  }
  const nextSignature = JSON.stringify([
    activeTab,
    selectedHistoryRun,
    history.map((run) => [
      run.run_id,
      run.result,
      run.crc_ok,
      run.completion_reason,
      run.product_id,
      run.transfer_id,
      run.completed_at,
      run.elapsed_seconds,
      run.retry_rounds,
      run.total_bytes,
      run.total_packets,
      run.output_urls || {},
    ]),
  ]);
  if (nextSignature === historySignature) return;
  historySignature = nextSignature;
  recentHistoryHeading.textContent = `History (${history.length})`;
  const recent = history.slice(0, 3);
  recentHistoryList.innerHTML = recent.length ? recent.map((run) => historyItem(run, true)).join("") : '<p class="history-empty">Completed payloads will appear here.</p>';
  bindHistoryButtons(recentHistoryList, history);

  fullHistoryList.innerHTML = history.length ? history.map((run) => historyItem(run)).join("") : '<p class="history-empty">No payload history yet.</p>';
  bindHistoryButtons(fullHistoryList, history);

  const selected = history.find((run) => run.run_id === selectedHistoryRun);
  if (!selected) {
    archivedContent.innerHTML = "<p>Select a run to view archived payload data.</p>";
    return;
  }
  const png = selected.output_urls?.png;
  const [resultLabel, resultClass] = runResult(selected);
  const links = Object.entries(selected.output_urls || {}).map(([key, value]) => `<a href="${escapeHtml(value)}" target="_blank" rel="noopener">${key === "fdp" ? ".fdp" : key.toUpperCase()}</a>`).join(" · ");
  archivedContent.innerHTML = `
    <h2>Product ${escapeHtml(selected.product_id ?? "—")}</h2>
    <p class="archived-result ${resultClass}">${resultLabel}</p>
    <div class="archived-meta">
      <div><span>Transfer</span><strong>${escapeHtml(selected.transfer_id ?? "—")}</strong></div>
      <div><span>Duration</span><strong>${formatDuration(selected.elapsed_seconds)}</strong></div>
      <div><span>Retries</span><strong>${formatNumber(selected.retry_rounds)}</strong></div>
      <div><span>Integrity</span><strong class="${selected.partial ? "is-warning" : (selected.crc_ok ? "crc-pass" : "crc-fail")}">${selected.partial ? "Partial · no CRC" : (selected.crc_ok ? "CRC passed" : "CRC failed")}</strong></div>
    </div>
    ${png ? `
      <div class="thermal-inspector archived-thermal-inspector">
        <img id="archivedThermalImage" src="${escapeHtml(png)}" alt="Archived Lepton thermal image for product ${escapeHtml(selected.product_id)}, transfer ${escapeHtml(selected.transfer_id)}">
        <output id="archivedThermalHover" class="thermal-tooltip" aria-live="polite">Move over image to inspect temperature</output>
      </div>` : ""}
    <p>${links || "No output files available."}</p>`;
  if (png && selected.output_urls?.csv) {
    attachThermalInspection({
      csvUrl: selected.output_urls?.csv,
      width: selected.decode?.width || 160,
      height: selected.decode?.height || 120,
      imageId: "archivedThermalImage",
      outputId: "archivedThermalHover",
      overlay: true,
    });
  }
}

function render(data) {
  latestState = data;
  const current = data.current;
  currentPanel.dataset.state = current.status;
  const nextHeading = headingFor(current);
  const nextSupporting = supportingFor(current);
  stateHeading.textContent = nextHeading;
  stateSupporting.textContent = nextSupporting;
  const nextStateSignature = `${current.status}|${nextHeading}|${nextSupporting}`;
  if (nextStateSignature !== stateSignature) {
    stateSignature = nextStateSignature;
    stateAnnouncement.textContent = `${nextHeading}. ${nextSupporting}`;
  }
  renderConnection(current);
  renderPorts(data.ports || [], current);
  renderTiming(current);

  productValue.textContent = current.product_id ?? "—";
  transferValue.textContent = current.transfer_id ?? "—";
  bytesValue.textContent = current.total_bytes ? `${formatNumber(current.total_bytes)} bytes` : "—";
  packetsValue.textContent = current.total_packets ? `${formatNumber(current.total_packets)} packets` : "—";
  progressValue.textContent = `${formatNumber(current.received_packets)} / ${formatNumber(current.total_packets)} packets`;
  const percent = Math.round((current.progress_fraction || 0) * 100);
  progressPercent.textContent = `${percent}%`;
  progressBar.value = current.progress_fraction || 0;
  progressBar.textContent = `${percent}%`;
  elapsedValue.textContent = formatDuration(current.elapsed_seconds);
  etaValue.textContent = formatDuration(current.estimated_remaining_seconds);
  retryValue.textContent = formatNumber(current.retry_rounds);
  const [integrityText, integrityClass] = integrityFor(current);
  integrityValue.textContent = integrityText;
  integrityValue.className = integrityClass;
  const activeDelayed = current.timing_band === "delayed"
    && ["receiving", "retrying", "verifying", "decoding"].includes(current.status);
  errorActions.hidden = current.status !== "disconnected" && !activeDelayed;
  reconnectButton.textContent = activeDelayed ? "Reset receiver" : "Reconnect receiver";
  const canCancel = ["receiving", "retrying"].includes(current.status)
    && current.transfer_id !== null
    && Number(current.total_packets || 0) > 0;
  transferActions.hidden = !canCancel && current.status !== "cancelling";
  cancelTransferButton.disabled = current.status === "cancelling";
  cancelTransferButton.textContent = current.status === "cancelling"
    ? "Saving partial…"
    : "Stop & save partial";

  renderLogs(data.logs || []);
  renderPreview(current);
  renderHistory(data.history || []);
}

async function postJson(path, payload = {}) {
  const response = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  const data = await response.json();
  if (!response.ok || data.error) throw new Error(data.error || `Request failed: ${response.status}`);
  return data;
}

async function refresh() {
  try {
    const response = await fetch("/api/state", { cache: "no-store" });
    const data = await response.json();
    if (!response.ok || data.error) throw new Error(data.error || "State unavailable");
    render(data);
  } catch (error) {
    stateHeading.textContent = "Payload web app unavailable";
    stateSupporting.textContent = error.message;
  } finally {
    window.setTimeout(refresh, 500);
  }
}

currentTab.addEventListener("click", () => showTab("current"));
historyTab.addEventListener("click", () => showTab("history"));
const tabs = [currentTab, historyTab];
tabs.forEach((tab, index) => {
  tab.addEventListener("keydown", (event) => {
    let nextIndex = null;
    if (event.key === "ArrowRight") nextIndex = (index + 1) % tabs.length;
    else if (event.key === "ArrowLeft") nextIndex = (index - 1 + tabs.length) % tabs.length;
    else if (event.key === "Home") nextIndex = 0;
    else if (event.key === "End") nextIndex = tabs.length - 1;
    if (nextIndex === null) return;
    event.preventDefault();
    tabs[nextIndex].focus();
    showTab(nextIndex === 0 ? "current" : "history");
  });
});
viewAllHistoryButton.addEventListener("click", () => showTab("history"));
clearLogButton.addEventListener("click", () => {
  clearedLogCount = latestState?.logs?.length || 0;
  renderLogs(latestState?.logs || []);
});
connectButton.addEventListener("click", async () => {
  connectButton.disabled = true;
  try { await postJson("/api/connect", { port: portSelect.value }); }
  catch (error) {
    stateSupporting.textContent = error.message;
    connectButton.disabled = !portSelect.value;
  }
});
reconnectButton.addEventListener("click", async () => {
  reconnectButton.disabled = true;
  try { await postJson("/api/reconnect"); }
  catch (error) { stateSupporting.textContent = error.message; }
  finally { reconnectButton.disabled = false; }
});
cancelTransferButton.addEventListener("click", async () => {
  const confirmed = window.confirm(
    "Stop ground reception and save the packets received so far? The satellite will continue transmitting the rest of this downlink."
  );
  if (!confirmed) return;
  cancelTransferButton.disabled = true;
  cancelTransferButton.textContent = "Saving partial…";
  try { await postJson("/api/transfer/cancel"); }
  catch (error) {
    stateSupporting.textContent = error.message;
    cancelTransferButton.disabled = false;
    cancelTransferButton.textContent = "Stop & save partial";
  }
});
openFolderButton.addEventListener("click", async () => {
  try { await postJson("/api/open-folder", { run_id: openFolderButton.dataset.runId }); }
  catch (error) { stateSupporting.textContent = error.message; }
});

function updateSystemTime() {
  const now = new Date();
  systemTime.textContent = `System time: ${now.toISOString().slice(0, 19).replace("T", " ")} UTC`;
}

updateSystemTime();
setInterval(updateSystemTime, 1000);

showTab("current");
refresh();
