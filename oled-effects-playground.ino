#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

// The HW-364 OLED uses software-I2C on GPIO14 (clock) and GPIO12 (data).
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
    U8G2_R0, /* clock=*/14, /* data=*/12, U8X8_PIN_NONE);

constexpr uint8_t LED_PIN = 13;  // NodeMCU D7; D6 is already used by the OLED.
constexpr uint8_t LED_COUNT = 8;
constexpr uint8_t LED_BRIGHTNESS = 48;
constexpr char AP_SSID[] = "Quest-Shift";
constexpr char AP_PASSWORD[] = "QUEST_ODO";
constexpr char WEB_USERNAME[] = "quest";
constexpr char WEB_PASSWORD[] = "QUEST_ODO";
constexpr float INITIAL_ODOMETER_KM = 184230.6f;

ESP8266WebServer server(80);
Adafruit_NeoPixel shiftLeds(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

enum class DriveMode : uint8_t { ECO, NORMAL, PERFORMANCE };

DriveMode driveMode = DriveMode::NORMAL;
uint32_t simulatorStartedAt = 0;
uint32_t lastSampleAt = 0;
uint32_t lastDisplayAt = 0;
uint32_t lastLedAt = 0;
float engineRpm = 900.0f;
float vehicleSpeedKmh = 0.0f;
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
  <meta name="theme-color" content="#11151b">
  <title>Quest Shift Display</title>
  <style>
    :root{color-scheme:dark;--bg:#11151b;--panel:#1b222b;--muted:#9ba9b7;--text:#f4f7fa;--green:#73e0a1;--amber:#ffc45c;--red:#ff6677}
    *{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at top,#26303b,var(--bg) 60%);color:var(--text);font:16px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}
    main{max-width:760px;margin:auto;padding:24px 16px 40px}.top{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:18px}
    h1{font-size:1.15rem;letter-spacing:.11em;margin:0}.subtitle,.label,.foot{color:var(--muted);font-size:.82rem}.badge{border:1px solid #495565;border-radius:20px;padding:5px 10px;color:var(--green);font-size:.8rem}
    .panel{background:linear-gradient(145deg,#202833,#171d25);border:1px solid #313d49;border-radius:18px;padding:18px;margin:12px 0;box-shadow:0 12px 30px #0003}
    .hero{display:grid;grid-template-columns:1.2fr 1fr;gap:12px;align-items:center}.rpm{font-size:clamp(3rem,15vw,5.5rem);font-weight:750;line-height:1;letter-spacing:-.06em}.unit{color:var(--muted);font-size:.85rem}
    .speed{text-align:right;font-size:2rem;font-weight:650}.bar{height:13px;border-radius:9px;background:linear-gradient(90deg,#35bd7a 0 45%,#eac04d 68%,#f18343 85%,#f04e5e);position:relative;margin:18px 0 8px;overflow:hidden}
    .bar:after{content:"";position:absolute;inset:0 auto 0 0;width:var(--rpm);background:#11151b66;border-right:2px solid white}
    .ticks{display:flex;justify-content:space-between;color:var(--muted);font-size:.7rem}.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}
    .metric{background:#121820aa;border-radius:12px;padding:13px}.value{font-size:1.3rem;font-weight:650;margin-top:3px}.modes{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:10px}
    button{border:1px solid #485565;background:#202833;color:var(--text);padding:12px 8px;border-radius:11px;font:inherit;font-weight:600;cursor:pointer}
    button.active{border-color:var(--green);color:var(--green);background:#173126}button:focus-visible{outline:2px solid white;outline-offset:2px}
    .leds{display:flex;gap:8px;align-items:center;margin-top:12px}.led{width:24px;height:12px;border-radius:8px;background:#35404b;box-shadow:inset 0 0 5px #0008}
    .led.on:nth-child(-n+4){background:#47db88;box-shadow:0 0 12px #47db8880}.led.on:nth-child(n+5):nth-child(-n+6){background:#ffd45e;box-shadow:0 0 12px #ffd45e80}.led.on:nth-child(n+7){background:#ff5969;box-shadow:0 0 12px #ff596980}
    .foot{margin-top:16px}.notice{color:#c6d0da;font-size:.8rem}#toast{min-height:1.3em;color:var(--amber);font-size:.85rem}
    @media(max-width:480px){main{padding:18px 12px 30px}.panel{padding:15px}.hero{grid-template-columns:1fr auto}.value{font-size:1.12rem}}
  </style>
</head>
<body>
<main>
  <header class="top">
    <div><h1>TOYOTA QUEST</h1><div class="subtitle">1.6L SHIFT DISPLAY · SIMULATOR</div></div>
    <span class="badge" id="connection">CONNECTING</span>
  </header>
  <section class="panel">
    <div class="hero">
      <div><div class="rpm" id="rpm">----</div><div class="unit">ENGINE RPM</div></div>
      <div class="speed"><span id="speed">--</span><div class="unit">km/h</div></div>
    </div>
    <div class="bar" id="rpmBar" style="--rpm:0%"></div>
    <div class="ticks"><span>0</span><span>2k</span><span>4k</span><span>6k</span><span>7k</span></div>
    <div class="label" style="margin-top:16px">SHIFT-LIGHT STRIP · GPIO13 / D7</div>
    <div class="leds" id="leds" aria-label="Simulated shift light"><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i></div>
  </section>
  <section class="panel">
    <div class="label">DRIVE MODE</div>
    <div class="modes">
      <button data-mode="eco">ECO</button><button data-mode="normal">NORMAL</button><button data-mode="performance">PERFORMANCE</button>
    </div>
    <div id="toast" role="status"></div>
  </section>
  <section class="grid">
    <article class="panel metric"><div class="label">AVERAGE FUEL ECONOMY</div><div class="value"><span id="average">--</span> L/100 km</div></article>
    <article class="panel metric"><div class="label">CURRENT ESTIMATE</div><div class="value"><span id="instant">--</span> L/100 km</div></article>
    <article class="panel metric"><div class="label">TOTAL ODOMETER</div><div class="value"><span id="odometer">--</span> km</div></article>
    <article class="panel metric"><div class="label">SESSION DISTANCE</div><div class="value"><span id="trip">--</span> km</div></article>
    <article class="panel metric"><div class="label">COOLANT</div><div class="value"><span id="coolant">--</span> °C</div></article>
    <article class="panel metric"><div class="label">THROTTLE (SIMULATED)</div><div class="value"><span id="throttle">--</span>%</div></article>
  </section>
  <p class="foot notice">Demo telemetry only — no ELM327 connection and no vehicle or gearbox control. Values reset when the device restarts.</p>
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
    $('rpm').textContent=Math.round(d.rpm).toLocaleString();
    $('speed').textContent=Math.round(d.speed);
    $('average').textContent=Number(d.averageFuel).toFixed(1);
    $('instant').textContent=Number(d.instantFuel).toFixed(1);
    $('odometer').textContent=Number(d.odometer).toLocaleString(undefined,{minimumFractionDigits:1,maximumFractionDigits:1});
    $('trip').textContent=Number(d.trip).toFixed(2);
    $('coolant').textContent=Math.round(d.coolant);
    $('throttle').textContent=Math.round(d.throttle);
    $('rpmBar').style.setProperty('--rpm',Math.min(100,d.rpm/7000*100)+'%');
    buttons.forEach(button=>button.classList.toggle('active',button.dataset.mode===d.mode));
    [...$('leds').children].forEach((led,index)=>led.classList.toggle('on',index<d.leds));
    document.title=Math.round(d.rpm)+' RPM · Quest Shift';
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
  char json[320];
  const float ledProgress = engineRpm / shiftRpm() * LED_COUNT;
  const uint8_t ledCount = ledProgress >= LED_COUNT
                               ? LED_COUNT
                               : static_cast<uint8_t>(ledProgress);
  snprintf(json, sizeof(json),
           "{\"mode\":\"%s\",\"rpm\":%.0f,\"speed\":%.1f,"
           "\"averageFuel\":%.2f,\"instantFuel\":%.2f,"
           "\"odometer\":%.1f,\"trip\":%.3f,\"coolant\":%.1f,"
           "\"throttle\":%.1f,\"leds\":%u}",
           modeSlug(), engineRpm, vehicleSpeedKmh, averageFuelLPer100Km,
           instantFuelLPer100Km, INITIAL_ODOMETER_KM + sessionDistanceKm,
           sessionDistanceKm, coolantTempC, throttlePercent, ledCount);
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
  u8g2.setFont(u8g2_font_7x14_tf);
  u8g2.drawStr(24, 21, "TOYOTA");
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(23, 34, "QUEST 1.6L");
  u8g2.drawStr(26, 45, "SHIFT DISPLAY");
  u8g2.drawFrame(15, 52, 98, 7);
  u8g2.drawBox(17, 54, 94, 3);
  u8g2.sendBuffer();
}

void drawDashboard() {
  char text[24];
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(2, 8, "TOYOTA QUEST 1.6L");
  u8g2.drawStr(91, 8, modeName());
  u8g2.drawHLine(0, 11, 128);

  snprintf(text, sizeof(text), "%.1fK", engineRpm / 1000.0f);
  u8g2.setFont(u8g2_font_7x14_tf);
  u8g2.drawStr(2, 27, text);
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(44, 26, "RPM");
  snprintf(text, sizeof(text), "%.0f km/h", vehicleSpeedKmh);
  u8g2.drawStr(77, 26, text);

  const float barProgress = engineRpm / 7000.0f * 16;
  const uint8_t bars = barProgress >= 16 ? 16 : static_cast<uint8_t>(barProgress);
  for (uint8_t i = 0; i < 16; ++i) {
    const int16_t x = 2 + i * 8;
    if (i < bars) {
      u8g2.drawBox(x, 31, 6, 6);
    } else {
      u8g2.drawFrame(x, 31, 6, 6);
    }
  }

  snprintf(text, sizeof(text), "SHIFT %.1fK", shiftRpm() / 1000.0f);
  u8g2.drawStr(2, 46, text);
  snprintf(text, sizeof(text), "AVG %.1f L/100", averageFuelLPer100Km);
  u8g2.drawStr(2, 55, text);
  snprintf(text, sizeof(text), "ODO %.0f km", INITIAL_ODOMETER_KM + sessionDistanceKm);
  u8g2.drawStr(2, 63, text);
  u8g2.sendBuffer();
}

void updateSimulator(uint32_t now) {
  const float elapsedSeconds = (now - simulatorStartedAt) / 1000.0f;
  const float wave = sinf(elapsedSeconds * 0.47f);
  const float speedWave = sinf(elapsedSeconds * 0.16f + 0.8f);
  engineRpm = 900.0f + (wave + 1.0f) * 2850.0f;
  vehicleSpeedKmh = 52.0f + speedWave * 45.0f;
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
  const bool shiftAlert = engineRpm >= shiftPoint;
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
