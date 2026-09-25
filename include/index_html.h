#pragma once

#include <Arduino.h>

static const char kIndexHtml[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP-PowerOn</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; font-family: sans-serif; background: #121417; color: #e8eaed; }
  main { max-width: 28rem; margin: 0 auto; padding: 1.5rem; }
  h1 { font-size: 1.4rem; margin: 0 0 0.25rem; }
  .lead { color: #b0b6bd; margin-top: 0; }
  .card { background: #1c2128; border-radius: 12px; padding: 1rem 1.1rem; margin: 1rem 0; }
  .state { font-size: 1.8rem; font-weight: 700; }
  .on { color: #3dd68c; }
  .off { color: #8b939c; }
  .meta { color: #b0b6bd; font-size: 0.95rem; line-height: 1.45; }
  .actions { display: flex; gap: 0.5rem; }
  form { flex: 1; margin: 0; }
  button { width: 100%; font: inherit; padding: 0.85rem 0.4rem; border: 0; border-radius: 8px; background: #2f6fed; color: white; }
  button.release { background: #3a424d; }
</style>
</head>
<body>
<main>
  <h1>ESP-PowerOn</h1>
  <p class="lead">The servo presses the PC power button. A photoresistor reads the power LED.</p>
  <section class="card">
    <div id="state" class="state off">Reading…</div>
    <p class="meta">Light reading <span id="adc">–</span> (threshold <span id="threshold">–</span>)<br>Servo <span id="servo">–</span></p>
  </section>
  <div class="actions">
    <form method="post" action="/tap"><button type="submit">Tap</button></form>
    <form method="post" action="/press"><button type="submit">Press</button></form>
    <form method="post" action="/release"><button type="submit" class="release">Release</button></form>
  </div>
</main>
<script>
async function refresh() {
  const response = await fetch("/status");
  if (!response.ok) return;
  const status = await response.json();
  const state = document.getElementById("state");
  state.textContent = status.powerOn ? "Power on" : "Power off";
  state.className = "state " + (status.powerOn ? "on" : "off");
  document.getElementById("adc").textContent = status.adc;
  document.getElementById("threshold").textContent = status.threshold;
  const servo = document.getElementById("servo");
  servo.textContent = status.holding ? "holding the button" : (status.pressed ? "pressed" : "released");
}
document.querySelectorAll("form").forEach((form) => {
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    await fetch(form.action, { method: "POST" });
    refresh();
  });
});
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)HTML";
