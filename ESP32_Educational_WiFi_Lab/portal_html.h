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
    <h2>Device status</h2>
    <div class="grid">
      <div class="card"><span>Free heap</span><strong id="heap">--</strong></div>
      <div class="card"><span>Lowest heap</span><strong id="minHeap">--</strong></div>
      <div class="card"><span>Uptime</span><strong id="uptime">--</strong></div>
      <div class="card"><span>Clients</span><strong id="clients">--</strong></div>
      <div class="card"><span>SSID</span><strong id="ssid">--</strong></div>
      <div class="card"><span>AP address</span><strong id="ip">--</strong></div>
      <div class="card"><span>AP MAC</span><strong id="mac">--</strong></div>
      <div class="card"><span>AP channel</span><strong id="channel">--</strong></div>
      <div class="card"><span>Onboard LED</span><strong id="ledState">--</strong></div>
      <div class="card"><span>DNS queries</span><strong id="dnsCount">--</strong></div>
      <div class="card"><span>Latest DNS name</span><strong id="dnsLast">--</strong></div>
    </div>
  </section>

  <section>
    <h2>Connected clients</h2>
    <p class="muted">Stations that have joined this access point. The table refreshes while this page is open. Devices that never join are not listed.</p>
    <div class="tableWrap">
      <table>
        <thead>
          <tr>
            <th>IP address</th>
            <th>MAC address</th>
          </tr>
        </thead>
        <tbody id="clientBody">
          <tr><td colspan="2">Waiting for the board...</td></tr>
        </tbody>
      </table>
    </div>
  </section>

  <section>
    <h2>Your browser leak info</h2>
    <p class="muted">These are the headers your own browser attached to the request for this section. The ESP32 sends them back so you can see the metadata a site on this network can read.</p>
    <button type="button" onclick="loadHeaders();">Refresh headers</button>
    <div class="tableWrap">
      <table>
        <thead>
          <tr>
            <th>Header</th>
            <th>Value</th>
          </tr>
        </thead>
        <tbody id="headerBody">
          <tr><td colspan="2">Waiting for the board...</td></tr>
        </tbody>
      </table>
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

function refreshStatus() {
  refreshClients();
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
    setText("clients", String(data.clients));
    setText("ssid", data.ssid || "");
    setText("ip", data.ip || "");
    setText("mac", data.mac || "");
    setText("channel", data.apChannel == null ? "--" : String(data.apChannel));
    setText("ledState", data.led ? "On" : "Off");
    setText("dnsCount", data.dnsQueries == null ? "--" : String(data.dnsQueries));
    setText("dnsLast", data.dnsLast ? data.dnsLast : "(none yet)");
    setText("devicePreview", data.deviceName || "");
    setText("notePreview", data.labNote ? data.labNote : "(empty)");
    setText("ledPreview", data.led ? "On" : "Off");
    if (data.ip) setText("portalLink", "http://" + data.ip + "/");
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
    var row = document.createElement("tr");
    row.appendChild(cell(list[i].ip));
    row.appendChild(cell(list[i].mac));
    body.appendChild(row);
  }
}

function refreshClients() {
  getJson("/api/clients", function (err, data) {
    if (err || !data) {
      renderClients({ clients: [] });
      return;
    }
    renderClients(data);
  });
}

function renderHeaders(data) {
  var body = document.getElementById("headerBody");
  while (body.firstChild) body.removeChild(body.firstChild);
  var list = data && data.headers ? data.headers : [];
  if (!list.length) {
    var tr = document.createElement("tr");
    var td = document.createElement("td");
    td.setAttribute("colspan", "2");
    td.textContent = "The board did not return headers.";
    tr.appendChild(td);
    body.appendChild(tr);
    return;
  }
  for (var i = 0; i < list.length; i++) {
    var row = document.createElement("tr");
    row.appendChild(cell(list[i].name));
    row.appendChild(cell(list[i].value ? list[i].value : "(not sent)"));
    body.appendChild(row);
  }
}

function loadHeaders() {
  getJson("/api/headers", function (err, data) {
    if (err || !data) {
      renderHeaders(null);
      return;
    }
    renderHeaders(data);
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
loadHeaders();
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
