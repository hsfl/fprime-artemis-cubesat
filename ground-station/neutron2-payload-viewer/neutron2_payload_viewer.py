#!/usr/bin/env python3
"""Neutron 2 payload CSV viewer for reconstructed science products."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import tempfile
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse


DEFAULT_CAPTURE_DIR = Path(tempfile.gettempdir()) / "neutron_payload_captures"
MAX_INLINE_ROWS = 5000
CLEANUP_NOTICE_THRESHOLD = 10


HTML = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Neutron 2 Payload Viewer</title>
  <style>
    :root {
      color-scheme: light;
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      color: #172026;
      background: #f5f7f9;
    }
    body { margin: 0; }
    header {
      background: #0f2533;
      color: white;
      padding: 18px 24px;
      border-bottom: 4px solid #2ea86f;
    }
    header h1 {
      margin: 0 0 4px;
      font-size: 24px;
      letter-spacing: 0;
    }
    header p { margin: 0; color: #c8d6df; font-size: 14px; }
    main { padding: 20px 24px 32px; max-width: 1180px; margin: 0 auto; }
    .toolbar {
      display: flex;
      gap: 12px;
      align-items: center;
      flex-wrap: wrap;
      margin-bottom: 18px;
    }
    select, button {
      height: 36px;
      border: 1px solid #b6c2cb;
      background: white;
      color: #172026;
      border-radius: 6px;
      padding: 0 10px;
      font-size: 14px;
    }
    button { cursor: pointer; background: #eaf4ee; border-color: #91c9a8; }
    .grid {
      display: grid;
      grid-template-columns: repeat(5, minmax(120px, 1fr));
      gap: 10px;
      margin-bottom: 16px;
    }
    .metric {
      background: white;
      border: 1px solid #d6dee4;
      border-radius: 8px;
      padding: 12px;
      min-height: 70px;
    }
    .metric .label { color: #5d6b76; font-size: 12px; margin-bottom: 6px; }
    .metric .value { font-weight: 700; font-size: 22px; }
    .panel {
      background: white;
      border: 1px solid #d6dee4;
      border-radius: 8px;
      padding: 14px;
      margin-bottom: 16px;
    }
    .panel h2 {
      margin: 0 0 10px;
      font-size: 16px;
      letter-spacing: 0;
    }
    .docs {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 16px;
    }
    .docs h3 {
      margin: 0 0 8px;
      font-size: 14px;
      color: #20313b;
    }
    .docs p {
      margin: 0 0 8px;
      color: #40505b;
      font-size: 13px;
      line-height: 1.45;
    }
    dl {
      margin: 0;
      display: grid;
      grid-template-columns: auto minmax(0, 1fr);
      column-gap: 10px;
      row-gap: 7px;
      font-size: 13px;
    }
    dt { font-weight: 700; color: #20313b; }
    dd { margin: 0; color: #40505b; line-height: 1.35; }
    .status-note {
      flex-basis: 100%;
      color: #5d6b76;
      font-size: 12px;
    }
    svg {
      width: 100%;
      height: 340px;
      display: block;
      background: #fbfcfd;
      border: 1px solid #e0e6eb;
      border-radius: 6px;
    }
    table {
      border-collapse: collapse;
      width: 100%;
      font-size: 13px;
    }
    th, td {
      border-bottom: 1px solid #e5ebef;
      text-align: right;
      padding: 7px 8px;
    }
    th:first-child, td:first-child { text-align: left; }
    .status { color: #5d6b76; font-size: 13px; }
    .saa { color: #b23b2f; font-weight: 600; }
    .bg { color: #2f6f8f; font-weight: 600; }
    @media (max-width: 760px) {
      main { padding: 16px; }
      .grid { grid-template-columns: repeat(2, minmax(120px, 1fr)); }
      .docs { grid-template-columns: 1fr; }
      header h1 { font-size: 21px; }
    }
  </style>
</head>
<body>
  <header>
    <h1>Neutron 2 Payload Viewer</h1>
    <p>Ground-side CSV analysis for reconstructed neutron-count science products.</p>
  </header>
  <main>
    <div class="toolbar">
      <select id="dataset"></select>
      <button id="refresh">Refresh</button>
      <span id="status" class="status"></span>
      <span id="watcher" class="status-note">Watching for the newest downlinked payload CSV.</span>
    </div>
    <section class="grid" id="metrics"></section>
    <section class="panel">
      <h2>Counts vs Mission Time</h2>
      <svg id="chart" viewBox="0 0 1000 340" role="img" aria-label="Neutron counts over time"></svg>
    </section>
    <section class="panel">
      <h2>First Rows</h2>
      <table>
        <thead><tr><th>Flag</th><th>t_s</th><th>Counts</th></tr></thead>
        <tbody id="rows"></tbody>
      </table>
    </section>
    <section class="panel">
      <h2>Data Notes</h2>
      <div class="docs">
        <div>
          <h3>CSV Fields</h3>
          <dl>
            <dt>t_s</dt><dd>Mission-elapsed seconds at the start of the integration window.</dd>
            <dt>counts</dt><dd>Neutron events counted during that one-second window.</dd>
            <dt>flag</dt><dd>BG means background. SAA means simulated South Atlantic Anomaly pass.</dd>
          </dl>
        </div>
        <div>
          <h3>Summary Metrics</h3>
          <dl>
            <dt>Rows</dt><dd>Number of one-second samples in this capture.</dd>
            <dt>Total</dt><dd>All neutron counts summed across the capture.</dd>
            <dt>Mean</dt><dd>Average counts per one-second sample.</dd>
            <dt>SAA/BG Mean</dt><dd>Average count rate split by anomaly and background windows.</dd>
          </dl>
        </div>
        <div>
          <h3>How To Read It</h3>
          <p>Red chart bands mark SAA rows, where elevated counts are expected. Blue line movement outside red bands is background variation.</p>
          <p>This demo data is synthetic and order-of-magnitude realistic, not a calibrated detector product. The viewer automatically opens a newer CSV when downlink or capture produces one.</p>
          <p>Capture files are run artifacts in the OS temp folder. Do not rely on automatic cleanup; archive or delete old files as needed, or run StorageService.REMOVE_OLD_DATASETS(confirm=1).</p>
        </div>
      </div>
    </section>
  </main>
  <script>
    const datasetSelect = document.getElementById("dataset");
    const refreshButton = document.getElementById("refresh");
    const statusEl = document.getElementById("status");
    const metricsEl = document.getElementById("metrics");
    const chartEl = document.getElementById("chart");
    const rowsEl = document.getElementById("rows");
    const watcherEl = document.getElementById("watcher");
    let activeSource = "";
    let newestPath = "";

    function metric(label, value) {
      return `<div class="metric"><div class="label">${label}</div><div class="value">${value}</div></div>`;
    }

    function drawChart(rows) {
      chartEl.innerHTML = "";
      if (!rows.length) return;
      const width = 1000, height = 340;
      const left = 52, right = 18, top = 20, bottom = 38;
      const plotW = width - left - right;
      const plotH = height - top - bottom;
      const minT = rows[0].t_s;
      const maxT = rows[rows.length - 1].t_s;
      const maxCount = Math.max(...rows.map(r => r.counts), 1);
      const x = r => left + ((r.t_s - minT) / Math.max(maxT - minT, 1)) * plotW;
      const y = r => top + plotH - (r.counts / maxCount) * plotH;
      const points = rows.map(r => `${x(r).toFixed(1)},${y(r).toFixed(1)}`).join(" ");
      const saaRects = [];
      let start = null;
      for (let i = 0; i <= rows.length; i++) {
        const row = rows[i];
        if (row && row.flag === "SAA" && start === null) start = row;
        if ((!row || row.flag !== "SAA") && start !== null) {
          const end = rows[Math.max(i - 1, 0)];
          saaRects.push(`<rect x="${x(start).toFixed(1)}" y="${top}" width="${Math.max(x(end) - x(start), 2).toFixed(1)}" height="${plotH}" fill="#f8d7d0" opacity="0.55"></rect>`);
          start = null;
        }
      }
      chartEl.insertAdjacentHTML("beforeend", `
        ${saaRects.join("")}
        <line x1="${left}" y1="${top + plotH}" x2="${width - right}" y2="${top + plotH}" stroke="#9aa8b2"></line>
        <line x1="${left}" y1="${top}" x2="${left}" y2="${top + plotH}" stroke="#9aa8b2"></line>
        <text x="${left}" y="${height - 10}" font-size="13" fill="#5d6b76">t_s ${minT} to ${maxT}</text>
        <text x="8" y="${top + 12}" font-size="13" fill="#5d6b76">max ${maxCount}</text>
        <polyline points="${points}" fill="none" stroke="#145c8a" stroke-width="2.5"></polyline>
      `);
    }

    async function loadDatasets() {
      const res = await fetch("/api/datasets");
      const data = await res.json();
      const selectedPath = datasetSelect.selectedOptions[0]?.dataset.path || "";
      const latestPath = data.datasets[0]?.path || "";
      const openedNewLatest = latestPath && latestPath !== newestPath;
      datasetSelect.innerHTML = "";
      data.datasets.forEach((item, index) => {
        const option = document.createElement("option");
        option.value = item.id;
        option.dataset.path = item.path;
        option.textContent = item.label;
        datasetSelect.appendChild(option);
      });
      newestPath = latestPath;
      const targetPath = openedNewLatest ? latestPath : selectedPath;
      const target = Array.from(datasetSelect.options).find(option => option.dataset.path === targetPath);
      if (target) {
        target.selected = true;
      } else if (datasetSelect.options.length) {
        datasetSelect.options[0].selected = true;
      }
      statusEl.textContent = data.cleanup_hint || (data.datasets.length ? "" : "No CSV products found.");
      return openedNewLatest;
    }

    async function loadDataset({silent = false} = {}) {
      if (!datasetSelect.value) return;
      if (!silent) statusEl.textContent = "Loading...";
      const res = await fetch(`/api/dataset?id=${encodeURIComponent(datasetSelect.value)}`);
      const data = await res.json();
      if (data.error) {
        statusEl.textContent = data.error;
        return;
      }
      metricsEl.innerHTML = [
        metric("Rows", data.summary.rows),
        metric("Total Counts", data.summary.total_counts),
        metric("Mean Counts", data.summary.mean_counts.toFixed(2)),
        metric("Max Counts", data.summary.max_counts),
        metric("SAA Rows", data.summary.saa_rows),
        metric("BG Rows", data.summary.bg_rows),
        metric("SAA Mean", data.summary.saa_mean.toFixed(2)),
        metric("BG Mean", data.summary.bg_mean.toFixed(2)),
        metric("Start t_s", data.summary.start_t_s),
        metric("End t_s", data.summary.end_t_s)
      ].join("");
      drawChart(data.rows);
      rowsEl.innerHTML = data.rows.slice(0, 30).map(r => `
        <tr>
          <td class="${r.flag === "SAA" ? "saa" : "bg"}">${r.flag}</td>
          <td>${r.t_s}</td>
          <td>${r.counts}</td>
        </tr>
      `).join("");
      activeSource = data.source;
      statusEl.textContent = data.source;
    }

    async function refreshFromDisk({manual = false} = {}) {
      const openedNewLatest = await loadDatasets();
      const selectedPath = datasetSelect.selectedOptions[0]?.dataset.path || "";
      if (manual || openedNewLatest || selectedPath !== activeSource) {
        await loadDataset({silent: !manual});
      }
      watcherEl.textContent = openedNewLatest
        ? "Opened newest downlinked payload CSV."
        : "Watching for the newest downlinked payload CSV.";
    }

    refreshButton.addEventListener("click", async () => {
      await refreshFromDisk({manual: true});
    });
    datasetSelect.addEventListener("change", () => loadDataset());
    refreshFromDisk({manual: true});
    setInterval(() => refreshFromDisk(), 3000);
  </script>
</body>
</html>
"""


