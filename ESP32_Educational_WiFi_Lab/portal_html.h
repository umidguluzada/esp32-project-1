#pragma once

#include <pgmspace.h>

// The dashboard is stored in flash and served from the ESP32.
// It has no external images, fonts, or scripts: clients on this access point
// do not have internet access.

static const char INDEX_HTML[] PROGMEM = R"PORTALHTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Educational Wi-Fi Lab</title>
<style>
  * { box-sizing: border-box; }
  body { margin: 0; background: #101820; color: #e8eef4; font-family: "Segoe UI", Tahoma, sans-serif; }
  .wrap { max-width: 920px; margin: 0 auto; padding: 20px 16px 48px; }
  h1 { font-size: 28px; line-height: 1.2; margin: 0 0 8px; }
  h2 { font-size: 18px; margin: 28px 0 8px; }
  p { line-height: 1.45; }
  .muted { color: #a9b8c6; font-size: 14px; }
  .eyebrow { letter-spacing: 0.04em; text-transform: uppercase; font-size: 12px; color: #7dcea0; margin: 0 0 6px; }
  .live { font-weight: 600; margin-top: 0; }
  .grid { display: flex; flex-wrap: wrap; margin: 0 -6px; }
  .card { background: #1b2836; border-radius: 12px; padding: 12px 14px; margin: 6px; flex: 1 1 140px; }
  .card span { display: block; color: #93a4b5; font-size: 12px; }
  .card strong { display: block; margin-top: 4px; font-size: 20px; font-weight: 600; word-break: break-word; }
  button { background: #1f8a70; color: #ffffff; border: 0; border-radius: 10px; padding: 12px 16px; font-size: 16px; }
  button.ghost { background: transparent; color: #e8eef4; border: 1px solid #3d5568; }
  button:disabled { opacity: 0.55; }
  label { display: block; margin: 12px 0 6px; font-size: 14px; }
  input, textarea, select { width: 100%; padding: 10px 12px; border-radius: 8px; border: 1px solid #345066; background: #0e1620; color: #e8eef4; font-size: 16px; font-family: inherit; }
  .check { margin-top: 14px; }
  .check input { width: auto; margin-right: 8px; }
  .row { margin-top: 14px; }
  .row button { margin-right: 8px; margin-top: 8px; }
  .tableWrap { overflow-x: auto; margin-top: 12px; }
  table { width: 100%; border-collapse: collapse; background: #1b2836; }
  th, td { text-align: left; padding: 10px 12px; border-bottom: 1px solid #2a3c4e; font-size: 14px; vertical-align: top; word-break: break-word; }
  th { color: #93a4b5; font-weight: 600; }
  .preview { background: #1b2836; border-radius: 12px; padding: 4px 14px; margin-top: 8px; }
  .preview p { margin: 10px 0; }
  footer { margin-top: 28px; }
  #qr { background: #f4fff6; border-radius: 12px; margin-top: 8px; }
</style>
</head>
<body>
<div class="wrap">
  <header>
    <p class="eyebrow">ESP32 DevKitC · local lab</p>
    <h1>Educational Wi-Fi Lab</h1>
    <p class="muted">This page is served by the ESP32 on its own access point. DNS queries from clients on this network are answered with the board's address. That is how a captive portal opens a local page. Encrypted HTTPS sites still will not load.</p>
    <p id="connState" class="live">Connecting to the board...</p>
  </header>

  <section>
    <h2>Join with a QR code</h2>
    <p class="muted">Point a phone camera at this code to join the lab network. The code is drawn on the page from the access-point name and password stored on the board.</p>
    <canvas id="qr" width="180" height="180"></canvas>
    <p id="qrCaption" class="muted"></p>
  </section>

  <section>
    <h2>Device status</h2>
    <div class="grid">
      <div class="card"><span>Free heap</span><strong id="heap">--</strong></div>
      <div class="card"><span>Lowest heap</span><strong id="minHeap">--</strong></div>
      <div class="card"><span>Uptime</span><strong id="uptime">--</strong></div>
      <div class="card"><span>SSID</span><strong id="ssid">--</strong></div>
      <div class="card"><span>AP address</span><strong id="ip">--</strong></div>
      <div class="card"><span>AP MAC</span><strong id="mac">--</strong></div>
      <div class="card"><span>AP channel</span><strong id="channel">--</strong></div>
      <div class="card"><span>Onboard LED</span><strong id="ledState">--</strong></div>
    </div>
  </section>

  <section>
    <h2>Nearby networks</h2>
    <p class="muted">The board listens for beacon frames and lists what it hears. It does not join those networks. Scanning uses the same radio as the access point, so connected clients can pause for a few seconds. Names that phones look up are counted above and printed on the Serial Monitor.</p>
    <button id="scanBtn" type="button" onclick="startScan();">Scan networks</button>
    <p id="scanMeta" class="muted"></p>
    <div class="tableWrap">
      <table>
        <thead>
          <tr>
            <th>SSID</th>
            <th>RSSI</th>
            <th>Signal</th>
            <th>Channel</th>
            <th>Encryption</th>
          </tr>
        </thead>
        <tbody id="scanBody">
          <tr><td colspan="5">Waiting for the board...</td></tr>
        </tbody>
      </table>
    </div>
  </section>

  <section>
    <h2>Local input demo</h2>
    <p class="muted">Submit the form to see the ESP32 store text in its own RAM and send that text back. Nothing is forwarded to the internet. Rebooting the board clears it.</p>
    <form action="/api/config" method="post" onsubmit="return onSave(event);" autocomplete="off">
      <label for="deviceName">Device label</label>
      <input id="deviceName" name="deviceName" maxlength="32" autocomplete="off">
      <label for="labNote">Lab note</label>
      <textarea id="labNote" name="labNote" maxlength="96" rows="3" autocomplete="off"></textarea>
      <label class="check" for="led">
        <input id="led" name="led" type="checkbox" value="1" onchange="onLedChanged();">
        Turn on the onboard LED (GPIO 2 on most DevKitC boards)
      </label>
      <p class="muted">Changing the box drives GPIO 2 immediately. Saving the form does the same with the box's current state.</p>
      <h2>Lab profile</h2>
      <p class="muted">Profile name and preference are sent with the rest of this form. The board prints them on the Serial Monitor with the time and client address.</p>
      <label for="profileName">Profile name</label>
      <input id="profileName" name="profileName" maxlength="32" autocomplete="off">
      <label for="preference">Preference</label>
      <select id="preference" name="preference">
        <option value="status">Status cards</option>
        <option value="scan">Network scan</option>
        <option value="both" selected>Status and scan</option>
      </select>
      <div class="row">
        <button type="submit">Save on device</button>
        <button type="button" class="ghost" onclick="onClear();">Clear</button>
      </div>
    </form>
    <p id="saveMsg" class="muted" role="status"></p>
    <div class="preview">
      <p>Stored label: <strong id="devicePreview">--</strong></p>
      <p>Stored note: <strong id="notePreview">--</strong></p>
      <p>LED: <strong id="ledPreview">--</strong></p>
      <p>Profile name: <strong id="profilePreview">--</strong></p>
      <p>Preference: <strong id="preferencePreview">--</strong></p>
    </div>
  </section>

  <footer>
    <p class="muted">If the phone does not show a sign-in popup, open <span id="portalLink">http://192.168.4.1/</span> in the browser. Rows in the scan table are networks the board can hear. It does not connect to them.</p>
  </footer>
</div>
<script>
"use strict";

var statusBusy = false;

function setText(id, value) {
  var el = document.getElementById(id);
  if (el) el.textContent = value;
}

function formatBytes(n) {
  n = Number(n);
  if (isNaN(n)) return "--";
  if (n < 1024) return Math.round(n) + " B";
  return (n / 1024).toFixed(1) + " KB";
}

function pad2(n) {
  return (n < 10 ? "0" : "") + n;
}

function formatUptime(total) {
  total = Number(total);
  if (isNaN(total) || total < 0) return "--";
  total = Math.floor(total);
  var s = total % 60;
  var m = Math.floor(total / 60) % 60;
  var h = Math.floor(total / 3600) % 24;
  var d = Math.floor(total / 86400);
  var clock = pad2(h) + ":" + pad2(m) + ":" + pad2(s);
  if (d > 0) return d + "d " + clock;
  return clock;
}

function signalLabel(rssi) {
  if (rssi >= -50) return "Excellent";
  if (rssi >= -60) return "Good";
  if (rssi >= -70) return "Fair";
  return "Weak";
}

function requestJson(method, url, body, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open(method, url, true);
  xhr.onreadystatechange = function () {
    if (xhr.readyState !== 4) return;
    if (xhr.status < 200 || xhr.status >= 300) {
      cb(new Error("bad status"));
      return;
    }
    var parsed;
    try {
      parsed = JSON.parse(xhr.responseText);
    } catch (e) {
      cb(e);
      return;
    }
    cb(null, parsed);
  };
  if (method === "POST") {
    xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
  }
  xhr.send(body || null);
}

function getJson(url, cb) {
  requestJson("GET", url, null, cb);
}

function postForm(url, body, cb) {
  requestJson("POST", url, body, cb);
}

var lastQr = "";

function makeQr(text) {
  var ECC = [
    null,
    { data: 19, ecc: 7, blocks: 1 },
    { data: 34, ecc: 10, blocks: 1 },
    { data: 55, ecc: 15, blocks: 1 },
    { data: 80, ecc: 20, blocks: 1 },
    { data: 108, ecc: 26, blocks: 1 },
    { data: 136, ecc: 18, blocks: 2 }
  ];
  var ALIGN = [null, [], [6, 18], [6, 22], [6, 26], [6, 30], [6, 34]];
  var FORMAT = [0x77c4, 0x72f3, 0x7daa, 0x789d, 0x662f, 0x6318, 0x6c41, 0x6976];
  var EXP = new Array(512);
  var LOG = new Array(256);
  (function () {
    var x = 1;
    for (var i = 0; i < 255; i++) {
      EXP[i] = x;
      LOG[x] = i;
      x <<= 1;
      if (x & 0x100) x ^= 0x11d;
    }
    for (var j = 255; j < 512; j++) EXP[j] = EXP[j - 255];
  })();
  function gfMul(a, b) {
    if (a === 0 || b === 0) return 0;
    return EXP[LOG[a] + LOG[b]];
  }
  function polyMul(p, q) {
    var r = [];
    for (var i = 0; i < p.length + q.length - 1; i++) r.push(0);
    for (var j = 0; j < p.length; j++) {
      for (var k = 0; k < q.length; k++) r[j + k] ^= gfMul(p[j], q[k]);
    }
    return r;
  }
  function rsPoly(nsym) {
    var g = [1];
    for (var i = 0; i < nsym; i++) g = polyMul(g, [1, EXP[i]]);
    return g;
  }
  function rsEncode(data, nsym) {
    var gen = rsPoly(nsym);
    var res = data.slice();
    for (var i = 0; i < nsym; i++) res.push(0);
    for (var n = 0; n < data.length; n++) {
      var coef = res[n];
      if (coef === 0) continue;
      for (var m = 0; m < gen.length; m++) res[n + m] ^= gfMul(gen[m], coef);
    }
    return res.slice(data.length);
  }
  var bytes = [];
  for (var c = 0; c < text.length; c++) bytes.push(text.charCodeAt(c) & 255);
  var version = 0;
  for (var v = 1; v <= 6; v++) {
    var cap = ECC[v].data * 8;
    var need = 4 + 8 + bytes.length * 8;
    if (need <= cap) { version = v; break; }
  }
  if (!version) return null;
  var spec = ECC[version];
  var bits = [];
  function put(val, len) {
    for (var i = len - 1; i >= 0; i--) bits.push((val >>> i) & 1);
  }
  put(0x4, 4);
  put(bytes.length, 8);
  for (var b = 0; b < bytes.length; b++) put(bytes[b], 8);
  var capBits = spec.data * 8;
  var term = Math.min(4, capBits - bits.length);
  put(0, term);
  while (bits.length % 8) bits.push(0);
  var data = [];
  for (var p = 0; p < bits.length; p += 8) {
    var byte = 0;
    for (var q = 0; q < 8; q++) byte = (byte << 1) | bits[p + q];
    data.push(byte);
  }
  var pad = 0xec;
  while (data.length < spec.data) {
    data.push(pad);
    pad = pad === 0xec ? 0x11 : 0xec;
  }
  var blockSize = Math.floor(spec.data / spec.blocks);
  var blocks = [];
  var eccs = [];
  for (var bi = 0; bi < spec.blocks; bi++) {
    var chunk = data.slice(bi * blockSize, (bi + 1) * blockSize);
    blocks.push(chunk);
    eccs.push(rsEncode(chunk, spec.ecc));
  }
  var stream = [];
  for (var col = 0; col < blockSize; col++) {
    for (var bi2 = 0; bi2 < spec.blocks; bi2++) stream.push(blocks[bi2][col]);
  }
  for (var ei = 0; ei < spec.ecc; ei++) {
    for (var bi3 = 0; bi3 < spec.blocks; bi3++) stream.push(eccs[bi3][ei]);
  }
  var size = 17 + 4 * version;
  var modules = [];
  var fn = [];
  for (var y = 0; y < size; y++) {
    modules.push(new Array(size));
    fn.push(new Array(size));
    for (var x = 0; x < size; x++) { modules[y][x] = false; fn[y][x] = false; }
  }
  function setFn(x, y, dark) {
    modules[y][x] = dark;
    fn[y][x] = true;
  }
  function finder(x0, y0) {
    for (var dy = -1; dy <= 7; dy++) {
      for (var dx = -1; dx <= 7; dx++) {
        var x = x0 + dx, y = y0 + dy;
        if (x < 0 || y < 0 || x >= size || y >= size) continue;
        var edge = dx === -1 || dy === -1 || dx === 7 || dy === 7;
        var box = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 && (dx === 0 || dy === 0 || dx === 6 || dy === 6);
        var core = dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4;
        setFn(x, y, !edge && (box || core));
      }
    }
  }
  finder(0, 0);
  finder(size - 7, 0);
  finder(0, size - 7);
  for (var i = 8; i < size - 8; i++) {
    setFn(6, i, i % 2 === 0);
    setFn(i, 6, i % 2 === 0);
  }
  var pos = ALIGN[version];
  for (var a = 0; a < pos.length; a++) {
    for (var b2 = 0; b2 < pos.length; b2++) {
      var cx = pos[a], cy = pos[b2];
      if (fn[cy][cx]) continue;
      for (var dy2 = -2; dy2 <= 2; dy2++) {
        for (var dx2 = -2; dx2 <= 2; dx2++) {
          var ax = cx + dx2, ay = cy + dy2;
          var on = Math.max(Math.abs(dx2), Math.abs(dy2)) !== 1;
          setFn(ax, ay, on);
        }
      }
    }
  }
  setFn(8, 4 * version + 9, true);
  for (var rf = 0; rf <= 5; rf++) setFn(8, rf, false);
  setFn(8, 7, false);
  setFn(8, 8, false);
  setFn(7, 8, false);
  for (var rf2 = 9; rf2 < 15; rf2++) setFn(14 - rf2, 8, false);
  for (var rf3 = 0; rf3 < 8; rf3++) setFn(size - 1 - rf3, 8, false);
  for (var rf4 = 8; rf4 < 15; rf4++) setFn(8, size - 15 + rf4, false);
  setFn(8, 4 * version + 9, true);
  var bitIndex = 0;
  var bitLen = stream.length * 8;
  for (var right = size - 1; right >= 1; right -= 2) {
    if (right === 6) right = 5;
    for (var vert = 0; vert < size; vert++) {
      for (var j = 0; j < 2; j++) {
        var upward = ((right + 1) & 2) === 0;
        var xx = right - j;
        var yy = upward ? size - 1 - vert : vert;
        if (fn[yy][xx]) continue;
        var dark = false;
        if (bitIndex < bitLen) {
          dark = ((stream[bitIndex >>> 3] >>> (7 - (bitIndex & 7))) & 1) === 1;
          bitIndex++;
        }
        modules[yy][xx] = dark;
      }
    }
  }
  function maskOf(mask, x, y) {
    if (mask === 0) return (x + y) % 2 === 0;
    if (mask === 1) return y % 2 === 0;
    if (mask === 2) return x % 3 === 0;
    if (mask === 3) return (x + y) % 3 === 0;
    if (mask === 4) return (Math.floor(y / 2) + Math.floor(x / 3)) % 2 === 0;
    if (mask === 5) return ((x * y) % 2) + ((x * y) % 3) === 0;
    if (mask === 6) return (((x * y) % 2) + ((x * y) % 3)) % 2 === 0;
    return (((x + y) % 2) + ((x * y) % 3)) % 2 === 0;
  }
  function applyMask(mask) {
    for (var y2 = 0; y2 < size; y2++) {
      for (var x2 = 0; x2 < size; x2++) {
        if (!fn[y2][x2] && maskOf(mask, x2, y2)) modules[y2][x2] = !modules[y2][x2];
      }
    }
  }
  function bitOn(bits15, i2) {
    return ((bits15 >>> i2) & 1) === 1;
  }
  function writeFormat(mask) {
    var bits15 = FORMAT[mask];
    for (var i2 = 0; i2 <= 5; i2++) setFn(8, i2, bitOn(bits15, i2));
    setFn(8, 7, bitOn(bits15, 6));
    setFn(8, 8, bitOn(bits15, 7));
    setFn(7, 8, bitOn(bits15, 8));
    for (var i3 = 9; i3 < 15; i3++) setFn(14 - i3, 8, bitOn(bits15, i3));
    for (var i4 = 0; i4 < 8; i4++) setFn(size - 1 - i4, 8, bitOn(bits15, i4));
    for (var i5 = 8; i5 < 15; i5++) setFn(8, size - 15 + i5, bitOn(bits15, i5));
    setFn(8, 4 * version + 9, true);
  }
  applyMask(0);
  writeFormat(0);
  return modules;
}

function drawQr(text, ssid) {
  if (!text || text === lastQr) return;
  lastQr = text;
  var modules = makeQr(text);
  var canvas = document.getElementById("qr");
  var caption = document.getElementById("qrCaption");
  if (!modules || !canvas) {
    if (caption) caption.textContent = "This network name is too long to draw. Join " + (ssid || "the lab network") + " by name.";
    return;
  }
  var n = modules.length;
  var scale = Math.floor(220 / (n + 8));
  if (scale < 3) scale = 3;
  var quiet = 4;
  var size = (n + quiet * 2) * scale;
  canvas.width = size;
  canvas.height = size;
  var ctx = canvas.getContext("2d");
  ctx.fillStyle = "#f4fff6";
  ctx.fillRect(0, 0, size, size);
  ctx.fillStyle = "#05210f";
  for (var y = 0; y < n; y++) {
    for (var x = 0; x < n; x++) {
      if (modules[y][x]) ctx.fillRect((x + quiet) * scale, (y + quiet) * scale, scale, scale);
    }
  }
  if (caption) caption.textContent = "Network: " + (ssid || "lab access point");
}

function refreshStatus() {
  if (statusBusy) return;
  statusBusy = true;
  getJson("/api/status", function (err, data) {
    statusBusy = false;
    if (err || !data) {
      setText("connState", "Dashboard lost contact with the board.");
      return;
    }
    setText("connState", "Live");
    setText("heap", formatBytes(data.heap));
    setText("minHeap", formatBytes(data.minHeap));
    setText("uptime", formatUptime(data.uptime));
    setText("ssid", data.ssid || "");
    setText("ip", data.ip || "");
    setText("mac", data.mac || "");
    setText("channel", data.apChannel == null ? "--" : String(data.apChannel));
    setText("ledState", data.led ? "On" : "Off");
    setText("devicePreview", data.deviceName || "");
    setText("notePreview", data.labNote ? data.labNote : "(empty)");
    setText("ledPreview", data.led ? "On" : "Off");
    if (data.ip) setText("portalLink", "http://" + data.ip + "/");
    if (data.qr) drawQr(data.qr, data.ssid || "");
  });
}

function cell(text) {
  var td = document.createElement("td");
  td.textContent = text == null ? "" : String(text);
  return td;
}

function addMessageRow(body, text) {
  var tr = document.createElement("tr");
  var td = document.createElement("td");
  td.setAttribute("colspan", "5");
  td.textContent = text;
  tr.appendChild(td);
  body.appendChild(tr);
}

function renderScan(data) {
  var body = document.getElementById("scanBody");
  var meta = document.getElementById("scanMeta");
  while (body.firstChild) body.removeChild(body.firstChild);

  if (!data || !data.state) {
    meta.textContent = "";
    addMessageRow(body, "No scan data.");
    return;
  }

  if (data.state === "running") {
    meta.textContent = "Scan in progress";
    addMessageRow(body, "Scanning nearby access points...");
    return;
  }

  if (data.state === "failed") {
    meta.textContent = "Scan failed";
    addMessageRow(body, data.error || "Scan failed. Try again.");
    return;
  }

  if (data.state === "idle") {
    meta.textContent = "Not scanned yet";
    addMessageRow(body, "Press Scan networks to listen for nearby access points.");
    return;
  }

  var nets = data.networks || [];
  if (data.age > 0) {
    meta.textContent = nets.length + " network(s), " + data.age + " s ago";
  } else {
    meta.textContent = nets.length + " network(s)";
  }

  if (nets.length === 0) {
    addMessageRow(body, "Scan finished. No access points were heard.");
    return;
  }

  for (var i = 0; i < nets.length; i++) {
    var net = nets[i];
    var tr = document.createElement("tr");
    tr.appendChild(cell(net.ssid));
    tr.appendChild(cell(String(net.rssi) + " dBm"));
    tr.appendChild(cell(signalLabel(Number(net.rssi))));
    tr.appendChild(cell(String(net.channel)));
    tr.appendChild(cell(net.enc));
    body.appendChild(tr);
  }
}

function setScanBusy(busy) {
  var btn = document.getElementById("scanBtn");
  btn.disabled = busy;
  btn.textContent = busy ? "Scanning..." : "Scan networks";
}

function pollScan() {
  getJson("/api/scan", function (err, data) {
    if (err || !data) {
      setScanBusy(false);
      renderScan({ state: "failed", error: "Could not read scan results." });
      return;
    }
    renderScan(data);
    if (data.state === "running") {
      setTimeout(pollScan, 800);
    } else {
      setScanBusy(false);
    }
  });
}

function startScan() {
  setScanBusy(true);
  postForm("/api/scan", "", function (err, data) {
    if (err || !data) {
      setScanBusy(false);
      renderScan({ state: "failed", error: "Scan request failed." });
      return;
    }
    renderScan(data);
    if (data.state === "running") {
      setTimeout(pollScan, 800);
    } else {
      setScanBusy(false);
    }
  });
}

function onLedChanged() {
  var on = document.getElementById("led").checked;
  postForm("/api/config", "led=" + (on ? "1" : "0"), function (err, data) {
    if (err || !data) {
      setText("saveMsg", "The board did not change the LED.");
      return;
    }
    document.getElementById("led").checked = !!data.led;
    setText("ledPreview", data.led ? "On" : "Off");
    setText("ledState", data.led ? "On" : "Off");
    setText("saveMsg", data.led ? "GPIO 2 is on." : "GPIO 2 is off.");
  });
}

function fillForm(data) {
  document.getElementById("deviceName").value = data.deviceName || "";
  document.getElementById("labNote").value = data.labNote || "";
  document.getElementById("led").checked = !!data.led;
  document.getElementById("profileName").value = data.profileName || "";
  document.getElementById("preference").value = data.preference || "both";
  showProfilePreview();
}

function preferenceLabel() {
  var el = document.getElementById("preference");
  if (!el || el.selectedIndex < 0) return "";
  return el.options[el.selectedIndex].text;
}

function showProfilePreview() {
  var name = document.getElementById("profileName").value;
  setText("profilePreview", name ? name : "(empty)");
  setText("preferencePreview", preferenceLabel());
}

function onSave(ev) {
  if (ev && ev.preventDefault) ev.preventDefault();
  var body = "deviceName=" + encodeURIComponent(document.getElementById("deviceName").value) +
    "&labNote=" + encodeURIComponent(document.getElementById("labNote").value) +
    "&led=" + (document.getElementById("led").checked ? "1" : "0") +
    "&profileName=" + encodeURIComponent(document.getElementById("profileName").value) +
    "&preference=" + encodeURIComponent(document.getElementById("preference").value);
  setText("saveMsg", "Saving on the board...");
  postForm("/api/config", body, function (err, data) {
    if (err || !data) {
      setText("saveMsg", "The board did not accept the form. Try again.");
      return;
    }
    fillForm(data);
    setText("saveMsg", "Saved in ESP32 RAM. GPIO 2 matches the LED checkbox.");
    refreshStatus();
  });
  return false;
}

function onClear() {
  setText("saveMsg", "Clearing stored values...");
  postForm("/api/config", "clear=1", function (err, data) {
    if (err || !data) {
      setText("saveMsg", "Could not clear the stored values.");
      return;
    }
    fillForm(data);
    setText("saveMsg", "Cleared. The board is back to its default label.");
    refreshStatus();
  });
}

refreshStatus();
setInterval(refreshStatus, 2000);
getJson("/api/config", function (err, data) {
  if (!err && data) fillForm(data);
});
getJson("/api/scan", function (err, data) {
  if (!err && data) renderScan(data);
});
</script>
</body>
</html>
)PORTALHTML";

// Served only after the admin password check in firmware.cpp.
// The public dashboard does not link here.
static const char ADMIN_HTML[] PROGMEM = R"ADMINHTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Lab admin</title>
<style>
  * { box-sizing: border-box; }
  body { margin: 0; background: #04140a; color: #39ff14; font-family: Consolas, "Cascadia Mono", monospace; }
  .wrap { max-width: 920px; margin: 0 auto; padding: 20px 16px 48px; }
  h1 { font-size: 22px; margin: 0 0 8px; }
  p { color: #8fbc8f; line-height: 1.45; }
  .console {
    background: #020a06;
    color: #39ff14;
    font-size: 13px;
    line-height: 1.45;
    min-height: 320px;
    max-height: 70vh;
    overflow: auto;
    margin: 12px 0;
    padding: 12px 14px;
    border-radius: 12px;
    border: 1px solid #14532d;
    white-space: pre-wrap;
    text-shadow: 0 0 6px rgba(57, 255, 20, 0.45);
  }
  h2 { font-size: 18px; margin: 28px 0 8px; }
  table { width: 100%; border-collapse: collapse; margin-top: 8px; }
  th, td { text-align: left; padding: 8px 10px; border-bottom: 1px solid #14532d; vertical-align: top; word-break: break-word; white-space: pre-wrap; }
  th { color: #8fbc8f; }
  button {
    background: transparent;
    color: #39ff14;
    border: 1px solid #14532d;
    border-radius: 10px;
    padding: 10px 14px;
    font: inherit;
  }
  a {
    display: inline-block;
    color: #39ff14;
    border: 1px solid #14532d;
    border-radius: 10px;
    padding: 10px 14px;
    text-decoration: none;
    margin-right: 8px;
  }
</style>
</head>
<body>
<div class="wrap">
  <h1>Lab admin</h1>
  <p>Clients, page requests, and DNS names seen by this access point. This page is not linked from the public portal.</p>
  <pre id="console" class="console">&gt; waiting for the board...</pre>
  <p>
    <a href="/api/log?dl=txt">Export text</a>
    <a href="/api/log?dl=json">Export JSON</a>
  </p>
  <h2>Connected clients</h2>
  <p>Each row is one device that joined this access point and then talked to the board. Headers come from that device's own requests. DNS names are the lookups that device sent here.</p>
  <table>
    <thead>
      <tr><th>IP and MAC</th><th>Captured request</th></tr>
    </thead>
    <tbody id="clientBody">
      <tr><td colspan="2">Waiting for the board...</td></tr>
    </tbody>
  </table>
</div>
<script>
"use strict";
function renderConsole(data) {
  var el = document.getElementById("console");
  var lines = data && data.lines ? data.lines : [];
  if (!lines.length) {
    el.textContent = "> console ready";
    return;
  }
  var text = "";
  for (var i = 0; i < lines.length; i++) text += "> " + lines[i].text + "\n";
  el.textContent = text;
  el.scrollTop = el.scrollHeight;
}
function refreshLog() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/api/log", true);
  xhr.onreadystatechange = function () {
    if (xhr.readyState !== 4 || xhr.status !== 200) return;
    try { renderConsole(JSON.parse(xhr.responseText)); } catch (e) {}
  };
  xhr.send(null);
}
function getJson(url, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", url, true);
  xhr.onreadystatechange = function () {
    if (xhr.readyState !== 4) return;
    if (xhr.status !== 200) { cb(new Error("bad status")); return; }
    try { cb(null, JSON.parse(xhr.responseText)); } catch (e) { cb(e); }
  };
  xhr.send(null);
}
function cell(text) {
  var td = document.createElement("td");
  td.textContent = text == null ? "" : String(text);
  return td;
}
function renderClients(data) {
  var body = document.getElementById("clientBody");
  while (body.firstChild) body.removeChild(body.firstChild);
  var list = data && data.clients ? data.clients : [];
  if (!list.length) {
    var tr = document.createElement("tr");
    var td = document.createElement("td");
    td.setAttribute("colspan", "2");
    td.textContent = "No stations are joined right now.";
    tr.appendChild(td);
    body.appendChild(tr);
    return;
  }
  for (var i = 0; i < list.length; i++) {
    var person = list[i];
    var row = document.createElement("tr");
    var who = document.createElement("td");
    who.textContent = (person.ip || "") + "\n" + (person.mac || "");
    var detail = document.createElement("td");
    var dns = person.dns && person.dns.length ? person.dns.join(", ") : "(none yet)";
    detail.textContent =
      "Last page: " + (person.path || "(none yet)") + "\n" +
      "Host: " + (person.host || "(none yet)") + "\n" +
      "User-Agent: " + (person.userAgent || "(none yet)") + "\n" +
      "Accept-Language: " + (person.acceptLanguage || "(none yet)") + "\n" +
      "DNS: " + dns;
    row.appendChild(who);
    row.appendChild(detail);
    body.appendChild(row);
  }
}
function refreshClients() {
  getJson("/api/clients", function (err, data) {
    if (err || !data) { renderClients({ clients: [] }); return; }
    renderClients(data);
  });
}
refreshLog();
refreshClients();
setInterval(refreshLog, 2000);
setInterval(refreshClients, 2000);
</script>
</body>
</html>
)ADMINHTML";

