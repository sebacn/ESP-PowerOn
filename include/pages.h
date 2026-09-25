#pragma once

#include <Arduino.h>

static const char kStyleCss[] PROGMEM = R"CSS(:root { color-scheme: dark; }
body { margin: 0; font-family: sans-serif; background: #121417; color: #e8eaed; }
main { max-width: 36rem; margin: 0 auto; padding: 1.25rem; }
nav { display: flex; gap: 0.5rem; margin-bottom: 1.25rem; }
nav a { color: #c5d0dc; text-decoration: none; padding: 0.45rem 0.75rem; border-radius: 999px; background: #1c2128; }
nav a.active { background: #2f6fed; color: white; }
h1 { font-size: 1.5rem; margin: 0 0 1rem; }
h2 { font-size: 1.05rem; margin: 1.3rem 0 0.5rem; }
dl { display: grid; grid-template-columns: 9.5rem 1fr; gap: 0.45rem 0.75rem; margin: 0 0 1.25rem; }
dt { color: #9aa3ad; }
dd { margin: 0; overflow-wrap: anywhere; }
.status { display: flex; align-items: center; gap: 1rem; background: #1c2128; border-radius: 12px; padding: 1rem; }
.led { width: 4.5rem; height: 4.5rem; border-radius: 50%; background: #6b7280; box-shadow: inset 0 0 10px rgba(0,0,0,.45); }
.led.on { background: #22c55e; box-shadow: 0 0 18px #22c55e; }
.kicker { color: #9aa3ad; font-size: 0.85rem; }
.led-label { font-size: 1.8rem; font-weight: 700; }
button { font: inherit; margin-top: 1rem; width: 100%; padding: 0.9rem; border: 0; border-radius: 8px; background: #2f6fed; color: white; }
button.secondary { width: auto; margin: 0.35rem 0 0.8rem; padding: 0.55rem 0.8rem; background: #3a424d; }
button:disabled { opacity: 0.55; }
label { display: block; margin: 0.75rem 0; }
input[type="number"], input[type="text"], input[type="password"] { width: 100%; box-sizing: border-box; margin-top: 0.3rem; padding: 0.55rem; border-radius: 8px; border: 1px solid #30363d; background: #0e1116; color: inherit; }
.hint, #note { color: #9aa3ad; }
table { width: 100%; border-collapse: collapse; }
th, td { text-align: left; padding: 0.45rem 0.25rem; border-bottom: 1px solid #2a313a; vertical-align: top; }
th { color: #9aa3ad; font-weight: 600; }
.check { display: flex; gap: 0.55rem; align-items: center; }
)CSS";

static const char kMainPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP-PowerOn</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<main>
<nav>
  <a class="active" href="/">Main</a>
  <a href="/settings">Settings</a>
  <a href="/logs">Logs</a>
</nav>
<h1>ESP-PowerOn</h1>
<dl>
  <dt>Date and time</dt><dd id="clock">—</dd>
  <dt>Uptime</dt><dd id="uptime">—</dd>
  <dt>IP address</dt><dd id="ip">—</dd>
  <dt>Wi-Fi</dt><dd id="wifi">—</dd>
</dl>
<section class="status">
  <div id="led" class="led"></div>
  <div>
    <div class="kicker">Power LED</div>
    <div id="led-label" class="led-label">—</div>
  </div>
</section>
<button id="enable" type="button">Enable PC</button>
<p id="note" class="hint"></p>
</main>
<script>
function formatUptime(total) {
  total = Math.max(0, total | 0);
  var days = Math.floor(total / 86400);
  var hours = Math.floor((total % 86400) / 3600);
  var minutes = Math.floor((total % 3600) / 60);
  var seconds = total % 60;
  function two(value) { return (value < 10 ? "0" : "") + value; }
  var clock = two(hours) + ":" + two(minutes) + ":" + two(seconds);
  return days ? days + "d " + clock : clock;
}
async function refresh() {
  var response = await fetch("/api/status");
  if (!response.ok) return;
  var status = await response.json();
  document.getElementById("clock").textContent = status.timeValid ? status.time : "Not set";
  document.getElementById("uptime").textContent = formatUptime(status.uptimeSec);
  document.getElementById("ip").textContent = status.ip || "—";
  document.getElementById("wifi").textContent = status.wifi || "—";
  var led = document.getElementById("led");
  led.className = status.powerOn ? "led on" : "led";
  document.getElementById("led-label").textContent = status.powerOn ? "ON" : "OFF";
  var button = document.getElementById("enable");
  button.disabled = !!status.holding;
  button.textContent = status.holding ? "Holding the button…" : "Enable PC";
}
document.getElementById("enable").addEventListener("click", async function () {
  var response = await fetch("/api/enable", { method: "POST" });
  if (!response.ok) return;
  var result = await response.json();
  document.getElementById("note").textContent = result.message || "";
  refresh();
});
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)HTML";

static const char kSettingsPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Settings · ESP-PowerOn</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<main>
<nav>
  <a href="/">Main</a>
  <a class="active" href="/settings">Settings</a>
  <a href="/logs">Logs</a>
</nav>
<h1>Settings</h1>
<h2>Servo</h2>
<label>Initial angle <input id="rest" type="number" min="0" max="180" step="1"></label>
<label>Push angle <input id="push" type="number" min="0" max="180" step="1"></label>
<label>Hold time (seconds) <input id="hold" type="number" min="1" max="30" step="1"></label>
<p class="hint">Enable PC moves the servo to the push angle, waits, then returns to the initial angle.</p>
<h2>Web server</h2>
<section id="wifi-section" hidden>
  <p class="hint">Wi-Fi settings are available while the device is in access-point mode.</p>
  <label>Wi-Fi name <input id="ssid" type="text" maxlength="32" autocomplete="off"></label>
  <label>Wi-Fi password <input id="wifi-password" type="password" maxlength="64" placeholder="Leave blank to keep the current password" autocomplete="off"></label>
</section>
<label class="check"><input id="ssl" type="checkbox"> Enable SSL</label>
<p id="ssl-state" class="hint"></p>
<label>Certificate <input id="cert-file" type="file" accept=".pem,.crt,.cer"></label>
<button class="secondary" id="upload-cert" type="button">Upload certificate</button>
<label>Private key <input id="key-file" type="file" accept=".pem,.key"></label>
<button class="secondary" id="upload-key" type="button">Upload private key</button>
<p class="hint">PEM files. A self-signed certificate makes the browser show a warning. Save after uploading both files so HTTPS restarts.</p>
<label>Login user <input id="user" type="text" maxlength="31" autocomplete="username"></label>
<label>Login password <input id="auth-password" type="password" maxlength="64" placeholder="Leave blank to keep the current password" autocomplete="new-password"></label>
<button id="save" type="button">Save</button>
<p id="note" class="hint"></p>
</main>
<script>
function note(text) { document.getElementById("note").textContent = text || ""; }
async function load() {
  var response = await fetch("/api/settings");
  if (!response.ok) return;
  var settings = await response.json();
  document.getElementById("rest").value = settings.restAngle;
  document.getElementById("push").value = settings.pushAngle;
  document.getElementById("hold").value = settings.pushSeconds;
  document.getElementById("ssl").checked = !!settings.sslEnabled;
  document.getElementById("user").value = settings.authUser || "";
  document.getElementById("ssid").value = settings.wifiSsid || "";
  document.getElementById("wifi-section").hidden = !settings.apMode;
  var parts = [];
  parts.push(settings.certStored ? "Certificate stored." : "Certificate missing.");
  parts.push(settings.keyStored ? "Private key stored." : "Private key missing.");
  if (settings.sslActive) parts.push("HTTPS is running on port 443.");
  if (settings.sslFailed) parts.push("HTTPS did not start, so this page is on HTTP.");
  document.getElementById("ssl-state").textContent = parts.join(" ");
}
async function upload(inputId, url, label) {
  var file = document.getElementById(inputId).files[0];
  if (!file) {
    note("Choose a " + label + " file first.");
    return;
  }
  var text = await file.text();
  var response = await fetch(url, { method: "POST", headers: { "Content-Type": "application/x-pem" }, body: text });
  var result = await response.json();
  note(result.message || "");
  if (result.ok) load();
}
document.getElementById("upload-cert").addEventListener("click", function () { upload("cert-file", "/api/cert", "certificate"); });
document.getElementById("upload-key").addEventListener("click", function () { upload("key-file", "/api/key", "private key"); });
document.getElementById("save").addEventListener("click", async function () {
  var body = new URLSearchParams();
  body.set("restAngle", document.getElementById("rest").value);
  body.set("pushAngle", document.getElementById("push").value);
  body.set("pushSeconds", document.getElementById("hold").value);
  body.set("authUser", document.getElementById("user").value);
  var authPassword = document.getElementById("auth-password").value;
  if (authPassword) body.set("authPassword", authPassword);
  if (!document.getElementById("wifi-section").hidden) {
    body.set("wifiSsid", document.getElementById("ssid").value);
    var wifiPassword = document.getElementById("wifi-password").value;
    if (wifiPassword) body.set("wifiPassword", wifiPassword);
  }
  if (document.getElementById("ssl").checked) body.set("sslEnabled", "1");
  var response = await fetch("/api/settings", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: body
  });
  var result = await response.json();
  note(result.message || "");
  if (result.ok && !result.restart) load();
});
load();
</script>
</body>
</html>
)HTML";

static const char kLogsPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Logs · ESP-PowerOn</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<main>
<nav>
  <a href="/">Main</a>
  <a href="/settings">Settings</a>
  <a class="active" href="/logs">Logs</a>
</nav>
<h1>Logs</h1>
<p class="hint">Newest first. Times use the device clock after it syncs. Earlier rows show uptime.</p>
<table>
  <thead><tr><th>Time</th><th>Event</th></tr></thead>
  <tbody id="rows"></tbody>
</table>
</main>
<script>
function formatUptime(total) {
  total = Math.max(0, total | 0);
  var days = Math.floor(total / 86400);
  var hours = Math.floor((total % 86400) / 3600);
  var minutes = Math.floor((total % 3600) / 60);
  var seconds = total % 60;
  function two(value) { return (value < 10 ? "0" : "") + value; }
  var clock = two(hours) + ":" + two(minutes) + ":" + two(seconds);
  return days ? days + "d " + clock : clock;
}
async function refresh() {
  var response = await fetch("/api/logs");
  if (!response.ok) return;
  var payload = await response.json();
  var rows = document.getElementById("rows");
  rows.replaceChildren();
  var events = payload.events || [];
  if (!events.length) {
    var empty = document.createElement("tr");
    var cell = document.createElement("td");
    cell.colSpan = 2;
    cell.textContent = "No events yet.";
    empty.appendChild(cell);
    rows.appendChild(empty);
    return;
  }
  events.forEach(function (event) {
    var row = document.createElement("tr");
    var time = document.createElement("td");
    var message = document.createElement("td");
    time.textContent = event.time ? event.time : "uptime " + formatUptime(event.uptimeSec);
    message.textContent = event.message || "";
    row.appendChild(time);
    row.appendChild(message);
    rows.appendChild(row);
  });
}
refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>
)HTML";