def read_rows(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"t_s", "counts", "flag"}
        if reader.fieldnames is None or not required.issubset(set(reader.fieldnames)):
            raise ValueError("CSV must include t_s, counts, and flag columns")
        for row in reader:
            rows.append({
                "t_s": int(float(row["t_s"])),
                "counts": int(float(row["counts"])),
                "flag": row["flag"].strip() or "BG",
            })
    if not rows:
        raise ValueError("CSV contains no data rows")
    return rows


def summarize(rows: list[dict[str, object]]) -> dict[str, object]:
    counts = [int(row["counts"]) for row in rows]
    saa_counts = [int(row["counts"]) for row in rows if row["flag"] == "SAA"]
    bg_counts = [int(row["counts"]) for row in rows if row["flag"] != "SAA"]
    return {
        "rows": len(rows),
        "start_t_s": rows[0]["t_s"],
        "end_t_s": rows[-1]["t_s"],
        "total_counts": sum(counts),
        "mean_counts": statistics.fmean(counts),
        "median_counts": statistics.median(counts),
        "min_counts": min(counts),
        "max_counts": max(counts),
        "saa_rows": len(saa_counts),
        "bg_rows": len(bg_counts),
        "saa_mean": statistics.fmean(saa_counts) if saa_counts else 0.0,
        "bg_mean": statistics.fmean(bg_counts) if bg_counts else 0.0,
    }


