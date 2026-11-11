#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <U8g2lib.h>
#include <math.h>

// The HW-364 OLED uses software-I2C on GPIO14 (clock) and GPIO12 (data).
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
    U8G2_R0, /* clock=*/14, /* data=*/12, U8X8_PIN_NONE);

constexpr uint8_t LED_GREEN_PIN = 16;  // NodeMCU D0
constexpr uint8_t LED_BLUE_PIN = 5;    // NodeMCU D1
constexpr uint8_t LED_RED_PIN = 4;     // NodeMCU D2
constexpr uint8_t SHIFT_SAMPLE_CAPACITY = 16;
constexpr uint32_t PERSIST_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t PERSIST_MAGIC = 0x51535431UL;
constexpr uint8_t PERSIST_VERSION = 1;
constexpr char AP_SSID[] = "Quest-Shift";
constexpr char AP_PASSWORD[] = "QUEST_ODO";
constexpr char WEB_USERNAME[] = "quest";
constexpr char WEB_PASSWORD[] = "QUEST_ODO";
ESP8266WebServer server(80);

enum class DriveMode : uint8_t { ECO, NORMAL, PERFORMANCE };
enum class ShiftCue : uint8_t { NONE, UP, DOWN };

struct ShiftSample {
  uint16_t rpm;
  uint16_t speedTenthsKmh;
  uint8_t throttlePercent;
  uint8_t gear;
  uint8_t mode;
  uint8_t cue;
};

