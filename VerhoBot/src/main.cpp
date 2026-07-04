#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <esp_sleep.h>
#include <ArduinoJson.h>

#include "pins.h"

// ============================================================
//  RTC memory – survives deep sleep
// ============================================================
RTC_DATA_ATTR float rtcCurtainPos = 0.0f;
RTC_DATA_ATTR uint32_t rtcBootCount = 0;

// ============================================================
//  Preferences (non‑volatile storage)
// ============================================================
Preferences prefs;

// ============================================================
//  Global state
// ============================================================
enum CurtainState { IDLE, OPENING, CLOSING };
CurtainState state = IDLE;

uint32_t moveStart    = 0;
uint32_t moveDuration = 0;
float    curtainPos   = 0.0f;   // 0.0 = fully open, 1.0 = fully closed

// Configuration loaded from Preferences
String wifiSSID = "";
String wifiPass = "";
int    openHour = 7;
int    openMin  = 0;
int    closeHour = 22;
int    closeMin  = 0;
String tzString  = TIMEZONE_STRING;
bool   configured = false;

// Web server
WebServer server(80);

// Awake timeout (STA mode only)
uint32_t awakeStart = 0;
const uint32_t AWAKE_TIMEOUT = 60000;  // 60 seconds after motor stops

// ============================================================
//  Motor control (non‑blocking, PWM soft start / soft stop)
// ============================================================
// PWMA is driven via LEDC instead of digitalWrite so we can ramp the duty
// cycle. Direction pins (AIN1/AIN2) stay as plain digital outputs.

void motorSetDuty(uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PWMA, duty);           // core 3.x: pin-addressed LEDC
#else
    ledcWrite(PWM_CHANNEL, duty);    // core <3.0: channel-addressed LEDC
#endif
}

void motorOpen() {
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);
    motorSetDuty(0); // ramp handled by updateMotor()
}

void motorClose() {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
    motorSetDuty(0); // ramp handled by updateMotor()
}

void motorStop() {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
    motorSetDuty(0);
}

void startOpen(uint32_t ms) {
    if (curtainPos <= 0.01f) return;
    motorOpen();
    state = OPENING;
    moveStart = millis();
    moveDuration = ms;
    awakeStart = millis(); // keep awake while moving
}

void startClose(uint32_t ms) {
    if (curtainPos >= 0.99f) return;
    motorClose();
    state = CLOSING;
    moveStart = millis();
    moveDuration = ms;
    awakeStart = millis(); // keep awake while moving
}

void stopMotor() {
    if (state != IDLE) {
        float fraction = (float)(millis() - moveStart) / (float)moveDuration;
        fraction = constrain(fraction, 0.0f, 1.0f);
        if (state == OPENING) curtainPos = max(curtainPos - fraction, 0.0f);
        if (state == CLOSING) curtainPos = min(curtainPos + fraction, 1.0f);
    }
    motorStop();
    state = IDLE;
    awakeStart = millis(); // start the 60‑second countdown
}

// Linearly ramps duty from `from` to `to` over [0, rampMs] given `elapsed`.
static uint8_t rampDuty(uint32_t elapsed, uint32_t rampMs, uint8_t from, uint8_t to) {
    if (rampMs == 0 || elapsed >= rampMs) return to;
    float t = (float)elapsed / (float)rampMs;
    return (uint8_t)(from + t * ((float)to - (float)from));
}

void updateMotor() {
    if (state == IDLE) return;

    uint32_t elapsed = millis() - moveStart;

    if (elapsed >= moveDuration || elapsed >= MAX_RUN_TIME) {
        stopMotor();
        return;
    }

    uint32_t remaining = moveDuration - elapsed;

    uint8_t duty;
    if (elapsed < MOTOR_RAMP_UP_MS) {
        // Soft start: 0 -> full speed
        duty = rampDuty(elapsed, MOTOR_RAMP_UP_MS, 0, PWM_MAX_DUTY);
    } else if (remaining < MOTOR_RAMP_DOWN_MS) {
        // Soft stop: full speed -> min duty (stopMotor() cuts the rest cleanly)
        uint32_t decelElapsed = MOTOR_RAMP_DOWN_MS - remaining;
        duty = rampDuty(decelElapsed, MOTOR_RAMP_DOWN_MS, PWM_MAX_DUTY, MOTOR_MIN_DUTY);
    } else {
        duty = PWM_MAX_DUTY;
    }

    motorSetDuty(duty);
}

// Drive toward an absolute position (0.0 = fully open, 1.0 = fully closed),
// scaling the move duration to the remaining distance so the dashboard's
// drag-to-set slider lands close to where the user dropped it.
void gotoPosition(float targetPos) {
    targetPos = constrain(targetPos, 0.0f, 1.0f);
    float diff = targetPos - curtainPos;
    if (fabs(diff) < 0.01f) return; // already there
    uint32_t ms = (uint32_t)(fabs(diff) * CURTAIN_TRAVEL_TIME);
    if (ms < 80) return; // too small a move to bother with
    if (diff > 0) startClose(ms);
    else          startOpen(ms);
}

// ============================================================
//  Battery monitoring
// ============================================================
float readBatteryVoltage() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ADC_SAMPLES; i++) sum += analogRead(BATTERY_ADC);
    float adcVoltage = ((float)(sum / ADC_SAMPLES) / 4095.0f) * 3.3f;
    return adcVoltage * BATTERY_CAL;
}