def list_datasets(capture_dir: Path, explicit_file: Path | None) -> list[Path]:
    files: list[Path] = []
    if explicit_file is not None:
        files.append(explicit_file.resolve())
    if capture_dir.exists():
        files.extend(sorted(capture_dir.glob("*.csv"), key=lambda path: path.stat().st_mtime, reverse=True))
    deduped: list[Path] = []
    seen: set[Path] = set()
    for path in files:
        resolved = path.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        deduped.append(resolved)
    return deduped


def cleanup_hint(paths: list[Path], capture_dir: Path) -> str:
    if len(paths) < CLEANUP_NOTICE_THRESHOLD:
        return ""
    return (
        f"{len(paths)} CSV products found. Cleanup old downlink files when done: "
        f"StorageService.REMOVE_OLD_DATASETS(confirm=1), or archive/delete files in {capture_dir}."
    )


def dataset_payload(path: Path) -> dict[str, object]:
    rows = read_rows(path)
    stride = max(1, math.ceil(len(rows) / MAX_INLINE_ROWS))
    sampled = rows[::stride]
    return {
        "source": str(path),
        "summary": summarize(rows),
        "rows": sampled,
        "sample_stride": stride,
    }


class ViewerServer(ThreadingHTTPServer):
    def __init__(self, server_address: tuple[str, int], handler_class: type[BaseHTTPRequestHandler], args: argparse.Namespace):
        super().__init__(server_address, handler_class)
        self.args = args