struct PersistentState {
  uint32_t magic;
  uint8_t version;
  uint8_t mode;
  uint8_t sampleCount;
  uint8_t sampleHead;
  float tripDistanceKm;
  float fuelUsedLitres;
  uint32_t upshiftEvents;
  uint32_t downshiftEvents;
  ShiftSample samples[SHIFT_SAMPLE_CAPACITY];
};

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
PersistentState persistentState{};
ShiftCue previousShiftCue = ShiftCue::NONE;
bool persistentStorageReady = false;
bool persistentStateDirty = false;
uint32_t lastPersistentSaveAt = 0;
float lastPersistedDistanceKm = 0.0f;

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
    .brand{display:flex;align-items:center;gap:12px}.brand-mark{display:block;width:52px;height:52px;flex:none;color:var(--red);filter:drop-shadow(0 0 10px #eb303833)}
    h1{font-size:1.35rem;font-weight:850;letter-spacing:.13em;margin:0}.subtitle,.label,.foot{color:var(--muted);font-size:.76rem;letter-spacing:.08em}.badge{border:1px solid #46474a;border-radius:20px;padding:6px 10px;color:var(--green);font-size:.7rem;letter-spacing:.06em}
    .panel{background:linear-gradient(145deg,#242528,#191a1c);border:1px solid #353639;border-radius:16px;padding:18px;margin:12px 0;box-shadow:0 14px 36px #0004}
    .instrument{position:relative;overflow:hidden;padding:24px 22px 18px;border-color:#464042;background:linear-gradient(130deg,#282527,#171719 58%,#242123)}
    .instrument:before{content:"";position:absolute;left:0;top:18px;bottom:18px;width:3px;background:var(--red);border-radius:4px}
    .hero{display:grid;grid-template-columns:1.08fr 1fr;gap:14px;align-items:center}.gear-block{display:flex;flex-direction:column;align-items:flex-start;gap:8px}.gear-heading{display:flex;align-items:center;gap:9px;min-height:23px}.gear-readout{display:flex;align-items:center;gap:15px}.gear-number{font-size:clamp(6.5rem,27vw,10rem);font-weight:800;line-height:.78;letter-spacing:-.1em;color:#fff;text-shadow:0 0 35px #fff2}
    .gear-label{font-size:.76rem;letter-spacing:.19em;color:var(--muted)}.shift-state{font-size:clamp(6.5rem,27vw,10rem);font-weight:800;line-height:.78;letter-spacing:-.08em}.shift-state.up{color:var(--red);text-shadow:0 0 12px #eb303866}.shift-state.down{color:#65baff;text-shadow:0 0 12px #65baff66}
    .stats{border-left:1px solid #414145;padding-left:20px}.speed{font-size:clamp(2.5rem,11vw,4rem);font-weight:720;line-height:1;letter-spacing:-.06em}.unit{color:var(--muted);font-size:.77rem;letter-spacing:.12em}.rpm-line{display:flex;align-items:baseline;gap:8px;margin-top:14px}.rpm{font-size:1.6rem;font-weight:650;font-variant-numeric:tabular-nums}
    .bar{height:10px;border-radius:8px;background:linear-gradient(90deg,#35bd7a 0 45%,#eac04d 68%,#f18343 85%,#f04e5e);position:relative;margin:16px 0 7px;overflow:hidden}
    .bar:after{content:"";position:absolute;inset:0 auto 0 0;width:var(--rpm);background:#11121466;border-right:2px solid white}
    .ticks{display:flex;justify-content:space-between;color:var(--muted);font-size:.65rem}
    .strip-label{margin-top:17px}.leds{display:flex;gap:8px;align-items:center;margin-top:10px}.led{flex:1;height:12px;border-radius:8px;background:#35363a;box-shadow:inset 0 0 5px #0008;transition:background .12s,box-shadow .12s}
    .led.on:nth-child(-n+4){background:#47db88;box-shadow:0 0 12px #47db8880}.led.on:nth-child(n+5):nth-child(-n+6){background:#ffd45e;box-shadow:0 0 12px #ffd45e80}.led.on:nth-child(n+7){background:#ff5969;box-shadow:0 0 12px #ff596980}
    .memory-note{margin-top:10px;color:var(--muted);font-size:.7rem;letter-spacing:.04em}
    .mode-panel{padding:17px 18px}.mode-heading{display:flex;justify-content:space-between;align-items:center}.current-mode{color:var(--red);font-size:.76rem;font-weight:750;letter-spacing:.12em}
    .modes{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:11px}
    button{border:1px solid #414246;background:#242528;color:#b8b8ba;padding:12px 6px;border-radius:9px;font:inherit;font-size:.8rem;font-weight:750;letter-spacing:.06em;cursor:pointer;transition:background .15s,border-color .15s,color .15s}
    button.active{border-color:var(--red);color:white;background:#492327;box-shadow:inset 0 -2px var(--red)}button:focus-visible{outline:2px solid white;outline-offset:2px}
    .grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.metric{margin:0;background:#191a1c;padding:15px}.value{font-size:1.2rem;font-weight:650;margin-top:5px;font-variant-numeric:tabular-nums}.metric .label{font-size:.66rem}
    .foot{margin:16px 2px 0;letter-spacing:0}.notice{color:#c6c2c2;font-size:.75rem}#toast{min-height:1.2em;color:var(--amber);font-size:.78rem;margin-top:8px}
    @media(max-width:520px){main{padding:16px 12px 30px}.top{margin-bottom:13px}.brand-mark{width:42px;height:42px}.instrument{padding:22px 16px 16px}.gear-block{gap:8px}.gear-number{font-size:clamp(6rem,29vw,8rem)}.stats{padding-left:14px}.grid{grid-template-columns:repeat(2,1fr);gap:8px}.metric{padding:13px}.value{font-size:1.08rem}}
    @media(max-width:360px){.subtitle{font-size:.65rem}.badge{padding:5px 7px;font-size:.62rem}.gear-number{font-size:5.7rem}.speed{font-size:2.4rem}.modes{gap:5px}button{font-size:.7rem}}
  </style>
</head>
<body>
<main>
  <header class="top">
    <div class="brand">
      <svg class="brand-mark" viewBox="0 0 24 24" role="img" aria-label="Toyota logo" xmlns="http://www.w3.org/2000/svg">
        <path fill="currentColor" d="M12 4.236c-6.627 0-12 3.476-12 7.762 0 4.289 5.373 7.766 12 7.766s12-3.476 12-7.766-5.373-7.762-12-7.762zm0 12.196c-.986 0-1.79-1.942-1.84-4.385a21.093 21.093 0 0 0 3.68 0c-.05 2.442-.854 4.385-1.84 4.385zm-1.719-6.324c.268-1.727.937-2.953 1.719-2.953s1.45 1.226 1.719 2.953a19.436 19.436 0 0 1-3.438 0zM12 5.358c-1.287 0-2.385 1.928-2.79 4.619-2.44-.38-4.143-1.248-4.143-2.256 0-1.36 3.104-2.461 6.933-2.461 3.83 0 6.933 1.102 6.933 2.461 0 1.008-1.703 1.876-4.143 2.256-.405-2.69-1.503-4.618-2.79-4.618zm-10.28 6.35c0-1.315.507-2.55 1.388-3.61-.009.074-.015.15-.015.226 0 1.657 2.485 3.07 5.953 3.59-.003.12-.003.242-.003.364 0 3.09.866 5.705 2.063 6.593-5.26-.317-9.385-3.403-9.385-7.163zm11.174 7.163c1.197-.888 2.063-3.504 2.063-6.593 0-.123-.002-.243-.003-.363 3.466-.52 5.953-1.932 5.953-3.591 0-.076-.006-.152-.015-.226.881 1.063 1.387 2.295 1.387 3.61 0 3.76-4.125 6.846-9.385 7.163z"/>
      </svg>
      <div><h1>TOYOTA</h1><div class="subtitle">QUEST 1.6 · SHIFT DISPLAY</div></div>
    </div>
    <span class="badge" id="connection">CONNECTING</span>
  </header>
  <section class="panel instrument">
    <div class="hero">
      <div class="gear-block">
        <div class="gear-heading"><span class="gear-label">GEAR</span></div>
        <div class="gear-readout"><div class="gear-number" id="gear">1</div><span class="shift-state" id="shiftState" aria-live="polite"></span></div>
      </div>
      <div class="stats">
        <div class="speed"><span id="speed">--</span></div><div class="unit">KM/H</div>
        <div class="rpm-line"><span class="rpm" id="rpm">----</span><span class="unit">RPM</span></div>
      </div>
    </div>
    <div class="bar" id="rpmBar" style="--rpm:0%"></div>
    <div class="ticks"><span>0</span><span>2k</span><span>4k</span><span>6k</span><span>7k</span></div>
    <div class="label strip-label">RPM SHIFT LIGHT · SIMULATED LED-BY-LED PREVIEW</div>
    <div class="leds" id="leds" aria-label="Simulated sequential shift light; physical analog strip changes as one unit"><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i><i class="led"></i></div>
    <div class="memory-note" id="memory">ON-BOARD MEMORY · LOADING</div>
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
  <p class="foot notice">SIMULATED DASHBOARD · No ELM327 connection or vehicle control. Trip totals and mode are remembered; live readings are simulated.</p>
  <p class="foot">Wi-Fi: Quest-Shift · Open <b>192.168.4.1</b> · Login: <b>quest</b> / <b>QUEST_ODO</b></p>
</main>
<script>
const $=id=>document.getElementById(id);
const buttons=[...document.querySelectorAll('[data-mode]')];
let updateInProgress=false;
async function update(){
  if(updateInProgress)return;
  updateInProgress=true;
  const controller=new AbortController();
  const timeout=setTimeout(()=>controller.abort(),3000);
  try{
    const response=await fetch('/api',{cache:'no-store',signal:controller.signal});
    if(!response.ok)throw new Error(response.status===401?'AUTH REQUIRED':'API ERROR');
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
    shiftState.textContent=d.shift==='up'?'↑':d.shift==='down'?'↓':'';
    shiftState.classList.toggle('up',d.shift==='up');
    shiftState.classList.toggle('down',d.shift==='down');
    shiftState.setAttribute('aria-label',d.shift==='up'?'Shift up':d.shift==='down'?'Shift down':'No shift cue');
    [...$('leds').children].forEach((led,index)=>led.classList.toggle('on',index<d.leds));
    $('memory').textContent='ON-BOARD MEMORY · '+d.samples+' SHIFT SAMPLES · ↑ '+d.upshifts+' / ↓ '+d.downshifts;
    buttons.forEach(button=>button.classList.toggle('active',button.dataset.mode===d.mode));
    document.title='Gear '+d.gear+' · Toyota Quest';
  }catch(error){
    $('connection').textContent=error.name==='AbortError'?'API TIMEOUT':error.message==='AUTH REQUIRED'?'AUTH REQUIRED':'OFFLINE';
  }finally{
    clearTimeout(timeout);
    updateInProgress=false;
  }
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
  if (simulatedGear < 6 && engineRpm >= shiftRpm()) return ShiftCue::UP;
  if (simulatedGear > 1 && engineRpm <= 1600.0f) return ShiftCue::DOWN;
  return ShiftCue::NONE;
}

struct ShiftLightColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

ShiftLightColor shiftLightColor(uint32_t now) {
  const float progress = min(1.0f, engineRpm / shiftRpm());
  ShiftLightColor color = {0, 0, 0};

  if (progress < 0.5f) {
    color.green = static_cast<uint8_t>(35.0f + progress * 440.0f);
  } else if (progress < 0.8f) {
    color.red = static_cast<uint8_t>((progress - 0.5f) / 0.3f * 255.0f);
    color.green = 255;
  } else {
    color.red = 255;
    color.green = static_cast<uint8_t>((1.0f - progress) / 0.2f * 255.0f);
  }

  const ShiftCue cue = shiftCue();
  if (cue == ShiftCue::UP && ((now / 180) & 1)) {
    color.red = 0;
    color.green = 0;
  } else if (cue == ShiftCue::DOWN) {
    color.red = 0;
    color.green = 0;
    color.blue = ((now / 300) & 1) ? 96 : 0;
  }
  return color;
}

static_assert(sizeof(PersistentState) <= 512,
              "Persistent shift memory must fit in the ESP8266 EEPROM emulation area");

bool savePersistentState(bool force) {
  if (!persistentStorageReady) return false;
  if (!force && millis() - lastPersistentSaveAt < PERSIST_INTERVAL_MS) return true;
  if (!persistentStateDirty && !force) return true;

  persistentState.magic = PERSIST_MAGIC;
  persistentState.version = PERSIST_VERSION;
  persistentState.mode = static_cast<uint8_t>(driveMode);
  persistentState.tripDistanceKm = sessionDistanceKm;
  persistentState.fuelUsedLitres = fuelUsedLitres;
  EEPROM.put(0, persistentState);
  if (!EEPROM.commit()) {
    Serial.println(F("ERROR: Could not save shift memory to flash"));
    return false;
  }

  lastPersistentSaveAt = millis();
  lastPersistedDistanceKm = sessionDistanceKm;
  persistentStateDirty = false;
  return true;
}

void loadPersistentState() {
  EEPROM.begin(sizeof(PersistentState));

  PersistentState loaded{};
  EEPROM.get(0, loaded);
  const bool valid = loaded.magic == PERSIST_MAGIC &&
                     loaded.version == PERSIST_VERSION &&
                     loaded.mode <= static_cast<uint8_t>(DriveMode::PERFORMANCE) &&
                     loaded.sampleCount <= SHIFT_SAMPLE_CAPACITY &&
                     loaded.sampleHead < SHIFT_SAMPLE_CAPACITY &&
                     isfinite(loaded.tripDistanceKm) &&
                     loaded.tripDistanceKm >= 0.0f &&
                     loaded.tripDistanceKm < 100000000.0f &&
                     isfinite(loaded.fuelUsedLitres) &&
                     loaded.fuelUsedLitres >= 0.0f &&
                     loaded.fuelUsedLitres < 100000000.0f;
  if (valid) {
    persistentState = loaded;
  } else {
    persistentState = {};
    persistentState.magic = PERSIST_MAGIC;
    persistentState.version = PERSIST_VERSION;
    persistentState.mode = static_cast<uint8_t>(DriveMode::NORMAL);
  }

  driveMode = static_cast<DriveMode>(persistentState.mode);
  sessionDistanceKm = persistentState.tripDistanceKm;
  fuelUsedLitres = persistentState.fuelUsedLitres;
  if (sessionDistanceKm > 0.001f) {
    averageFuelLPer100Km = fuelUsedLitres / sessionDistanceKm * 100.0f;
  }

  persistentStorageReady = true;
  lastPersistentSaveAt = millis();
  lastPersistedDistanceKm = sessionDistanceKm;
  if (!valid) {
    persistentStateDirty = true;
    savePersistentState(true);
    Serial.println(F("Shift memory initialized"));
  } else {
    Serial.println(F("Shift memory restored from flash"));
  }
}

void rememberShiftDecision(ShiftCue cue) {
  if (cue == ShiftCue::NONE || cue == previousShiftCue) return;
  previousShiftCue = cue;
  if (cue == ShiftCue::UP && simulatedGear >= 6) return;
  if (cue == ShiftCue::DOWN && simulatedGear <= 1) return;

  ShiftSample &sample = persistentState.samples[persistentState.sampleHead];
  sample.rpm = static_cast<uint16_t>(engineRpm);
  sample.speedTenthsKmh = static_cast<uint16_t>(vehicleSpeedKmh * 10.0f);
  sample.throttlePercent = static_cast<uint8_t>(throttlePercent);
  sample.gear = simulatedGear;
  sample.mode = static_cast<uint8_t>(driveMode);
  sample.cue = static_cast<uint8_t>(cue);

  persistentState.sampleHead =
      (persistentState.sampleHead + 1) % SHIFT_SAMPLE_CAPACITY;
  if (persistentState.sampleCount < SHIFT_SAMPLE_CAPACITY) {
    ++persistentState.sampleCount;
  }
  if (cue == ShiftCue::UP) {
    ++persistentState.upshiftEvents;
  } else {
    ++persistentState.downshiftEvents;
  }
  persistentStateDirty = true;
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
  char json[448];
  const ShiftCue cue = shiftCue();
  const char *cueName = cue == ShiftCue::UP ? "up" :
                        cue == ShiftCue::DOWN ? "down" : "none";
  const ShiftLightColor color = shiftLightColor(millis());
  const float ledProgress = engineRpm / shiftRpm() * 8.0f;
  const uint8_t litLeds = ledProgress >= 8.0f
                              ? 8
                              : static_cast<uint8_t>(ledProgress);
  snprintf(json, sizeof(json),
           "{\"mode\":\"%s\",\"gear\":%u,\"rpm\":%.0f,\"speed\":%.1f,"
           "\"averageFuel\":%.2f,\"instantFuel\":%.2f,"
           "\"trip\":%.3f,\"coolant\":%.1f,\"red\":%u,\"green\":%u,"
           "\"blue\":%u,\"leds\":%u,\"shift\":\"%s\",\"samples\":%u,"
           "\"upshifts\":%lu,\"downshifts\":%lu}",
           modeSlug(), simulatedGear, engineRpm, vehicleSpeedKmh,
           averageFuelLPer100Km, instantFuelLPer100Km, sessionDistanceKm,
           coolantTempC, color.red, color.green, color.blue, litLeds, cueName,
           persistentState.sampleCount,
           static_cast<unsigned long>(persistentState.upshiftEvents),
           static_cast<unsigned long>(persistentState.downshiftEvents));
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

  persistentStateDirty = true;
  if (!savePersistentState(true)) {
    server.send(500, "text/plain", "Mode changed, but could not save it to flash");
    return;
  }
  server.send(200, "text/plain", modeName());
}

void drawToyotaLogo(uint8_t centerX, uint8_t centerY) {
  // Keep the full emblem inside the OLED's yellow top 16-pixel band.
  u8g2.drawEllipse(centerX, centerY, 14, 6);
  u8g2.drawEllipse(centerX, centerY, 4, 5);
  u8g2.drawEllipse(centerX, centerY, 10, 2);
}

void drawBootScreen() {
  u8g2.clearBuffer();
  drawToyotaLogo(64, 8);
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(22, 35, "TOYOTA");
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(30, 45, "QUEST 1.6L");
  u8g2.drawStr(29, 54, "SHIFT DISPLAY");
  u8g2.drawFrame(15, 57, 98, 6);
  u8g2.drawBox(17, 59, 94, 2);
  u8g2.sendBuffer();
}

void drawDashboard() {
  char text[24];
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(2, 8, "TOYOTA");
  u8g2.drawStr(38, 8, "QUEST 1.6L");
  u8g2.drawStr(90, 8, modeName());
  u8g2.drawHLine(0, 15, 128);

  u8g2.drawStr(3, 24, "GEAR");
  const ShiftCue cue = shiftCue();
  snprintf(text, sizeof(text), "%u", simulatedGear);
  u8g2.setFont(u8g2_font_logisoso32_tn);
  u8g2.drawStr(2, 60, text);
  const int16_t arrowX = 32;
  if (cue == ShiftCue::UP) {
    u8g2.drawLine(arrowX, 57, arrowX, 35);
    u8g2.drawTriangle(arrowX, 27, arrowX - 9, 38, arrowX + 9, 38);
  } else if (cue == ShiftCue::DOWN) {
    u8g2.drawLine(arrowX, 34, arrowX, 54);
    u8g2.drawTriangle(arrowX, 61, arrowX - 9, 50, arrowX + 9, 50);
  }

  u8g2.setFont(u8g2_font_5x8_tf);
  snprintf(text, sizeof(text), "%.0f", vehicleSpeedKmh);
  u8g2.setFont(u8g2_font_logisoso18_tn);
  u8g2.drawStr(49, 39, text);
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(101, 39, "km/h");
  snprintf(text, sizeof(text), "RPM %.0f", engineRpm);
  u8g2.drawStr(51, 48, text);
  snprintf(text, sizeof(text), "AVG %.1f L/100", averageFuelLPer100Km);
  u8g2.drawStr(51, 57, text);

  const float barProgress = engineRpm / 7000.0f * 9;
  const uint8_t bars = barProgress >= 9 ? 9 : static_cast<uint8_t>(barProgress);
  for (uint8_t i = 0; i < 9; ++i) {
    const int16_t x = 53 + i * 8;
    if (i < bars) {
      u8g2.drawBox(x, 61, 6, 3);
    } else {
      u8g2.drawFrame(x, 61, 6, 3);
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
  } else if (vehicleSpeedKmh < 65.0f) {
    simulatedGear = 4;
  } else if (vehicleSpeedKmh < 82.0f) {
    simulatedGear = 5;
  } else {
    simulatedGear = 6;
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
    if (sessionDistanceKm > lastPersistedDistanceKm) {
      persistentStateDirty = true;
    }
    lastSampleAt = now;
  }

  rememberShiftDecision(shiftCue());
  savePersistentState(false);
}

void updateShiftLeds(uint32_t now) {
  const ShiftLightColor color = shiftLightColor(now);
  analogWrite(LED_RED_PIN, color.red);
  analogWrite(LED_GREEN_PIN, color.green);
  analogWrite(LED_BLUE_PIN, color.blue);
}

void setup() {
  Serial.begin(115200);
  loadPersistentState();
  u8g2.begin();
  drawBootScreen();

  analogWriteRange(255);
  analogWriteFreq(1000);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  analogWrite(LED_RED_PIN, 0);
  analogWrite(LED_GREEN_PIN, 0);
  analogWrite(LED_BLUE_PIN, 0);

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