// ============================================================
//  Preferences (save / load / clear)
// ============================================================
void saveConfig(String ssid, String pass, int oh, int om, int ch, int cm, String tz) {
    prefs.begin("verhobot", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.putInt("openH", oh);
    prefs.putInt("openM", om);
    prefs.putInt("closeH", ch);
    prefs.putInt("closeM", cm);
    prefs.putString("tz", tz);
    prefs.putBool("configured", true);
    prefs.end();
}

void loadConfig() {
    prefs.begin("verhobot", true);
    wifiSSID   = prefs.getString("ssid", "");
    wifiPass   = prefs.getString("pass", "");
    openHour   = prefs.getInt("openH", 7);
    openMin    = prefs.getInt("openM", 0);
    closeHour  = prefs.getInt("closeH", 22);
    closeMin   = prefs.getInt("closeM", 0);
    tzString   = prefs.getString("tz", TIMEZONE_STRING);
    configured = prefs.getBool("configured", false);
    prefs.end();
}

void clearConfig() {
    prefs.begin("verhobot", false);
    prefs.clear();
    prefs.end();
    configured = false;
}

// ============================================================
//  Time / NTP sync (with timeout)
// ============================================================
bool syncTime() {
    if (WiFi.status() != WL_CONNECTED) return false;
    // Use a POSIX TZ string (configurable, see /setup) instead of raw UTC
    // offsets so DST is handled automatically. Previously this called
    // configTime(0, 0, ...), which left the RTC in UTC and made scheduled
    // open/close times silently off by 2-3 hours in Finland.
    configTzTime(tzString.c_str(), "pool.ntp.org", "time.google.com");
    time_t now = time(nullptr);
    int attempts = 0;
    while (now < 8 * 3600 * 2 && attempts < 30) {
        delay(250);
        now = time(nullptr);
        attempts++;
    }
    return (now > 8 * 3600);
}

String getTimeString() {
    time_t now = time(nullptr);
    struct tm *info = localtime(&now);
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", info);
    return String(buf);
}

// ============================================================
//  Deep sleep preparation
// ============================================================
struct NextEvent {
    bool     isOpenEvent; // true = the pending action is OPEN, false = CLOSE
    int      hour;
    int      minute;
    uint64_t secondsUntil;
};

NextEvent computeNextEvent() {
    NextEvent ev;
    time_t now = time(nullptr);
    struct tm *tm_now = localtime(&now);

    // BUGFIX: this used to pick the SAME edge the curtain just reached
    // (closed -> target closeHour again), which meant a device that fell
    // asleep closed would sleep straight through the next openHour and
    // only ever wake up at closeHour, 24h later. It must target the
    // *opposite* edge - the one that hasn't happened yet.
    if (curtainPos > 0.5f) {
        // currently closed -> next thing to do is OPEN
        ev.isOpenEvent = true;
        ev.hour = openHour;
        ev.minute = openMin;
    } else {
        // currently open -> next thing to do is CLOSE
        ev.isOpenEvent = false;
        ev.hour = closeHour;
        ev.minute = closeMin;
    }

    struct tm target = *tm_now;
    target.tm_hour = ev.hour;
    target.tm_min  = ev.minute;
    target.tm_sec  = 0;

    time_t targetTime = mktime(&target);
    if (targetTime <= now) targetTime += 86400;

    ev.secondsUntil = (uint64_t)(targetTime - now);
    return ev;
}

uint64_t calculateSleepSeconds() {
    return computeNextEvent().secondsUntil;
}

bool inFallbackAP = false; // true only when AP is up because STA failed post-config (see below)

void enterDeepSleep() {
    Serial.println("Entering deep sleep...");
    delay(100);

    rtcCurtainPos = curtainPos;

    motorStop();
    digitalWrite(STBY, LOW);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    bool timeSynced = (time(nullptr) > 8 * 3600);
    uint64_t sleepSec;
    if (timeSynced) {
        sleepSec = computeNextEvent().secondsUntil;
    } else {
        // Clock isn't trustworthy (WiFi/NTP failed this cycle) - computing a
        // schedule-based sleep duration from an unsynced clock (effectively
        // Jan 1 1970) could sleep for a wildly wrong number of hours. Retry
        // again shortly instead of trusting it.
        sleepSec = UNSYNCED_RETRY_SEC;
        Serial.println("Clock not synced - using a short fixed retry interval instead of the schedule.");
    }

    esp_sleep_enable_timer_wakeup(sleepSec * 1000000ULL);

    // The ESP32-C3 is RISC-V and has no dedicated RTC GPIO mux, so it
    // doesn't support ext0/ext1 wakeup sources the way the original ESP32,
    // S2, and S3 do. It uses a separate GPIO wakeup API instead.
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
    esp_sleep_enable_ext1_wakeup(1ULL << WAKE_BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
#else
    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif

    if (timeSynced) {
        Serial.printf("Sleeping for %llu seconds (schedule-based)\n", sleepSec);
    } else {
        Serial.printf("Sleeping for %llu seconds (retry, clock unsynced)\n", sleepSec);
    }
    Serial.flush();

    esp_deep_sleep_start();
}

// ============================================================
//  Web Server – Full Dashboard + Setup
// ============================================================

// ---------- DASHBOARD PAGE (with ghost animation) ----------
const char PAGE_DASHBOARD[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VerhoBot</title>
<style>
:root{
  --bg:#f5f4f0;--surface:#faf9f6;
  --ink:#1a1a1a;--ink2:#3a3a3a;
  --mute:#8a8a85;--rule:#c8c7c0;--rule2:#dddcd6;
  --green:#1a6b3a;--red:#8b1a1a;--amber:#8a5a00;
  --mono:'Courier New',Courier,'Lucida Console',monospace;
  --sans:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
  /* Accent + sky follow the curtain state: warm dawn when open (daylight
     coming in), cool dusk when closed (night). Set by JS via body attrs. */
  --accent:#c2560c;--accent-dim:rgba(194,86,12,0.10);--accent-border:rgba(194,86,12,0.35);
  --sky-a:#f3e2c8;--sky-b:#f5f4f0;
}
body[data-phase="night"]{
  --accent:#38477a;--accent-dim:rgba(56,71,122,0.12);--accent-border:rgba(56,71,122,0.4);
  --sky-a:#20264a;--sky-b:#3a4472;
}
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0;}
body{
  background:var(--bg);color:var(--ink);font-family:var(--sans);
  min-height:100vh;padding:1.5rem 1rem 3rem;
  background-image:
    linear-gradient(var(--rule2) 1px,transparent 1px),
    linear-gradient(90deg,var(--rule2) 1px,transparent 1px);
  background-size:24px 24px;
  transition:background .6s ease;
}
.wrap{max-width:480px;margin:0 auto;}

header{
  display:flex;justify-content:space-between;align-items:flex-start;
  border-bottom:2px solid var(--ink);padding-bottom:10px;margin-bottom:1rem;
  position:relative;
}
header::after{
  content:'';position:absolute;bottom:-2px;left:0;width:48px;height:2px;
  background:var(--accent);transition:background .6s ease;
}
.logo{
  font-family:var(--mono);font-size:1.5rem;letter-spacing:.1em;
  display:flex;align-items:baseline;gap:0;line-height:1;
}
.logo-verho{color:var(--ink);font-weight:normal;}
.logo-slash{color:var(--accent);margin:0 1px;transition:color .6s ease;}
.logo-bot{color:var(--accent);font-weight:bold;letter-spacing:.06em;transition:color .6s ease;}
.logo-sub{font-family:var(--mono);font-size:.5rem;letter-spacing:.2em;color:var(--mute);margin-top:5px;text-transform:uppercase;}
.hdr-right{text-align:right;}
.conn{font-family:var(--mono);font-size:.52rem;letter-spacing:.15em;text-transform:uppercase;
  display:flex;align-items:center;gap:5px;color:var(--green);justify-content:flex-end;}
.conn-dot{width:6px;height:6px;border-radius:50%;background:var(--green);animation:blink 2.5s steps(1) infinite;}
#conn-offline{color:var(--red);display:none;}
#conn-offline .conn-dot{background:var(--red);animation:none;}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0}}
.mode-badge{font-family:var(--mono);font-size:.44rem;letter-spacing:.12em;color:var(--mute);margin-top:3px;text-transform:uppercase;}
.ip{font-family:var(--mono);font-size:.46rem;letter-spacing:.1em;color:var(--mute);margin-top:2px;}
.gear{display:inline-block;font-size:1.1rem;cursor:pointer;text-decoration:none;color:var(--mute);transition:transform 0.3s;margin-top:4px;}
.gear:hover{transform:rotate(60deg);color:var(--accent);}

.banner{
  font-family:var(--mono);font-size:.56rem;letter-spacing:.05em;
  padding:8px 10px;border:1px solid var(--amber);color:var(--amber);
  background:rgba(138,90,0,.06);margin-bottom:1rem;display:none;
  align-items:center;gap:8px;
}
.banner.show{display:flex;}
.banner.danger{border-color:var(--red);color:var(--red);background:rgba(139,26,26,.06);}
.banner-dot{width:6px;height:6px;flex:0 0 auto;border-radius:50%;background:currentColor;}

.section{
  font-family:var(--mono);font-size:.52rem;letter-spacing:.2em;text-transform:uppercase;
  color:var(--accent);margin-bottom:8px;display:flex;align-items:center;gap:8px;
  transition:color .6s ease;
}
.section::after{content:'';flex:1;height:1px;background:var(--rule);}

.curtain-block{border:1px solid var(--ink2);margin-bottom:1.25rem;}
.curtain-head{
  background:var(--ink);color:#f5f4f0;
  font-family:var(--mono);font-size:.58rem;letter-spacing:.18em;
  padding:5px 10px;display:flex;justify-content:space-between;
}
.curtain-head span:last-child{color:var(--accent);transition:color .6s ease;}
.curtain-vis{
  position:relative;height:96px;overflow:hidden;cursor:grab;
  background:linear-gradient(180deg,var(--sky-a),var(--sky-b));
  transition:background 1s ease;
  touch-action:none;
}
.curtain-vis:active{cursor:grabbing;}
.curtain-vis::before{content:'';position:absolute;top:0;left:0;right:0;height:3px;background:var(--ink);z-index:4;}
.drape{
  position:absolute;top:3px;left:0;height:calc(100% - 3px);
  background:var(--accent-dim);
  border-right:1.5px solid var(--accent-border);
  overflow:hidden;z-index:1;
  transition:background .6s ease,border-color .6s ease;
}
.drape::after{
  content:'';position:absolute;inset:0;
  background-image:repeating-linear-gradient(45deg,transparent,transparent 4px,rgba(0,0,0,.05) 4px,rgba(0,0,0,.05) 5px);
}
.drape-ghost{
  position:absolute;top:3px;left:0;height:calc(100% - 3px);
  background:transparent;border-right:2px dashed var(--accent-border);
  pointer-events:none;z-index:2;display:none;
}
.ring{position:absolute;top:-2px;width:7px;height:7px;border:1.5px solid var(--ink);border-radius:50%;background:var(--bg);transform:translateX(-50%);z-index:3;pointer-events:none;}
.handle{
  position:absolute;top:50%;width:16px;height:16px;border-radius:50%;
  background:#fff;border:2px solid var(--ink);transform:translate(-50%,-50%);
  z-index:5;box-shadow:0 1px 3px rgba(0,0,0,.3);
}
.curtain-foot{border-top:1px solid var(--rule);padding:6px 10px;display:flex;justify-content:space-between;align-items:center;}
.pos-scale{font-family:var(--mono);font-size:.46rem;letter-spacing:.05em;color:var(--mute);}
.pos-right{display:flex;align-items:center;gap:10px;}
.state-badge{
  font-family:var(--mono);font-size:.48rem;letter-spacing:.15em;text-transform:uppercase;
  padding:2px 7px;border:1px solid var(--ink2);color:var(--ink2);
}
.state-badge.opening,.state-badge.closing{border-color:var(--accent);color:var(--accent);}
.pos-pct{font-family:var(--mono);font-size:1rem;font-weight:700;color:var(--accent);transition:color .6s ease;}

.metrics{display:grid;grid-template-columns:1fr 1fr 1fr;border:1px solid var(--ink2);margin-bottom:1.25rem;}
.metric{padding:.75rem .9rem;border-right:1px solid var(--rule);}
.metric:last-child{border-right:none;}
.m-label{font-family:var(--mono);font-size:.46rem;letter-spacing:.18em;text-transform:uppercase;color:var(--mute);margin-bottom:5px;}
.m-val{font-family:var(--mono);font-size:.92rem;font-weight:700;line-height:1;}
.m-unit{font-family:var(--mono);font-size:.48rem;color:var(--mute);margin-left:1px;}
.bbar-row{margin-top:5px;display:flex;align-items:center;gap:5px;}
.bbar-track{flex:1;height:3px;background:var(--rule2);}
.bbar-fill{height:100%;background:var(--ink);transition:width .6s ease,background .3s ease;}

.sched-block{border:1px solid var(--ink2);margin-bottom:1.25rem;padding:.8rem .9rem;}
.sched-row{display:flex;justify-content:space-between;align-items:baseline;}
.sched-label{font-family:var(--mono);font-size:.46rem;letter-spacing:.16em;text-transform:uppercase;color:var(--mute);}
.sched-times{font-family:var(--mono);font-size:.6rem;color:var(--ink2);margin-top:4px;}
.sched-next{font-family:var(--mono);font-size:.78rem;font-weight:700;color:var(--accent);margin-top:2px;transition:color .6s ease;}
.sched-note{font-family:var(--mono);font-size:.46rem;color:var(--mute);margin-top:6px;}

.controls{display:grid;grid-template-columns:1fr 1fr 1fr;border:1px solid var(--ink2);margin-bottom:.6rem;}
.cmd{
  all:unset;cursor:pointer;
  font-family:var(--mono);font-size:.58rem;letter-spacing:.15em;text-transform:uppercase;
  text-align:center;color:var(--ink2);
  padding:.85rem .4rem;
  display:flex;flex-direction:column;align-items:center;gap:7px;
  border-right:1px solid var(--rule);
  transition:background .12s,color .12s,border-color .12s,opacity .2s;
  position:relative;
}
.cmd:last-child{border-right:none;}
.cmd:active{transform:scale(.98);}
.cmd[aria-disabled="true"]{opacity:.35;pointer-events:none;}
.cmd svg{width:22px;height:22px;stroke:var(--ink2);fill:none;stroke-width:1.5;stroke-linecap:round;stroke-linejoin:round;transition:stroke .12s;}
.cmd.open:hover,.cmd.close:hover{background:var(--accent);color:#fff;}
.cmd.open:hover svg,.cmd.close:hover svg{stroke:#fff;}
.cmd.stop:hover{background:var(--ink);color:#f5f4f0;}
.cmd.stop:hover svg{stroke:#f5f4f0;}

.calib-row{display:flex;justify-content:center;gap:1.5rem;margin-bottom:1.25rem;}
.calib-btn{
  all:unset;cursor:pointer;font-family:var(--mono);font-size:.46rem;letter-spacing:.1em;
  text-transform:uppercase;color:var(--mute);border-bottom:1px dotted var(--mute);
  padding-bottom:1px;
}
.calib-btn:hover{color:var(--accent);border-color:var(--accent);}

.log-block{border:1px solid var(--rule);margin-bottom:1.25rem;}
.log-head{font-family:var(--mono);font-size:.52rem;letter-spacing:.18em;text-transform:uppercase;color:var(--mute);padding:5px 10px;border-bottom:1px solid var(--rule);display:flex;justify-content:space-between;}
#log{padding:8px 10px;font-family:var(--mono);font-size:.58rem;color:var(--mute);line-height:2;min-height:80px;max-height:180px;overflow-y:auto;}
.lt{color:var(--rule);margin-right:8px;}
.ok{color:var(--green);}
.er{color:var(--red);}

footer{display:flex;justify-content:space-between;padding-top:10px;border-top:1px solid var(--rule);font-family:var(--mono);font-size:.46rem;letter-spacing:.12em;text-transform:uppercase;color:var(--mute);}

.toast-wrap{position:fixed;left:0;right:0;bottom:14px;display:flex;flex-direction:column;align-items:center;gap:6px;z-index:50;pointer-events:none;}
.toast{
  font-family:var(--mono);font-size:.56rem;letter-spacing:.05em;
  background:var(--ink);color:#f5f4f0;padding:7px 14px;
  border-left:3px solid var(--accent);opacity:0;transform:translateY(6px);
  transition:opacity .25s,transform .25s;max-width:90vw;
}
.toast.er{border-left-color:var(--red);}
.toast.show{opacity:1;transform:translateY(0);}

@media (prefers-reduced-motion:reduce){
  *{animation-duration:.001ms !important;transition-duration:.001ms !important;}
}
</style>
</head>
<body id="body">
<div class="wrap">

<header>
  <div>
    <div class="logo"><span class="logo-verho">VERHO</span><span class="logo-slash">/</span><span class="logo-bot">BOT</span></div>
    <div class="logo-sub">ESP32 &middot; TB6612 &middot; WiFi</div>
  </div>
  <div class="hdr-right">
    <div class="conn" id="conn-online"><span>online</span><div class="conn-dot"></div></div>
    <div class="conn" id="conn-offline"><span>offline</span><div class="conn-dot"></div></div>
    <div class="mode-badge" id="modeBadge">--</div>
    <div class="ip" id="ipDisplay">--</div>
    <a href="/setup" class="gear" title="Settings">&#9881;</a>
  </div>
</header>

<div class="banner" id="banner-clock">
  <div class="banner-dot"></div>
  <span>Clock not synced — schedule is paused until WiFi/NTP succeeds.</span>
</div>
<div class="banner danger" id="banner-batt">
  <div class="banner-dot"></div>
  <span>Battery low — open/close may be unreliable. Charge soon.</span>
</div>

<div class="section">curtain</div>
<div class="curtain-block">
  <div class="curtain-head">
    <span>POSITION &middot; DRAG TO SET</span>
    <span id="pct-head">0%</span>
  </div>
  <div class="curtain-vis" id="cvis">
    <div class="drape" id="drape" style="width:0%"></div>
    <div class="drape-ghost" id="drape-ghost" style="width:0%"></div>
    <div class="handle" id="handle" style="left:0%"></div>
  </div>
  <div class="curtain-foot">
    <span class="pos-scale">OPEN &larr;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&rarr; CLOSED</span>
    <div class="pos-right">
      <span class="state-badge" id="sbadge">IDLE</span>
      <span class="pos-pct" id="posval">0%</span>
    </div>
  </div>
</div>

<div class="section">telemetry</div>
<div class="metrics">
  <div class="metric">
    <div class="m-label">Battery</div>
    <div class="m-val"><span id="bv">--</span><span class="m-unit">V</span></div>
    <div class="bbar-row">
      <div class="bbar-track"><div class="bbar-fill" id="bfill" style="width:0%"></div></div>
      <span style="font-family:var(--mono);font-size:.44rem;color:var(--mute)" id="bpct">--%</span>
    </div>
  </div>
  <div class="metric">
    <div class="m-label">State</div>
    <div class="m-val" id="stval" style="font-size:.75rem;margin-top:3px">--</div>
  </div>
  <div class="metric">
    <div class="m-label" id="thirdLabel">Uptime</div>
    <div class="m-val" style="font-size:.78rem;margin-top:2px" id="upval">--:--</div>
  </div>
</div>

<div class="section">schedule</div>
<div class="sched-block">
  <div class="sched-row">
    <div>
      <div class="sched-label">Next action</div>
      <div class="sched-next" id="nextEvent">--</div>
    </div>
    <div style="text-align:right">
      <div class="sched-label">Open / Close</div>
      <div class="sched-times" id="schedTimes">--:-- / --:--</div>
    </div>
  </div>
  <div class="sched-note" id="schedNote"></div>
</div>

<div class="section">control</div>
<div class="controls">
  <button class="cmd open" id="btnOpen">
    <svg viewBox="0 0 24 24"><polyline points="19 12 13 6 13 18"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
    open
  </button>
  <button class="cmd stop" id="btnStop">
    <svg viewBox="0 0 24 24"><rect x="7" y="7" width="10" height="10"/></svg>
    stop
  </button>
  <button class="cmd close" id="btnClose">
    <svg viewBox="0 0 24 24"><polyline points="5 12 11 6 11 18"/><line x1="19" y1="12" x2="5" y2="12"/></svg>
    close
  </button>
</div>
<div class="calib-row">
  <button class="calib-btn" id="calOpen">set as fully open</button>
  <button class="calib-btn" id="calClose">set as fully closed</button>
</div>

<div class="section">log</div>
<div class="log-block">
  <div class="log-head">
    <span>event log</span>
    <span id="lcnt">0 events</span>
  </div>
  <div id="log"><span style="color:var(--rule2)">// awaiting events</span></div>
</div>

<footer>
  <span>verhobot v0.3</span>
  <span id="ts">--:--:--</span>
  <span>finland</span>
</footer>
</div>

<div class="toast-wrap" id="toastWrap"></div>

<script>
const $=id=>document.getElementById(id);
const entries=[];

function pad(n){return String(n).padStart(2,'0');}
function pad2h(h,m){return pad(h)+':'+pad(m);}
function ts(){const d=new Date();return pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());}

// ---- tick marks along the position track ----
(function(){
  const vis=$('cvis');
  const n=9;
  for(let i=0;i<n;i++){
    const r=document.createElement('div');
    r.className='ring';r.style.left=(i/(n-1)*100)+'%';
    vis.appendChild(r);
  }
})();

// ---- log ----
function logAdd(msg,type=''){
  entries.unshift({t:ts(),m:msg,type});
  if(entries.length>10)entries.pop();
  $('log').innerHTML=entries.map(e=>{
    const cls=e.type==='ok'?'ok':e.type==='er'?'er':'';
    return '<div><span class="lt">'+e.t+'</span><span class="'+cls+'">'+e.m+'</span></div>';
  }).join('');
  $('lcnt').textContent=entries.length+' event'+(entries.length!==1?'s':'');
}

// ---- toast ----
function toast(msg,isErr){
  const t=document.createElement('div');
  t.className='toast'+(isErr?' er':'');
  t.textContent=msg;
  $('toastWrap').appendChild(t);
  requestAnimationFrame(()=>t.classList.add('show'));
  setTimeout(()=>{t.classList.remove('show');setTimeout(()=>t.remove(),300);},2600);
}

function posLabel(pos, state){
  if(state==='opening') return 'OPENING';
  if(state==='closing') return 'CLOSING';
  if(pos<=0.01) return 'OPEN';
  if(pos>=0.99) return 'CLOSED';
  return Math.round((1-pos)*100)+'% OPEN';
}

function setPhase(pos){
  // Day/night accent theme follows how open the curtain is, not just a
  // binary open/closed - it eases toward night as it closes.
  document.body.setAttribute('data-phase', pos>0.5?'night':'day');
}

function paintPosition(pos){
  const drapeW=Math.round(pos*100);
  const openPct=Math.round((1-pos)*100);
  $('drape').style.width=drapeW+'%';
  $('handle').style.left=drapeW+'%';
  $('posval').textContent=openPct+'%';
  $('pct-head').textContent=openPct+'%';
  setPhase(pos);
}

function applySnap(d){
  const label=posLabel(d.position, d.state);
  const sb=$('sbadge');
  sb.textContent=label;
  sb.className='state-badge'+(d.state!=='idle'?' '+d.state:'');
  const sv=$('stval');
  sv.textContent=label;

  const bv=d.battery;
  const pctB=Math.max(0,Math.min(100,Math.round((bv-3.0)/1.2*100)));
  $('bv').textContent=bv.toFixed(2);
  $('bfill').style.width=pctB+'%';
  $('bpct').textContent=pctB+'%';
  $('bfill').style.background=pctB>50?'var(--ink)':pctB>20?'var(--amber)':'var(--red)';
  $('banner-batt').classList.toggle('show', pctB<=15);

  $('modeBadge').textContent=(d.mode||'--').toUpperCase();
  $('banner-clock').classList.toggle('show', d.mode==='sta' && d.timeSynced===false);

  if(d.mode==='ap'){
    $('thirdLabel').textContent='Uptime';
    $('upval').textContent=fmtDuration(Math.floor(d.uptime));
  } else if(d.sleepInMs!==undefined && d.sleepInMs>=0){
    $('thirdLabel').textContent='Sleep in';
    $('upval').textContent=fmtDuration(Math.ceil(d.sleepInMs/1000));
  } else {
    $('thirdLabel').textContent='Uptime';
    $('upval').textContent=fmtDuration(Math.floor(d.uptime));
  }

  // Schedule card
  if(d.openHour!==undefined){
    $('schedTimes').textContent=pad2h(d.openHour,d.openMin)+' / '+pad2h(d.closeHour,d.closeMin);
  }
  if(d.timeSynced && d.nextEventSeconds>=0){
    const type=(d.nextEventType||'').toUpperCase();
    $('nextEvent').textContent=type+' in '+fmtDuration(d.nextEventSeconds);
    $('schedNote').textContent='';
  } else if(d.mode==='ap'){
    $('nextEvent').textContent='--';
    $('schedNote').textContent='Finish setup to enable the schedule.';
  } else {
    $('nextEvent').textContent='--';
    $('schedNote').textContent='Waiting for clock sync over WiFi.';
  }
}

function fmtDuration(totalSec){
  totalSec=Math.max(0,totalSec|0);
  const h=Math.floor(totalSec/3600), m=Math.floor((totalSec%3600)/60), s=totalSec%60;
  if(h>0) return h+'h '+pad(m)+'m';
  if(m>0) return m+'m '+pad(s)+'s';
  return s+'s';
}

function setGhost(pos, state){
  const ghost=$('drape-ghost');
  ghost.style.width=Math.round(pos*100)+'%';
  ghost.style.display='block';
  const openPct=Math.round((1-pos)*100);
  $('posval').textContent=openPct+'%';
  $('pct-head').textContent=openPct+'%';
  $('sbadge').textContent=posLabel(pos,state);
  $('stval').textContent=posLabel(pos,state);
  $('handle').style.left=Math.round(pos*100)+'%';
  setPhase(pos);
}
function hideGhost(){ $('drape-ghost').style.display='none'; }

// ---- animate position between polls using the ESP32's own move timing ----
let clientOffset=0, snap=null, rafId=null;
function rafLoop(){
  if(!snap || snap.state==='idle'){rafId=null;return;}
  const espElapsed=(performance.now()-clientOffset)-snap.moveStart;
  const fraction=Math.min(Math.max(espElapsed/snap.moveDur,0),1);
  let ghostPos;
  if(snap.state==='closing') ghostPos=Math.min(snap.position+(1.0-snap.position)*fraction,1.0);
  else ghostPos=Math.max(snap.position-snap.position*fraction,0.0);
  setGhost(ghostPos, snap.state);
  rafId=requestAnimationFrame(rafLoop);
}
function startRaf(){ if(rafId) cancelAnimationFrame(rafId); rafId=requestAnimationFrame(rafLoop); }

// ---- countdown ticker (client-side, resynced every poll) ----
let nextEventBase=null; // {seconds, type, capturedAtMs}
setInterval(()=>{
  $('ts').textContent=ts();
  if(nextEventBase){
    const elapsed=Math.floor((Date.now()-nextEventBase.capturedAtMs)/1000);
    const remain=nextEventBase.seconds-elapsed;
    if(remain>=0) $('nextEvent').textContent=nextEventBase.type.toUpperCase()+' in '+fmtDuration(remain);
  }
},1000);

// ---- robust polling: timeout + backoff, never leaves the UI silently stale ----
let pollDelay=2000;
const POLL_MIN=2000, POLL_MAX=10000;
let consecutiveFailures=0;
let lastKeepalive=0;

async function fetchWithTimeout(url,opts={},ms=4000){
  const ctrl=new AbortController();
  const id=setTimeout(()=>ctrl.abort(),ms);
  try{ return await fetch(url,{...opts,signal:ctrl.signal}); }
  finally{ clearTimeout(id); }
}

async function poll(){
  try{
    const fetchStart=performance.now();
    const r=await fetchWithTimeout('/status',{},4000);
    if(!r.ok) throw new Error('http '+r.status);
    const d=await r.json();
    const rtt=performance.now()-fetchStart;
    clientOffset=performance.now()-(d.now+rtt/2);
    snap=d;
    applySnap(d);

    if(d.timeSynced && d.nextEventSeconds>=0){
      nextEventBase={seconds:d.nextEventSeconds, type:d.nextEventType, capturedAtMs:Date.now()};
    } else {
      nextEventBase=null;
    }

    if(d.state==='idle'){
      if(rafId){cancelAnimationFrame(rafId);rafId=null;}
      hideGhost();
      paintPosition(d.position);
    } else {
      startRaf();
    }

    $('conn-online').style.display='flex';
    $('conn-offline').style.display='none';
    if(consecutiveFailures>0) logAdd('reconnected','ok');
    consecutiveFailures=0;
    pollDelay=POLL_MIN;

    // Keep the dashboard's own session alive so browsing it doesn't get cut
    // off by the device's auto-sleep timer, but don't spam it.
    if(d.mode==='sta' && d.state==='idle' && Date.now()-lastKeepalive>15000){
      lastKeepalive=Date.now();
      fetchWithTimeout('/keepalive',{},3000).catch(()=>{});
    }
  }catch(err){
    consecutiveFailures++;
    $('conn-online').style.display='none';
    $('conn-offline').style.display='flex';
    if(consecutiveFailures===1) logAdd('connection lost, retrying…','er');
    pollDelay=Math.min(POLL_MAX, pollDelay*1.5);
  }finally{
    setTimeout(poll,pollDelay);
  }
}
poll();
fetchWithTimeout('/ip',{},3000).then(r=>r.text()).then(t=>$('ipDisplay').textContent=t).catch(()=>{});

// ---- command buttons: disabled while in-flight, toast on failure ----
function bindCmd(btn,label,url){
  btn.addEventListener('click', async ()=>{
    if(btn.getAttribute('aria-disabled')==='true') return;
    btn.setAttribute('aria-disabled','true');
    logAdd('&gt; cmd '+label);
    try{
      const r=await fetchWithTimeout(url,{redirect:'follow'},4000);
      if(!r.ok) throw new Error('http '+r.status);
      setTimeout(poll,150);
    }catch(err){
      toast(label+' failed — check connection',true);
      logAdd(label+' failed','er');
    }finally{
      setTimeout(()=>btn.removeAttribute('aria-disabled'),400);
    }
  });
}
bindCmd($('btnOpen'),'OPEN','/open');
bindCmd($('btnStop'),'STOP','/stop');
bindCmd($('btnClose'),'CLOSE','/close');

function bindCalib(btn,end,label){
  btn.addEventListener('click', async ()=>{
    try{
      const r=await fetchWithTimeout('/calibrate?end='+end,{},4000);
      if(!r.ok) throw new Error('http '+r.status);
      toast('Marked as '+label);
      logAdd('calibrated: '+label,'ok');
      setTimeout(poll,150);
    }catch(err){
      toast('Calibration failed',true);
      logAdd('calibration failed','er');
    }
  });
}
bindCalib($('calOpen'),'open','fully open');
bindCalib($('calClose'),'close','fully closed');

// ---- drag-to-set slider on the curtain visualization ----
(function(){
  const vis=$('cvis');
  let dragging=false;

  function pctFromEvent(e){
    const rect=vis.getBoundingClientRect();
    const x=(e.touches?e.touches[0].clientX:e.clientX)-rect.left;
    return Math.max(0,Math.min(100,Math.round((x/rect.width)*100)));
  }

  function onDown(e){
    dragging=true;
    onMove(e);
  }
  function onMove(e){
    if(!dragging) return;
    const drapePct=pctFromEvent(e); // % closed, left-to-right
    $('drape').style.width=drapePct+'%';
    $('handle').style.left=drapePct+'%';
    const openPct=100-drapePct;
    $('posval').textContent=openPct+'%';
    $('pct-head').textContent=openPct+'%';
    setPhase(drapePct/100);
  }
  async function onUp(e){
    if(!dragging) return;
    dragging=false;
    const drapePct=pctFromEvent(e);
    const openPct=100-drapePct;
    logAdd('&gt; goto '+openPct+'% open');
    try{
      const r=await fetchWithTimeout('/goto?pos='+openPct,{},4000);
      if(!r.ok) throw new Error('http '+r.status);
      setTimeout(poll,150);
    }catch(err){
      toast('Move failed — check connection',true);
      logAdd('goto failed','er');
    }
  }

  vis.addEventListener('mousedown',onDown);
  window.addEventListener('mousemove',onMove);
  window.addEventListener('mouseup',onUp);
  vis.addEventListener('touchstart',onDown,{passive:true});
  window.addEventListener('touchmove',onMove,{passive:true});
  window.addEventListener('touchend',onUp);
})();
</script>
</body>
</html>
)rawhtml";

// ---------- SETUP PAGE (configuration form) ----------
const char PAGE_SETUP[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VerhoBot Setup</title>
<style>
:root{
  --bg:#f5f4f0;--ink:#1a1a1a;--ink2:#3a3a3a;--mute:#8a8a85;--rule:#c8c7c0;
  --accent:#c2560c;--mono:'Courier New',Courier,monospace;
  --sans:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
}
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0;}
body{
  background:var(--bg);color:var(--ink);font-family:var(--sans);
  padding:2rem 1rem 3rem;
  background-image:linear-gradient(#dddcd6 1px,transparent 1px),linear-gradient(90deg,#dddcd6 1px,transparent 1px);
  background-size:24px 24px;
}
.wrap{max-width:440px;margin:0 auto;}
.logo{font-family:var(--mono);font-size:1.3rem;letter-spacing:.1em;margin-bottom:2px;}
.logo .bot{color:var(--accent);font-weight:bold;}
.sub{font-family:var(--mono);font-size:.5rem;letter-spacing:.2em;color:var(--mute);text-transform:uppercase;margin-bottom:1.5rem;}
h2{font-size:.9rem;font-family:var(--mono);letter-spacing:.1em;text-transform:uppercase;color:var(--accent);margin:1.5rem 0 .8rem;border-bottom:1px solid var(--rule);padding-bottom:4px;}
h2:first-of-type{margin-top:0;}
label{display:block;margin-top:.9rem;font-family:var(--mono);font-size:.62rem;letter-spacing:.08em;text-transform:uppercase;color:var(--ink2);}
.hint{font-family:var(--mono);font-size:.5rem;color:var(--mute);margin-top:3px;line-height:1.5;}
input{width:100%;padding:.6rem .5rem;font-size:.9rem;font-family:var(--sans);border:1px solid var(--rule);background:#fff;margin-top:4px;}
input:focus{outline:2px solid var(--accent);outline-offset:-1px;}
input:invalid:not(:placeholder-shown){border-color:#8b1a1a;}
button{
  margin-top:1.8rem;padding:.8rem 1.5rem;background:var(--accent);color:#fff;
  border:none;font-family:var(--mono);letter-spacing:.1em;text-transform:uppercase;
  font-size:.75rem;cursor:pointer;width:100%;
}
button:hover{background:#a04709;}
button:disabled{opacity:.5;cursor:default;}
a.back{display:inline-block;margin-top:1.2rem;color:var(--mute);font-family:var(--mono);font-size:.6rem;text-decoration:none;letter-spacing:.05em;}
a.back:hover{color:var(--accent);}
.msg{font-family:var(--mono);font-size:.62rem;padding:8px 10px;margin-top:1rem;display:none;}
.msg.show{display:block;}
.msg.ok{border:1px solid #1a6b3a;color:#1a6b3a;}
.msg.err{border:1px solid #8b1a1a;color:#8b1a1a;}
</style>
</head>
<body>
<div class="wrap">
  <div class="logo">VERHO<span class="bot">/BOT</span></div>
  <div class="sub">Configuration</div>

  <div class="msg" id="msg"></div>

  <form id="form">
    <h2>WiFi</h2>
    <label for="ssid">Network name (SSID)</label>
    <input id="ssid" name="ssid" required>

    <label for="pass">Password</label>
    <input id="pass" name="pass" type="password" placeholder="Leave blank to keep current password">
    <div class="hint">Only fill this in if you're changing networks or the password itself.</div>

    <h2>Schedule</h2>
    <label for="openTime">Open at (24h)</label>
    <input id="openTime" name="openTime" placeholder="07:00" pattern="[0-9]{2}:[0-9]{2}" required>

    <label for="closeTime">Close at (24h)</label>
    <input id="closeTime" name="closeTime" placeholder="22:00" pattern="[0-9]{2}:[0-9]{2}" required>

    <label for="tz">Timezone</label>
    <input id="tz" name="tz" placeholder="EET-2EEST,M3.5.0/3,M10.5.0/4">
    <div class="hint">POSIX TZ string, handles daylight saving automatically. Default is Europe/Helsinki. Find yours at <span style="white-space:nowrap">github.com/nayarsystems/posix_tz_db</span>.</div>

    <button type="submit" id="submitBtn">Save &amp; restart</button>
  </form>
  <a class="back" href="/">&larr; back to dashboard</a>
</div>

<script>
const $=id=>document.getElementById(id);

// Prefill from the device's current config so re-visiting this page doesn't
// present blank fields (previously every edit meant retyping everything,
// including the WiFi password, just to change the schedule).
(async function(){
  try{
    const r=await fetch('/config');
    if(!r.ok) return;
    const c=await r.json();
    if(c.ssid) $('ssid').value=c.ssid;
    if(c.openHour!==undefined) $('openTime').value=String(c.openHour).padStart(2,'0')+':'+String(c.openMin).padStart(2,'0');
    if(c.closeHour!==undefined) $('closeTime').value=String(c.closeHour).padStart(2,'0')+':'+String(c.closeMin).padStart(2,'0');
    if(c.tz) $('tz').value=c.tz;
  }catch(err){ /* fresh device, or AP mode before first save - fields just stay blank */ }
})();

$('form').addEventListener('submit', async (e)=>{
  e.preventDefault();
  const btn=$('submitBtn');
  const msg=$('msg');
  msg.className='msg';
  btn.disabled=true;
  btn.textContent='Saving…';

  const body=new URLSearchParams(new FormData($('form')));
  try{
    const ctrl=new AbortController();
    const timeout=setTimeout(()=>ctrl.abort(),6000);
    const r=await fetch('/save',{method:'POST',body,signal:ctrl.signal});
    clearTimeout(timeout);
    if(!r.ok){
      const text=await r.text();
      throw new Error(text||('HTTP '+r.status));
    }
    msg.textContent='Saved. VerhoBot is restarting…';
    msg.className='msg ok show';
    setTimeout(()=>{ window.location.href='/'; }, 4000);
  }catch(err){
    msg.textContent='Could not save: '+(err.message||'connection lost')+'. Check the device is still on this network and try again.';
    msg.className='msg err show';
    btn.disabled=false;
    btn.textContent='Save & restart';
  }
});
</script>
</body>
</html>
)rawhtml";

// ---------- Web Handlers ----------
void handleRoot() {
    server.send_P(200, "text/html", PAGE_DASHBOARD);
}

void handleSetup() {
    server.send_P(200, "text/html", PAGE_SETUP);
}

void handleSave() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String openT = server.arg("openTime");
    String closeT = server.arg("closeTime");
    String tz = server.arg("tz");

    if (ssid.length() == 0) {
        server.send(400, "text/plain", "SSID is required");
        return;
    }

    int oh, om, ch, cm;
    if (sscanf(openT.c_str(), "%d:%d", &oh, &om) != 2 ||
        sscanf(closeT.c_str(), "%d:%d", &ch, &cm) != 2) {
        server.send(400, "text/plain", "Invalid time format. Use HH:MM");
        return;
    }
    if (oh < 0 || oh > 23 || om < 0 || om > 59 || ch < 0 || ch > 23 || cm < 0 || cm > 59) {
        server.send(400, "text/plain", "Time values out of range");
        return;
    }

    // Password field is left blank when the user only wants to change the
    // schedule - don't force them to retype WiFi credentials every time.
    if (pass.length() == 0) pass = wifiPass;
    if (tz.length() == 0) tz = tzString.length() ? tzString : String(TIMEZONE_STRING);

    saveConfig(ssid, pass, oh, om, ch, cm, tz);
    String response = "<html><body><h2>✅ Saved!</h2><p>Restarting...</p>"
                      "<meta http-equiv='refresh' content='3;url=/'>"
                      "</body></html>";
    server.send(200, "text/html", response);
    delay(500);
    ESP.restart();
}

void handleOpen() {
    startOpen(CURTAIN_TRAVEL_TIME);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleClose() {
    startClose(CURTAIN_TRAVEL_TIME);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleStop() {
    stopMotor();
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleGoto() {
    if (!server.hasArg("pos")) {
        server.send(400, "text/plain", "Missing pos (0-100, 100=open)");
        return;
    }
    int openPct = server.arg("pos").toInt();
    openPct = constrain(openPct, 0, 100);
    float targetPos = 1.0f - (openPct / 100.0f); // curtainPos: 0=open, 1=closed
    gotoPosition(targetPos);
    server.sendHeader("Location", "/");
    server.send(303);
}

// Manually mark the current physical position as a known end-stop, without
// moving the motor. Use this if the tracked position has drifted from
// reality (the tracking is open-loop/time-based - see project notes).
void handleCalibrate() {
    String end = server.arg("end");
    if (end == "open") {
        curtainPos = 0.0f;
    } else if (end == "close") {
        curtainPos = 1.0f;
    } else {
        server.send(400, "text/plain", "end must be 'open' or 'close'");
        return;
    }
    motorStop();
    state = IDLE;
    rtcCurtainPos = curtainPos;
    server.sendHeader("Location", "/");
    server.send(303);
}

// Lets the dashboard postpone the STA-mode auto-sleep timer while someone
// is actively looking at / using the page.
void handleKeepalive() {
    awakeStart = millis();
    server.send(200, "text/plain", "ok");
}

// Current saved config, for prefilling the setup form (password omitted).
void handleConfigGet() {
    JsonDocument doc;
    doc["ssid"] = wifiSSID;
    doc["openHour"] = openHour;
    doc["openMin"] = openMin;
    doc["closeHour"] = closeHour;
    doc["closeMin"] = closeMin;
    doc["tz"] = tzString;
    String out;
    serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", out);
}

void handleStatus() {
    uint32_t now = millis();
    bool apMode = (WiFi.getMode() == WIFI_AP);
    bool timeSynced = (time(nullptr) > 8 * 3600);
    NextEvent ev = computeNextEvent();

    JsonDocument doc;
    doc["state"] = (state == OPENING) ? "opening" : (state == CLOSING) ? "closing" : "idle";
    doc["position"] = round(curtainPos * 1000) / 1000.0;
    doc["battery"] = round(readBatteryVoltage() * 1000) / 1000.0;
    doc["uptime"] = now / 1000;
    doc["moveStart"] = moveStart;
    doc["moveDur"] = moveDuration;
    doc["now"] = now;
    doc["mode"] = apMode ? "ap" : "sta";
    doc["timeSynced"] = timeSynced;
    doc["openHour"] = openHour;
    doc["openMin"] = openMin;
    doc["closeHour"] = closeHour;
    doc["closeMin"] = closeMin;
    doc["nextEventType"] = ev.isOpenEvent ? "open" : "close";
    doc["nextEventSeconds"] = timeSynced ? (uint32_t)ev.secondsUntil : -1;

    if (!apMode && state == IDLE) {
        int32_t remain = (int32_t)AWAKE_TIMEOUT - (int32_t)(now - awakeStart);
        doc["sleepInMs"] = remain > 0 ? remain : 0;
    } else {
        doc["sleepInMs"] = -1; // won't sleep: in AP mode or currently moving
    }

    String json;
    serializeJson(doc, json);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
}

void handleIP() {
    server.send(200, "text/plain", WiFi.localIP().toString());
}

void startWebServer() {
    server.on("/",          handleRoot);
    server.on("/setup",     handleSetup);
    server.on("/save",      HTTP_POST, handleSave);
    server.on("/open",      handleOpen);
    server.on("/close",     handleClose);
    server.on("/stop",      handleStop);
    server.on("/goto",      handleGoto);
    server.on("/calibrate", handleCalibrate);
    server.on("/keepalive", handleKeepalive);
    server.on("/status",    handleStatus);
    server.on("/config",    handleConfigGet);
    server.on("/ip",        handleIP);
    server.begin();
    Serial.println("Web server started");
}

// ============================================================
//  Main Setup
// ============================================================
// Try to join the saved network, bounded by a real timeout (previously this
// was a hardcoded 20*250ms = 5s spin in three separate places).
bool connectSTA(uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

// If the saved credentials don't work, re-open the setup AP instead of
// leaving the device unreachable on both its own AP and the target network.
// This only fires for post-config attempts (see CASE 4 / CASE 2 below) -
// CASE 3 (unattended scheduled wake) deliberately skips this: popping open
// an AP and staying awake with nobody there to fix it would just drain the
// battery until the next button press.
void startFallbackAP() {
    Serial.println("Could not join the saved WiFi network - reopening VerhoBot-Setup so it can be fixed.");
    inFallbackAP = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("VerhoBot-Setup", "12345678");
    Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n=== VerhoBot v0.2 (deep_sleep + dashboard) ===");

    pinMode(AIN1, OUTPUT);
    pinMode(AIN2, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PWMA, PWM_FREQ, PWM_RESOLUTION_BITS);              // core 3.x
#else
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION_BITS);        // core <3.0
    ledcAttachPin(PWMA, PWM_CHANNEL);
#endif
    pinMode(STBY, OUTPUT);
    digitalWrite(STBY, HIGH);
    motorStop();

    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);

    // ---- Factory reset (hold button 3s) ----
    if (digitalRead(WAKE_BUTTON_PIN) == LOW) {
        unsigned long start = millis();
        while (digitalRead(WAKE_BUTTON_PIN) == LOW && millis() - start < 3000) {
            delay(10);
        }
        if (millis() - start >= 3000) {
            Serial.println("Factory reset triggered! Clearing config.");
            clearConfig();
            ESP.restart();
        }
    }

    // ---- Restore position from RTC ----
    curtainPos = rtcCurtainPos;
    rtcBootCount++;
    Serial.print("Boot #"); Serial.println(rtcBootCount);
    Serial.print("Curtain position: "); Serial.println(curtainPos * 100);

    loadConfig();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // ---- CASE 1: No config → AP mode (dashboard + setup) ----
    if (!configured) {
        Serial.println("No config found. Starting AP mode.");
        WiFi.softAP("VerhoBot-Setup", "12345678");
        Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
        startWebServer();
        return;
    }

    // ---- CASE 2: when Woken by button ----
    // ESP32-C3: GPIO wakeup reports ESP_SLEEP_WAKEUP_GPIO, not EXT1
    // (see the matching fix in enterDeepSleep()).
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
    bool wokeByButton = (cause == ESP_SLEEP_WAKEUP_EXT1);
#else
    bool wokeByButton = (cause == ESP_SLEEP_WAKEUP_GPIO);
#endif
    if (wokeByButton) {
        Serial.println("Woken by button! Toggling curtain.");
        if (curtainPos < 0.1f) startClose(CURTAIN_TRAVEL_TIME);
        else startOpen(CURTAIN_TRAVEL_TIME);
        // Connect to WiFi so dashboard works
        if (connectSTA(WIFI_CONNECT_TIMEOUT_MS)) {
            Serial.print("STA IP: "); Serial.println(WiFi.localIP());
            syncTime(); // optional
        } else {
            // Someone's physically at the device (they just pressed the
            // button) - worth giving them a way to fix bad credentials
            // rather than silently failing.
            startFallbackAP();
        }
        startWebServer();
        awakeStart = millis();
        return;
    }

    // ---- CASE 3: Woken by timer (scheduled) ----
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Woken by timer! Connecting to WiFi...");
        // Deliberately no AP fallback here: this is an unattended scheduled
        // wake. Popping open an AP and staying awake (AP mode never sleeps -
        // see loop()) with nobody there to fix it would just drain the
        // battery until someone notices. Skip the move and retry at the
        // next scheduled wake instead - see the unsynced-clock guard in
        // enterDeepSleep() for how the retry timing stays sane.
        if (connectSTA(WIFI_CONNECT_TIMEOUT_MS)) {
            Serial.print("STA IP: "); Serial.println(WiFi.localIP());
            if (syncTime()) {
                Serial.println("NTP synced: " + getTimeString());
                time_t now = time(nullptr);
                struct tm *tm_now = localtime(&now);
                int currentMin = tm_now->tm_hour * 60 + tm_now->tm_min;
                int openMinTotal = openHour * 60 + openMin;
                int closeMinTotal = closeHour * 60 + closeMin;
                bool shouldBeOpen = (currentMin >= openMinTotal && currentMin < closeMinTotal);
                if (shouldBeOpen && curtainPos > 0.1f) startOpen(CURTAIN_TRAVEL_TIME);
                else if (!shouldBeOpen && curtainPos < 0.9f) startClose(CURTAIN_TRAVEL_TIME);
                else Serial.println("Already in correct position.");
            } else {
                Serial.println("NTP failed. Skipping move.");
            }
        } else {
            Serial.println("WiFi failed. Skipping move.");
        }
        startWebServer();
        awakeStart = millis();
        return;
    }

    // ---- CASE 4: Normal boot (after restart / power‑on) ----
    Serial.println("Normal boot. Connecting to WiFi...");
    if (connectSTA(WIFI_CONNECT_TIMEOUT_MS)) {
        Serial.print("STA IP: "); Serial.println(WiFi.localIP());
        syncTime();
    } else {
        // This is the exact case that used to leave the device unreachable:
        // configured==true so CASE 1's AP never runs, but if the saved
        // SSID/password don't actually work, there was previously no way
        // back in short of re-flashing or erasing Preferences.
        startFallbackAP();
    }
    startWebServer();
    awakeStart = millis();  // stay awake for 60s then sleep
}

// ============================================================
//  Main Loop
// ============================================================
void loop() {
    server.handleClient();
    updateMotor();

    // ---- AP mode: stay awake ----
    if (WiFi.getMode() == WIFI_AP) {
        // The never-configured setup AP (CASE 1) stays up indefinitely -
        // that's an out-of-box, actively-being-set-up state.
        // The fallback AP (saved credentials failed to connect) is bounded:
        // if nobody connects to fix it within AP_FALLBACK_TIMEOUT_MS, give
        // up and go back to sleep to retry the saved network later, rather
        // than burning battery broadcasting an AP with nobodye around.
        if (inFallbackAP && millis() - awakeStart > AP_FALLBACK_TIMEOUT_MS) {
            Serial.println("No one connected to the fallback AP in time - sleeping to retry the saved network later.");
            enterDeepSleep();
        }
        return;
    }

    // ---- STA mode: sleep after 60s of idle ----
    if (state == IDLE) {
        if (millis() - awakeStart > AWAKE_TIMEOUT) {
            enterDeepSleep();
        }
    } else {
        // motor is moving – keep resetting the awake timer
        awakeStart = millis();
    }
}
