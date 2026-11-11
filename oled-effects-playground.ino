#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

// The HW-364 OLED uses software-I2C on GPIO14 (clock) and GPIO12 (data).
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
    U8G2_R0, /* clock=*/14, /* data=*/12, U8X8_PIN_NONE);

constexpr uint8_t LED_PIN = 4;  // NodeMCU D2; GRB addressable strips use one data pin.
constexpr uint8_t LED_COUNT = 8;
constexpr uint8_t LED_BRIGHTNESS = 48;
constexpr char AP_SSID[] = "Quest-Shift";
constexpr char AP_PASSWORD[] = "QUEST_ODO";
constexpr char WEB_USERNAME[] = "quest";
constexpr char WEB_PASSWORD[] = "QUEST_ODO";
ESP8266WebServer server(80);
Adafruit_NeoPixel shiftLeds(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

enum class DriveMode : uint8_t { ECO, NORMAL, PERFORMANCE };
enum class ShiftCue : uint8_t { NONE, UP, DOWN };

DriveMode driveMode = DriveMode::NORMAL;
uint32_t simulatorStartedAt = 0;
uint32_t lastSampleAt = 0;
uint32_t lastDisplayAt = 0;
uint32_t lastLedAt = 0;
float engineRpm = 900.0f;
float vehicleSpeedKmh = 0.0f;
uint8_t simulatedGear = 1;
float coolantTempC = 89.0f;
float throttlePercent = 0.0f;
float sessionDistanceKm = 0.0f;
float fuelUsedLitres = 0.0f;
float averageFuelLPer100Km = 7.4f;
float instantFuelLPer100Km = 7.4f;

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#111214">
  <title>Quest Shift Display</title>
  <style>
    :root{color-scheme:dark;--bg:#111214;--panel:#1c1d20;--muted:#9b9da2;--text:#f4f3f0;--red:#eb3038;--green:#75e3a0;--amber:#ffd15c}
    *{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at 48% -15%,#383033 0,#171719 40%,var(--bg) 74%);color:var(--text);font:15px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}
    main{max-width:720px;margin:auto;padding:24px 18px 42px}.top{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}
    .brand{display:flex;align-items:center;gap:12px}.brand-mark{display:grid;place-items:center;width:48px;height:48px;border:2px solid var(--red);border-radius:50%;color:var(--red);font-size:1.35rem;font-weight:850;font-style:italic;letter-spacing:-.12em;padding-right:3px}
    h1{font-size:1.35rem;font-weight:850;letter-spacing:.13em;margin:0}.subtitle,.label,.foot{color:var(--muted);font-size:.76rem;letter-spacing:.08em}.badge{border:1px solid #46474a;border-radius:20px;padding:6px 10px;color:var(--green);font-size:.7rem;letter-spacing:.06em}
    .panel{background:linear-gradient(145deg,#242528,#191a1c);border:1px solid #353639;border-radius:16px;padding:18px;margin:12px 0;box-shadow:0 14px 36px #0004}
    .instrument{position:relative;overflow:hidden;padding:24px 22px 18px;border-color:#464042;background:linear-gradient(130deg,#282527,#171719 58%,#242123)}
    .instrument:before{content:"";position:absolute;left:0;top:18px;bottom:18px;width:3px;background:var(--red);border-radius:4px}
    .hero{display:grid;grid-template-columns:1.08fr 1fr;gap:14px;align-items:center}.gear-block{display:flex;align-items:center;gap:16px}.gear-number{font-size:clamp(6.5rem,27vw,10rem);font-weight:800;line-height:.78;letter-spacing:-.1em;color:#fff;text-shadow:0 0 35px #fff2}
    .gear-label{writing-mode:vertical-rl;transform:rotate(180deg);font-size:.72rem;letter-spacing:.22em;color:var(--muted)}
    .stats{border-left:1px solid #414145;padding-left:20px}.speed{font-size:clamp(2.5rem,11vw,4rem);font-weight:720;line-height:1;letter-spacing:-.06em}.unit{color:var(--muted);font-size:.77rem;letter-spacing:.12em}    .rpm-line{display:flex;align-items:baseline;gap:8px;margin-top:14px}.rpm{font-size:1.6rem;font-weight:650;font-variant-numeric:tabular-nums}.shift-state{min-height:1.2em;margin-top:2px;font-size:.85rem;font-weight:850;letter-spacing:.12em}.shift-state.up{color:var(--red)}.shift-state.down{color:#65baff}
    .bar{height:10px;border-radius:8px;background:linear-gradient(90deg,#35bd7a 0 45%,#eac04d 68%,#f18343 85%,#f04e5e);position:relative;margin:16px 0 7px;overflow:hidden}
    .bar:after{content:"";position:absolute;inset:0 auto 0 0;width:var(--rpm);background:#11121466;border-right:2px solid white}
    .ticks{display:flex;justify-content:space-between;color:var(--muted);font-size:.65rem}
    .strip-label{margin-top:17px}.leds{display:flex;gap:8px;align-items:center;margin-top:10px}.led{flex:1;height:12px;border-radius:8px;background:#35363a;box-shadow:inset 0 0 5px #0008}
    .led.on:nth-child(-n+4){background:#47db88;box-shadow:0 0 12px #47db8880}.led.on:nth-child(n+5):nth-child(-n+6){background:#ffd45e;box-shadow:0 0 12px #ffd45e80}.led.on:nth-child(n+7){background:#ff5969;box-shadow:0 0 12px #ff596980}
    .mode-panel{padding:17px 18px}.mode-heading{display:flex;justify-content:space-between;align-items:center}.current-mode{color:var(--red);font-size:.76rem;font-weight:750;letter-spacing:.12em}
    .modes{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:11px}
    button{border:1px solid #414246;background:#242528;color:#b8b8ba;padding:12px 6px;border-radius:9px;font:inherit;font-size:.8rem;font-weight:750;letter-spacing:.06em;cursor:pointer;transition:background .15s,border-color .15s,color .15s}
    button.active{border-color:var(--red);color:white;background:#492327;box-shadow:inset 0 -2px var(--red)}button:focus-visible{outline:2px solid white;outline-offset:2px}
    .grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.metric{margin:0;background:#191a1c;padding:15px}.value{font-size:1.2rem;font-weight:650;margin-top:5px;font-variant-numeric:tabular-nums}.metric .label{font-size:.66rem}
    .foot{margin:16px 2px 0;letter-spacing:0}.notice{color:#c6c2c2;font-size:.75rem}#toast{min-height:1.2em;color:var(--amber);font-size:.78rem;margin-top:8px}
    @media(max-width:520px){main{padding:16px 12px 30px}.top{margin-bottom:13px}.brand-mark{width:42px;height:42px}.instrument{padding:22px 16px 16px}.gear-block{gap:10px}.gear-number{font-size:clamp(6rem,29vw,8rem)}.stats{padding-left:14px}.grid{grid-template-columns:repeat(2,1fr);gap:8px}.metric{padding:13px}.value{font-size:1.08rem}}
    @media(max-width:360px){.subtitle{font-size:.65rem}.badge{padding:5px 7px;font-size:.62rem}.gear-number{font-size:5.7rem}.speed{font-size:2.4rem}.modes{gap:5px}button{font-size:.7rem}}
  </style>
</head>
<body>
<main>
  <header class="top">
    <div class="brand"><div class="brand-mark" aria-hidden="true">T</div><div><h1>TOYOTA</h1><div class="subtitle">QUEST 1.6 · SHIFT DISPLAY</div></div></div>
    <span class="badge" id="connection">CONNECTING</span>
  </header>
  <section class="panel instrument">
    <div class="hero">
      <div class="gear-block"><div class="gear-number" id="gear">1</div><div class="gear-label">CURRENT GEAR</div></div>
      <div class="stats">
        <div class="speed"><span id="speed">--</span></div><div class="unit">KM/H</div>
        <div class="rpm-line"><span class="rpm" id="rpm">----</span><span class="unit">RPM</span></div>
        <div class="shift-state" id="shiftState" aria-live="polite"></div>
      </div>
    </div>
    <div class="bar" id="rpmBar" style="--rpm:0%"></div>
    <div class="ticks"><span>0</span><span>2k</span><span>4k</span><span>6k</span><span>7k</span></div>
    <div class="label strip-label">SHIFT LIGHT · GPIO4 / D2 · GRB</div>
    <div class="leds" id="leds" aria-label="Simulated shift light"><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i></div>
  </section>
  <section class="panel mode-panel">
    <div class="mode-heading"><div class="label">DRIVE MODE</div><div class="current-mode" id="modeName">NORMAL</div></div>
    <div class="modes">
      <button data-mode="eco">ECO</button><button data-mode="normal">NORMAL</button><button data-mode="performance">PERFORMANCE</button>
    </div>
    <div id="toast" role="status"></div>
  </section>
  <section class="grid">
    <article class="panel metric"><div class="label">AVG. FUEL ECONOMY</div><div class="value"><span id="average">--</span> L/100</div></article>
    <article class="panel metric"><div class="label">CURRENT ESTIMATE</div><div class="value"><span id="instant">--</span> L/100</div></article>
    <article class="panel metric"><div class="label">TRIP DISTANCE</div><div class="value"><span id="trip">--</span> km</div></article>
    <article class="panel metric"><div class="label">COOLANT TEMP</div><div class="value"><span id="coolant">--</span> °C</div></article>
  </section>
  <p class="foot notice">SIMULATED DASHBOARD · No ELM327 connection or vehicle control. Trip and demo values reset on restart.</p>
  <p class="foot">Wi-Fi: Quest-Shift · Open <b>192.168.4.1</b> · Login: <b>quest</b> / <b>QUEST_ODO</b></p>
</main>
<script>
const $=id=>document.getElementById(id);
const buttons=[...document.querySelectorAll('[data-mode]')];
async function update(){
  try{
    const response=await fetch('/api',{cache:'no-store'});
    if(!response.ok)throw new Error('HTTP '+response.status);
    const d=await response.json();
    $('connection').textContent='SIMULATOR ONLINE';
    $('gear').textContent=d.gear;
    $('rpm').textContent=Math.round(d.rpm).toLocaleString();
    $('speed').textContent=Math.round(d.speed);
    $('modeName').textContent=d.mode.toUpperCase();
    $('average').textContent=Number(d.averageFuel).toFixed(1);
    $('instant').textContent=Number(d.instantFuel).toFixed(1);
    $('trip').textContent=Number(d.trip).toFixed(2);
    $('coolant').textContent=Math.round(d.coolant);
    $('rpmBar').style.setProperty('--rpm',Math.min(100,d.rpm/7000*100)+'%');
    const shiftState=$('shiftState');
    shiftState.textContent=d.shift==='up'?'↑ SHIFT UP':d.shift==='down'?'↓ SHIFT DOWN':'';
    shiftState.classList.toggle('up',d.shift==='up');
    shiftState.classList.toggle('down',d.shift==='down');
    buttons.forEach(button=>button.classList.toggle('active',button.dataset.mode===d.mode));
    [...$('leds').children].forEach((led,index)=>led.classList.toggle('on',index<d.leds));
    document.title='Gear '+d.gear+' · Toyota Quest';
  }catch(error){$('connection').textContent='OFFLINE';}
}
buttons.forEach(button=>button.addEventListener('click',async()=>{
  $('toast').textContent='';
  try{
    const response=await fetch('/mode?value='+encodeURIComponent(button.dataset.mode),{method:'POST'});
    if(!response.ok)throw new Error('Mode change failed ('+response.status+')');
    $('toast').textContent='Mode set to '+button.textContent+'. This only changes the simulator.';
    await update();
  }catch(error){$('toast').textContent=error.message;}
}));
update();setInterval(update,700);
</script>
</body>
</html>
)rawliteral";

const char *modeName() {
  switch (driveMode) {
    case DriveMode::ECO: return "ECO";
    case DriveMode::PERFORMANCE: return "PERFORMANCE";
    default: return "NORMAL";
  }
}

const char *modeSlug() {
  switch (driveMode) {
    case DriveMode::ECO: return "eco";
    case DriveMode::PERFORMANCE: return "performance";
    default: return "normal";
  }
}

float shiftRpm() {
  switch (driveMode) {
    case DriveMode::ECO: return 3800.0f;
    case DriveMode::PERFORMANCE: return 6400.0f;
    default: return 5200.0f;
  }
}

ShiftCue shiftCue() {
  if (engineRpm >= shiftRpm()) return ShiftCue::UP;
  if (simulatedGear > 1 && engineRpm <= 1600.0f) return ShiftCue::DOWN;
  return ShiftCue::NONE;
}

bool requireAuthentication() {
  if (server.authenticate(WEB_USERNAME, WEB_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

void handleRoot() {
  if (!requireAuthentication()) return;
  server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
}

void handleApi() {
  if (!requireAuthentication()) return;
  char json[360];
  const float ledProgress = engineRpm / shiftRpm() * LED_COUNT;
  const uint8_t ledCount = ledProgress >= LED_COUNT
                               ? LED_COUNT
                               : static_cast<uint8_t>(ledProgress);
  const ShiftCue cue = shiftCue();
  const char *cueName = cue == ShiftCue::UP ? "up" :
                        cue == ShiftCue::DOWN ? "down" : "none";
  snprintf(json, sizeof(json),
           "{\"mode\":\"%s\",\"gear\":%u,\"rpm\":%.0f,\"speed\":%.1f,"
           "\"averageFuel\":%.2f,\"instantFuel\":%.2f,"
           "\"trip\":%.3f,\"coolant\":%.1f,\"leds\":%u,\"shift\":\"%s\"}",
           modeSlug(), simulatedGear, engineRpm, vehicleSpeedKmh,
           averageFuelLPer100Km, instantFuelLPer100Km, sessionDistanceKm,
           coolantTempC, ledCount, cueName);
  server.send(200, "application/json", json);
}

void handleMode() {
  if (!requireAuthentication()) return;
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing mode value");
    return;
  }

  const String value = server.arg("value");
  if (value == "eco") {
    driveMode = DriveMode::ECO;
  } else if (value == "normal") {
    driveMode = DriveMode::NORMAL;
  } else if (value == "performance") {
    driveMode = DriveMode::PERFORMANCE;
  } else {
    server.send(400, "text/plain", "Mode must be eco, normal, or performance");
    return;
  }

  server.send(200, "text/plain", modeName());
}

void drawBootScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(22, 21, "TOYOTA");
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(30, 34, "QUEST 1.6L");
  u8g2.drawStr(29, 45, "SHIFT DISPLAY");
  u8g2.drawFrame(15, 52, 98, 7);
  u8g2.drawBox(17, 54, 94, 3);
  u8g2.sendBuffer();
}

void drawDashboard() {
  char text[24];
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(2, 8, "TOYOTA");
  u8g2.drawStr(3, 8, "TOYOTA");
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(38, 8, "QUEST 1.6L");
  u8g2.drawStr(90, 8, modeName());
  u8g2.drawHLine(0, 11, 128);

  u8g2.drawStr(3, 21, "GEAR");
  snprintf(text, sizeof(text), "%u", simulatedGear);
  u8g2.setFont(u8g2_font_logisoso32_tn);
  u8g2.drawStr(2, 60, text);

  u8g2.setFont(u8g2_font_5x8_tf);
  snprintf(text, sizeof(text), "%.0f", vehicleSpeedKmh);
  u8g2.setFont(u8g2_font_logisoso20_tn);
  u8g2.drawStr(49, 35, text);
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(101, 34, "km/h");
  snprintf(text, sizeof(text), "RPM %.0f", engineRpm);
  u8g2.drawStr(51, 45, text);
  snprintf(text, sizeof(text), "AVG %.1f L/100", averageFuelLPer100Km);
  u8g2.drawStr(51, 55, text);

  const ShiftCue cue = shiftCue();
  if (cue != ShiftCue::NONE) {
    const int16_t arrowX = 115;
    if (cue == ShiftCue::UP) {
      u8g2.drawLine(arrowX, 51, arrowX, 61);
      u8g2.drawTriangle(arrowX, 47, arrowX - 5, 53, arrowX + 5, 53);
      u8g2.drawStr(108, 43, "UP");
    } else {
      u8g2.drawLine(arrowX, 47, arrowX, 57);
      u8g2.drawTriangle(arrowX, 61, arrowX - 5, 55, arrowX + 5, 55);
      u8g2.drawStr(105, 43, "DOWN");
    }
  }

  const float barProgress = engineRpm / 7000.0f * 9;
  const uint8_t bars = barProgress >= 9 ? 9 : static_cast<uint8_t>(barProgress);
  for (uint8_t i = 0; i < 9; ++i) {
    const int16_t x = 53 + i * 8;
    if (i < bars) {
      u8g2.drawBox(x, 59, 6, 4);
    } else {
      u8g2.drawFrame(x, 59, 6, 4);
    }
  }
  u8g2.sendBuffer();
}

void updateSimulator(uint32_t now) {
  const float elapsedSeconds = (now - simulatorStartedAt) / 1000.0f;
  const float wave = sinf(elapsedSeconds * 0.47f);
  const float speedWave = sinf(elapsedSeconds * 0.16f + 0.8f);
  engineRpm = 900.0f + (wave + 1.0f) * 2850.0f;
  vehicleSpeedKmh = 52.0f + speedWave * 45.0f;
  if (vehicleSpeedKmh < 18.0f) {
    simulatedGear = 1;
  } else if (vehicleSpeedKmh < 35.0f) {
    simulatedGear = 2;
  } else if (vehicleSpeedKmh < 55.0f) {
    simulatedGear = 3;
  } else if (vehicleSpeedKmh < 75.0f) {
    simulatedGear = 4;
  } else {
    simulatedGear = 5;
  }
  throttlePercent = 17.0f + (wave + 1.0f) * 25.0f;
  coolantTempC = 89.0f + sinf(elapsedSeconds * 0.035f) * 2.0f;

  float modeBase = 7.6f;
  if (driveMode == DriveMode::ECO) modeBase = 6.4f;
  if (driveMode == DriveMode::PERFORMANCE) modeBase = 9.2f;
  instantFuelLPer100Km = modeBase + vehicleSpeedKmh / 110.0f * 0.7f +
                         throttlePercent / 100.0f * 1.4f;

  const uint32_t elapsedMs = now - lastSampleAt;
  if (elapsedMs > 0) {
    const float elapsedHours = elapsedMs / 3600000.0f;
    const float distanceDelta = vehicleSpeedKmh * elapsedHours;
    sessionDistanceKm += distanceDelta;
    fuelUsedLitres += instantFuelLPer100Km * distanceDelta / 100.0f;
    if (sessionDistanceKm > 0.001f) {
      averageFuelLPer100Km = fuelUsedLitres / sessionDistanceKm * 100.0f;
    } else {
      averageFuelLPer100Km = instantFuelLPer100Km;
    }
    lastSampleAt = now;
  }
}

void updateShiftLeds(uint32_t now) {
  const float shiftPoint = shiftRpm();
  const float ledProgress = engineRpm / shiftPoint * LED_COUNT;
  const uint8_t litCount = ledProgress >= LED_COUNT
                               ? LED_COUNT
                               : static_cast<uint8_t>(ledProgress);
  const bool shiftAlert = shiftCue() == ShiftCue::UP;
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    uint32_t color = 0;
    if (i < litCount) {
      if (i < 4) {
        color = shiftLeds.Color(45, 220, 110);
      } else if (i < 6) {
        color = shiftLeds.Color(255, 190, 45);
      } else {
        color = shiftLeds.Color(255, 45, 55);
      }
      if (shiftAlert && ((now / 180) & 1)) color = 0;
    }
    shiftLeds.setPixelColor(i, color);
  }
  shiftLeds.show();
}

void setup() {
  Serial.begin(115200);
  u8g2.begin();
  drawBootScreen();

  shiftLeds.begin();
  shiftLeds.setBrightness(LED_BRIGHTNESS);
  shiftLeds.clear();
  shiftLeds.show();

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println(F("ERROR: Could not start Quest-Shift access point"));
  } else {
    Serial.print(F("Quest-Shift Wi-Fi ready at http://"));
    Serial.println(WiFi.softAPIP());
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api", HTTP_GET, handleApi);
  server.on("/mode", HTTP_POST, handleMode);
  server.onNotFound([]() {
    if (!requireAuthentication()) return;
    server.send(404, "text/plain", "Not found");
  });
  server.begin();

  simulatorStartedAt = millis();
  lastSampleAt = simulatorStartedAt;
  lastDisplayAt = simulatorStartedAt;
  lastLedAt = simulatorStartedAt;
  Serial.println(F("Demo telemetry active; no vehicle connection or control"));
}

void loop() {
  server.handleClient();
  const uint32_t now = millis();
  updateSimulator(now);

  if (now - lastDisplayAt >= 180) {
    lastDisplayAt = now;
    drawDashboard();
  }
  if (now - lastLedAt >= 70) {
    lastLedAt = now;
    updateShiftLeds(now);
  }
}
