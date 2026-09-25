# ESP-PowerOn

ESP-PowerOn is a remote power-control device built on an ESP32. A web interface lets you control a PC from a browser. A servo presses and releases the computer's power button, and a photoresistor reads the power-on state from the PC power LED.

It is similar to mechanical button pushers and to motherboard power-control projects, with two differences: the button is pushed by a servo, and the power state is read optically from the power LED with a photoresistor. Nothing is wired to the motherboard.

## Similar projects

### Mechanical button pushers (servo / solenoid)

These press a button from outside the device, without changing its wiring.

- `vanyisg/esp-button-pusher` — ESP8266/ESP32 and a mini SG90 servo. A local web page has Press controls that rotate the servo to an angle and pull it back, like a mechanical finger.
- `fanthomas/ESP32-Mqtt-Servo-Button-Pusher` — the same kind of servo pusher, with commands over MQTT so it can be driven from Node-RED, Home Assistant, or openHAB instead of a page hosted on the board.

ESP-PowerOn uses that servo approach, and adds the photoresistor so the page can show whether the PC power LED is on.

### Direct motherboard power controls (relay / optocoupler)

These projects switch the front-panel power header electrically. [ESP32PCRemote](https://github.com/fnskye/ESP32PCRemote) is the relay version: an ESP32 web interface pulses a relay across the power switch. The projects below use the same idea with different isolation or sensing.

- `Aandis/ESP32-PC-Power-Control` — asynchronous web server on an ESP32. An optocoupler, instead of a mechanical relay, bridges the front-panel power header for a silent, isolated press.
- `AitorGs/ESP-PC-Remote-Control` — ESP8266/ESP32 firmware that reads the computer state from the motherboard power-LED pin and cycles power with a relay.

ESP-PowerOn does not use a relay, an optocoupler, or the motherboard LED pin. The servo is the button, and the photoresistor watches the light from the power LED.

### Smart-home firmware

- [esphome/esphome](https://github.com/esphome/esphome) — a YAML configuration, rather than an Arduino sketch, can drive a servo or a relay and expose it to Home Assistant, Apple Home, or Google Home over local Wi-Fi.

ESP-PowerOn is a small PlatformIO sketch with its own web page, for a device that only needs to tap one power button and report the LED.

## Comparing the methods

| Approach | Actuation | How power state is read | Primary interface | Best use |
| --- | --- | --- | --- | --- |
| ESP-PowerOn | Servo arm presses and releases the case button | Photoresistor aimed at the power LED | Web page on the ESP32 | A PC you do not want to open or wire into |
| Mechanical servo pusher | Physical arm rotation | Usually none | Web server or MQTT | Appliances, light switches, and other external buttons |
| ESP32PCRemote (relay) | Electromagnetic switch across the power header | Controller status, not the PC LED | Web interface | Desktop PCs and other low-voltage switch loops |
| Optocoupler control | Optical / solid-state switch on the power header | Motherboard power-LED pin | Web interface or API | Desktop PCs where the press should be silent and isolated |
| ESPHome YAML | Servo or relay | Whatever the YAML sensor reads | Home Assistant and other smart-home apps | A smart-home setup without a custom sketch |

## What the firmware does

The sketch in this repository is the base ESP32 firmware for that device:

- Hosts a web page with **Tap**, **Press**, and **Release**. Tap holds the button for a short time, then releases it. Press and Release move the servo and leave it there.
- Reads the photoresistor and shows **Power on** or **Power off**, plus the raw light reading, so the threshold can be tuned to the LED.
- Joins the Wi-Fi network in `include/secrets.h` (or `build_flags` in `platformio.ini`). If that network is missing, it starts an access point named `ESP-PowerOn`.

## Hardware

| Part | Connection |
| --- | --- |
| Servo signal (orange / yellow) | GPIO 18 |
| Servo power (red) | 5 V. An external 5 V supply is safer than the ESP32 5 V pin; share ground with the ESP32 |
| Servo ground (brown / black) | GND |
| Photoresistor divider | 3V3 — photoresistor — GPIO 34 — 10 kΩ — GND, aimed at the power LED |

GPIO 34 is an ADC1 pin. ADC2 pins stop working while Wi-Fi is on, so keep the photoresistor on ADC1 (GPIO 32–39 on the classic ESP32).

With the LED off and on, compare the light reading on the web page and set `kPowerOnThreshold` in `include/config.h` between those two values. If a brighter LED makes the reading fall, set `kPowerOnWhenAbove` to `false` or swap the photoresistor and the 10 kΩ resistor. Servo travel is `kServoReleaseAngle` and `kServoPressAngle`.

## Open in VS Code with PlatformIO

1. Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension.
2. Open this folder. PlatformIO uses `platformio.ini` at the repository root. The recommended extension is listed in `.vscode/extensions.json`.
3. Copy `include/secrets.h.example` to `include/secrets.h` and set the Wi-Fi name and password. Leave the SSID empty to use only the access point (password `esp-poweron`, address http://192.168.4.1).
4. Build, upload, and open the serial monitor from the PlatformIO toolbar. The monitor prints the IP address. On the station network the page is also at http://esp-poweron.local when mDNS works.

From a terminal, with the [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html) on your path:

```bash
pio run
pio run -t upload
pio device monitor
```

The default board is `esp32dev` (classic ESP32). Change `board` in `platformio.ini` if you use another module.

## Project layout

| Path | Purpose |
| --- | --- |
| `platformio.ini` | PlatformIO environment: ESP32, Arduino framework, ESP32Servo |
| `src/main.cpp` | Wi-Fi, web server, servo, photoresistor |
| `include/config.h` | Pins, servo angles, LED threshold |
| `include/secrets.h` | Wi-Fi credentials (not committed; see `secrets.h.example`) |
| `lib/` | Local libraries, if you add any |
| `test/` | PlatformIO tests, if you add any |

## License

MIT. See [LICENSE](LICENSE).
