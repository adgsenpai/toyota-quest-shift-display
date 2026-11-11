# Toyota Quest 1.6L Shift Display Simulator

An ESP8266-based prototype for a Toyota Quest-inspired dashboard. It simulates a three-mode shift display on the built-in 128x64 OLED, serves a password-protected dashboard over its own Wi-Fi access point, and drives an analog RGB strip as a simulated shift light.

**This is a visual simulator only.** It does not connect to an ELM327, read real vehicle telemetry, or control a gearbox, throttle, engine, or other vehicle system. Live demo readings reset when the board restarts; stored trip totals, shift history, and selected mode are retained in flash. The dashboard uses the Toyota SVG supplied with the project; the OLED boot screen draws a compact monochrome emblem.

## Features

- ECO, NORMAL, and PERFORMANCE modes, selectable from the web dashboard.
- Large simulated 1–6 gear indicator, speed, and RPM shift bar, with a large up/down arrow beside the gear number (no arrow text).
- Up-shift arrow at the selected mode's RPM limit and down-shift arrow below 1,600 RPM when above first gear.
- Simulated RPM, speed, coolant temperature, and throttle.
- Average and current estimated fuel economy in L/100 km.
- Session trip distance (no odometer display).
- RPM shift-light colors on an analog RGB strip, transitioning green to amber to red. All LEDs on an analog strip always show the same color; individual LED control requires an addressable strip such as WS2812B/NeoPixel.
- On-chip flash memory remembers the selected mode, trip/fuel totals, and the most recent 16 simulated shift-decision samples across restarts.
- Bounded shift recommendations: never suggest an upshift above 6th gear or a downshift below 1st.
- Two-tone OLED layout: Toyota emblem and header are kept in the yellow top band; gear, shift arrow, and live readings stay in the blue area.
- Local Wi-Fi access point and HTTP Basic Authentication.

## Hardware and pins

- ESP8266 NodeMCU / HW-364-style board with SSD1306 128x64 OLED.
- The OLED's yellow top band is 16 pixels high; the dashboard positions its logo/header and blue-band driving data to respect this physical color split.
- OLED software-I²C clock: GPIO14; data: GPIO12.
- Analog RGB strip color channels: green to GPIO16 (NodeMCU D0), blue to GPIO5 (D1), red to GPIO4 (D2). The dashboard represents the strip LEDs in a single uniform color to match the hardware.
- Drive each strip channel through a suitable transistor/MOSFET driver; never connect a strip's power or channel current directly to ESP8266 GPIO pins. Use an appropriately rated external strip supply and connect its ground to board ground.
- This pin mapping is for an analog strip with separate color channels, not a WS2812/NeoPixel strip (which uses one data input).
- D6/GPIO12 is already occupied by the OLED. D8/GPIO15 affects ESP8266 boot mode and is not used.

Check the strip's common-anode/common-cathode wiring and choose compatible channel drivers. Keep brightness low during prototyping.

## On-chip memory and ML preparation

The firmware uses the ESP8266's emulated EEPROM area in flash; no separate memory module is needed. It saves the selected mode immediately when changed and checkpoints accumulated trip/fuel values and a 16-sample rolling shift history about every 15 minutes. A sudden power loss can therefore lose changes since the last checkpoint. The dashboard shows the remembered sample and shift-cue counts.

This is groundwork for later machine-learning experiments, not machine learning yet. Stored RPM, speed, throttle, gear, mode, and cue values come from the simulator, not from the car. A future model needs real, validated OBD data and a safe evaluation process; it must not control the vehicle.

## Install and upload

Install the ESP8266 Arduino core and these libraries:

- U8g2 by olikraus

Select a compatible ESP8266 board profile, then compile and upload:

```sh
arduino-cli core install esp8266:esp8266
arduino-cli lib install U8g2
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 .
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 --port /dev/ttyUSB0 .
```

Open the serial monitor at 115200 baud. Connect a phone or computer to the `Quest-Shift` Wi-Fi network and browse to `http://192.168.4.1`.

| Access | Value |
| --- | --- |
| Wi-Fi password | `QUEST_ODO` |
| Dashboard username | `quest` |
| Dashboard password | `QUEST_ODO` |

The ECO, NORMAL, and PERFORMANCE settings change only the simulator's display and shift-light threshold. The RPM, speed, and fuel economy are generated demo values and are not calibrated for the vehicle.
