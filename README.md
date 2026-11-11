# Toyota Quest 1.6L Shift Display Simulator

An ESP8266-based prototype for a Toyota Quest-inspired dashboard. It simulates a three-mode shift display on the built-in 128x64 OLED, serves a password-protected dashboard over its own Wi-Fi access point, and drives an addressable RGB strip as a simulated shift light.

**This is a visual simulator only.** It does not connect to an ELM327, read real vehicle telemetry, or control a gearbox, throttle, engine, or other vehicle system. Demo values reset when the board restarts. The dashboard uses a Toyota text wordmark and a simple `T` monogram, not the official emblem artwork.

## Features

- ECO, NORMAL, and PERFORMANCE modes, selectable from the web dashboard.
- Large simulated gear indicator, speed, and RPM shift bar.
- Up-shift arrow at the selected mode's RPM limit and down-shift arrow below 1,600 RPM when above first gear.
- Simulated RPM, speed, coolant temperature, and throttle.
- Average and current estimated fuel economy in L/100 km.
- Session trip distance (no odometer display).
- RPM shift-light animation on an 8-pixel addressable RGB strip.
- Monochrome OLED boot screen and live dashboard.
- Local Wi-Fi access point and HTTP Basic Authentication.

## Hardware and pins

- ESP8266 NodeMCU / HW-364-style board with SSD1306 128x64 OLED.
- OLED software-I²C clock: GPIO14; data: GPIO12.
- WS2812B/NeoPixel-compatible GRB strip: data input to GPIO4 (NodeMCU D2). Addressable GRB strips use a single data pin; D0/D1 are not separate color-channel pins.
- Connect the strip's ground to board ground. Power the strip from an appropriate external supply when required, with a common ground; do not draw high strip current from the ESP8266 3.3V pin.
- D6/GPIO12 is already occupied by the OLED. D8/GPIO15 affects ESP8266 boot mode and is not used.

Use a suitable level shifter for a 5V LED strip data input if needed. Keep the strip short and brightness low during prototyping.

## Install and upload

Install the ESP8266 Arduino core and these libraries:

- U8g2 by olikraus
- Adafruit NeoPixel

Select a compatible ESP8266 board profile, then compile and upload:

```sh
arduino-cli core install esp8266:esp8266
arduino-cli lib install U8g2
arduino-cli lib install "Adafruit NeoPixel"
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