class Handler(BaseHTTPRequestHandler):
    server: ViewerServer

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        try:
            if parsed.path == "/":
                self.send_text(HTML, "text/html; charset=utf-8")
            elif parsed.path == "/api/datasets":
                self.send_json(self.handle_datasets())
            elif parsed.path == "/api/dataset":
                self.send_json(self.handle_dataset(parsed.query))
            else:
                self.send_error(404)
        except Exception as exc:
            self.send_json({"error": str(exc)}, status=500)

    def log_message(self, fmt: str, *args: object) -> None:
        if not self.server.args.quiet:
            super().log_message(fmt, *args)

    def handle_datasets(self) -> dict[str, object]:
        paths = list_datasets(self.server.args.capture_dir, self.server.args.file)
        return {
            "cleanup_hint": cleanup_hint(paths, self.server.args.capture_dir),
            "datasets": [
                {
                    "id": str(index),
                    "label": f"{path.name} ({path.parent})",
                    "path": str(path),
                    "mtime": path.stat().st_mtime,
                }
                for index, path in enumerate(paths)
            ]
        }

    def handle_dataset(self, query: str) -> dict[str, object]:
        query_args = parse_qs(query)
        index = int(query_args.get("id", ["0"])[0])
        paths = list_datasets(self.server.args.capture_dir, self.server.args.file)
        if index < 0 or index >= len(paths):
            return {"error": "dataset index out of range"}
        return dataset_payload(paths[index])

    def send_json(self, payload: dict[str, object], status: int = 200) -> None:
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_text(self, payload: str, content_type: str) -> None:
        data = payload.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-dir", type=Path, default=DEFAULT_CAPTURE_DIR)
    parser.add_argument("--file", type=Path, help="Open one reconstructed payload CSV explicitly")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8062)
    parser.add_argument("--no-open", action="store_true", help="Do not open a browser automatically")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--summary", type=Path, help="Print JSON summary for a CSV and exit")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.summary is not None:
        print(json.dumps(dataset_payload(args.summary)["summary"], indent=2, sort_keys=True))
        return 0

    server = ViewerServer((args.host, args.port), Handler, args)
    url = f"http://{args.host}:{args.port}/"
    print(f"Neutron 2 payload viewer: {url}")
    print(f"Capture directory: {args.capture_dir}")
    print("Cleanup note: archive/delete old downlink CSVs when done, or use StorageService.REMOVE_OLD_DATASETS(confirm=1).")
    if args.file:
        print(f"Explicit file: {args.file}")
    if not args.no_open:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
