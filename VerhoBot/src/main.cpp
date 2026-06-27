#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <esp_sleep.h>

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
bool   configured = false;

// Web server
WebServer server(80);

// Awake timeout (STA mode only)
uint32_t awakeStart = 0;
const uint32_t AWAKE_TIMEOUT = 60000;  // 60 seconds after motor stops

// ============================================================
//  Motor control (non‑blocking)
// ============================================================
void motorOpen() {
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);
    digitalWrite(PWMA, HIGH);
}

void motorClose() {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
    digitalWrite(PWMA, HIGH);
}

void motorStop() {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
    digitalWrite(PWMA, LOW);
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

void updateMotor() {
    if (state == IDLE) return;
    uint32_t elapsed = millis() - moveStart;
    if (elapsed >= moveDuration || elapsed >= MAX_RUN_TIME)
        stopMotor();
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
void saveConfig(String ssid, String pass, int oh, int om, int ch, int cm) {
    prefs.begin("verhobot", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.putInt("openH", oh);
    prefs.putInt("openM", om);
    prefs.putInt("closeH", ch);
    prefs.putInt("closeM", cm);
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
    configTime(0, 0, "pool.ntp.org", "time.google.com");
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
uint64_t calculateSleepSeconds() {
    time_t now = time(nullptr);
    struct tm *tm_now = localtime(&now);
    
    int targetHour, targetMin;
    if (curtainPos > 0.5f) {
        targetHour = closeHour;
        targetMin  = closeMin;
    } else {
        targetHour = openHour;
        targetMin  = openMin;
    }
    
    struct tm target = *tm_now;
    target.tm_hour = targetHour;
    target.tm_min  = targetMin;
    target.tm_sec  = 0;
    
    time_t targetTime = mktime(&target);
    if (targetTime <= now) targetTime += 86400;
    
    return (uint64_t)(targetTime - now);
}

void enterDeepSleep() {
    Serial.println("Entering deep sleep...");
    delay(100);

    rtcCurtainPos = curtainPos;

    motorStop();
    digitalWrite(STBY, LOW);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    uint64_t sleepSec = calculateSleepSeconds();
    esp_sleep_enable_timer_wakeup(sleepSec * 1000000ULL);
    esp_sleep_enable_ext1_wakeup(1ULL << WAKE_BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_LOW);

    Serial.printf("Sleeping for %llu seconds until %02d:%02d\n", sleepSec,
                  (curtainPos > 0.5f) ? closeHour : openHour,
                  (curtainPos > 0.5f) ? closeMin  : openMin);
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
  --orange:#b84a00;--orange-dim:rgba(184,74,0,0.10);--orange-border:rgba(184,74,0,0.35);
  --green:#1a6b3a;--red:#8b1a1a;--amber:#7a5500;
  --mono:'Courier New',Courier,'Lucida Console',monospace;
  --sans:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
}
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0;}
body{
  background:var(--bg);color:var(--ink);font-family:var(--sans);
  min-height:100vh;padding:1.5rem 1rem 3rem;
  background-image:
    linear-gradient(var(--rule2) 1px,transparent 1px),
    linear-gradient(90deg,var(--rule2) 1px,transparent 1px);
  background-size:24px 24px;
}
.wrap{max-width:480px;margin:0 auto;}

header{
  display:flex;justify-content:space-between;align-items:flex-start;
  border-bottom:2px solid var(--ink);padding-bottom:10px;margin-bottom:1.25rem;
  position:relative;
}
header::after{
  content:'';position:absolute;bottom:-2px;left:0;width:48px;height:2px;
  background:var(--orange);
}
.logo{
  font-family:var(--mono);font-size:1.5rem;letter-spacing:.1em;
  display:flex;align-items:baseline;gap:0;line-height:1;
}
.logo-verho{color:var(--ink);font-weight:normal;}
.logo-slash{color:var(--orange);margin:0 1px;}
.logo-bot{
  color:var(--orange);
  font-weight:bold;
  letter-spacing:.06em;
}
.logo-sub{font-family:var(--mono);font-size:.5rem;letter-spacing:.2em;color:var(--mute);margin-top:5px;text-transform:uppercase;}
.conn{font-family:var(--mono);font-size:.52rem;letter-spacing:.15em;text-transform:uppercase;
  display:flex;align-items:center;gap:5px;color:var(--green);}
.conn-dot{width:6px;height:6px;border-radius:50%;background:var(--green);animation:blink 2.5s steps(1) infinite;}
#conn-offline{color:var(--red);display:none;}
#conn-offline .conn-dot{background:var(--red);}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0}}
.ip{font-family:var(--mono);font-size:.48rem;letter-spacing:.1em;color:var(--mute);margin-top:3px;text-align:right;}
.gear{
  display:inline-block;font-size:1.2rem;cursor:pointer;text-decoration:none;color:var(--mute);
  transition:transform 0.3s;
}
.gear:hover{transform:rotate(60deg);color:var(--orange);}

.section{
  font-family:var(--mono);font-size:.52rem;letter-spacing:.2em;text-transform:uppercase;
  color:var(--orange);margin-bottom:8px;display:flex;align-items:center;gap:8px;
}
.section::after{content:'';flex:1;height:1px;background:var(--rule);}

.curtain-block{border:1px solid var(--ink2);margin-bottom:1.25rem;}
.curtain-head{
  background:var(--ink);color:#f5f4f0;
  font-family:var(--mono);font-size:.58rem;letter-spacing:.18em;
  padding:5px 10px;display:flex;justify-content:space-between;
}
.curtain-head span:last-child{color:var(--orange);}
.curtain-vis{position:relative;height:88px;background:var(--surface);overflow:hidden;}
.curtain-vis::before{content:'';position:absolute;top:0;left:0;right:0;height:3px;background:var(--ink);}
.drape{
  position:absolute;top:3px;left:0;height:calc(100% - 3px);
  background:var(--orange-dim);
  border-right:1.5px solid var(--orange-border);
  overflow:hidden;
  z-index:1;
}
.drape::after{
  content:'';position:absolute;inset:0;
  background-image:repeating-linear-gradient(45deg,transparent,transparent 4px,rgba(184,74,0,.07) 4px,rgba(184,74,0,.07) 5px);
}
.drape-ghost{
  position:absolute;top:3px;left:0;height:calc(100% - 3px);
  background:transparent;
  border-right:2px dashed rgba(184,74,0,0.35);
  pointer-events:none;
  z-index:2;
  display:none;
}
.drape-ghost::after{
  content:'';position:absolute;inset:0;
  background-image:repeating-linear-gradient(45deg,transparent,transparent 4px,rgba(184,74,0,.04) 4px,rgba(184,74,0,.04) 5px);
}
.ring{position:absolute;top:-2px;width:7px;height:7px;border:1.5px solid var(--ink);border-radius:50%;background:var(--bg);transform:translateX(-50%);z-index:3;}
.curtain-foot{border-top:1px solid var(--rule);padding:6px 10px;display:flex;justify-content:space-between;align-items:center;}
.pos-scale{font-family:var(--mono);font-size:.48rem;letter-spacing:.08em;color:var(--mute);}
.pos-right{display:flex;align-items:center;gap:10px;}
.state-badge{
  font-family:var(--mono);font-size:.48rem;letter-spacing:.15em;text-transform:uppercase;
  padding:2px 7px;border:1px solid var(--ink2);color:var(--ink2);
}
.state-badge.opening{border-color:var(--orange);color:var(--orange);}
.state-badge.closing{border-color:var(--orange);color:var(--orange);}
.pos-pct{font-family:var(--mono);font-size:1rem;font-weight:700;color:var(--orange);}

.metrics{display:grid;grid-template-columns:1fr 1fr 1fr;border:1px solid var(--ink2);margin-bottom:1.25rem;}
.metric{padding:.75rem .9rem;border-right:1px solid var(--rule);}
.metric:last-child{border-right:none;}
.m-label{font-family:var(--mono);font-size:.46rem;letter-spacing:.18em;text-transform:uppercase;color:var(--mute);margin-bottom:5px;}
.m-val{font-family:var(--mono);font-size:.92rem;font-weight:700;line-height:1;}
.m-unit{font-family:var(--mono);font-size:.48rem;color:var(--mute);margin-left:1px;}
.bbar-row{margin-top:5px;display:flex;align-items:center;gap:5px;}
.bbar-track{flex:1;height:3px;background:var(--rule2);}
.bbar-fill{height:100%;background:var(--ink);transition:width .6s ease;}

.controls{display:grid;grid-template-columns:1fr 1fr 1fr;border:1px solid var(--ink2);margin-bottom:1.25rem;}
.cmd{
  all:unset;cursor:pointer;
  font-family:var(--mono);font-size:.58rem;letter-spacing:.15em;text-transform:uppercase;
  text-align:center;color:var(--ink2);
  padding:.85rem .4rem;
  display:flex;flex-direction:column;align-items:center;gap:7px;
  border-right:1px solid var(--rule);
  transition:background .12s,color .12s,border-color .12s;
  position:relative;
}
.cmd:last-child{border-right:none;}
.cmd:active{transform:scale(.98);}
.cmd svg{width:22px;height:22px;stroke:var(--ink2);fill:none;stroke-width:1.5;stroke-linecap:round;stroke-linejoin:round;transition:stroke .12s;}
.cmd::before{content:'';position:absolute;top:4px;left:4px;width:5px;height:5px;border-top:1px solid var(--rule);border-left:1px solid var(--rule);}
.cmd::after{content:'';position:absolute;bottom:4px;right:4px;width:5px;height:5px;border-bottom:1px solid var(--rule);border-right:1px solid var(--rule);}
.cmd.open:hover,.cmd.close:hover{background:var(--orange);color:#fff;}
.cmd.open:hover svg,.cmd.close:hover svg{stroke:#fff;}
.cmd.stop:hover{background:var(--ink);color:#f5f4f0;}
.cmd.stop:hover svg{stroke:#f5f4f0;}

.log-block{border:1px solid var(--rule);margin-bottom:1.25rem;}
.log-head{font-family:var(--mono);font-size:.52rem;letter-spacing:.18em;text-transform:uppercase;color:var(--mute);padding:5px 10px;border-bottom:1px solid var(--rule);display:flex;justify-content:space-between;}
#log{padding:8px 10px;font-family:var(--mono);font-size:.58rem;color:var(--mute);line-height:2;min-height:80px;}
.lt{color:var(--rule);margin-right:8px;}
.ok{color:var(--green);}
.er{color:var(--red);}

footer{display:flex;justify-content:space-between;padding-top:10px;border-top:1px solid var(--rule);font-family:var(--mono);font-size:.46rem;letter-spacing:.12em;text-transform:uppercase;color:var(--mute);}
</style>
</head>
<body>
<div class="wrap">

<header>
  <div>
    <div class="logo"><span class="logo-verho">VERHO</span><span class="logo-slash">/</span><span class="logo-bot">BOT</span></div>
    <div class="logo-sub">ESP32 &middot; TB6612 &middot; WiFi AP</div>
  </div>
  <div style="text-align:right">
    <div class="conn" id="conn-online">
      <span>online</span><div class="conn-dot"></div>
    </div>
    <div class="conn" id="conn-offline">
      <span>offline</span><div class="conn-dot"></div>
    </div>
    <div class="ip" id="ipDisplay">--</div>
    <a href="/setup" class="gear" title="Settings">⚙</a>
  </div>
</header>

<div class="section">curtain</div>
<div class="curtain-block">
  <div class="curtain-head">
    <span>POSITION TRACK</span>
    <span id="pct-head">0%</span>
  </div>
  <div class="curtain-vis" id="cvis">
    <div class="drape" id="drape" style="width:0%"></div>
    <div class="drape-ghost" id="drape-ghost" style="width:0%"></div>
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
    <div class="m-label">Uptime</div>
    <div class="m-val" style="font-size:.78rem;margin-top:2px" id="upval">--:--</div>
  </div>
</div>

<div class="section">control</div>
<div class="controls">
  <a href="/open" class="cmd open" onclick="logCmd('OPEN')">
    <svg viewBox="0 0 24 24"><polyline points="19 12 13 6 13 18"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
    open
  </a>
  <a href="/stop" class="cmd stop" onclick="logCmd('STOP')">
    <svg viewBox="0 0 24 24"><rect x="7" y="7" width="10" height="10"/></svg>
    stop
  </a>
  <a href="/close" class="cmd close" onclick="logCmd('CLOSE')">
    <svg viewBox="0 0 24 24"><polyline points="5 12 11 6 11 18"/><line x1="19" y1="12" x2="5" y2="12"/></svg>
    close
  </a>
</div>

<div class="section">log</div>
<div class="log-block">
  <div class="log-head">
    <span>serial output</span>
    <span id="lcnt">0 events</span>
  </div>
  <div id="log"><span style="color:var(--rule2)">// awaiting events</span></div>
</div>

<footer>
  <span>verhobot v2.0</span>
  <span id="ts">--:--:--</span>
  <span>finland</span>
</footer>
</div>

<script>
const $=id=>document.getElementById(id);
const entries=[];
let bootMs=Date.now();

function pad(n){return String(n).padStart(2,'0');}
function ts(){const d=new Date();return pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());}

(function(){
  const vis=$('cvis');
  const n=9;
  for(let i=0;i<n;i++){
    const r=document.createElement('div');
    r.className='ring';r.style.left=(i/(n-1)*100)+'%';
    vis.appendChild(r);
  }
})();

function logAdd(msg,type=''){
  entries.unshift({t:ts(),m:msg,type});
  if(entries.length>8)entries.pop();
  $('log').innerHTML=entries.map(e=>{
    const cls=e.type==='ok'?'ok':e.type==='er'?'er':'';
    return '<div><span class="lt">'+e.t+'</span><span class="'+cls+'">'+e.m+'</span></div>';
  }).join('');
  $('lcnt').textContent=entries.length+' event'+(entries.length!==1?'s':'');
}

function logCmd(c){logAdd('> cmd '+c);}

function posLabel(pos, state){
  if(state==='opening') return 'OPENING';
  if(state==='closing') return 'CLOSING';
  if(pos<=0.01) return 'OPEN';
  if(pos>=0.99) return 'CLOSED';
  return Math.round((1-pos)*100)+'% OPEN';
}

function applySnap(d){
  const label=posLabel(d.position, d.state);
  const sb=$('sbadge');
  sb.textContent=label;
  sb.className='state-badge'+(d.state!=='idle'?' '+d.state:'');
  const sv=$('stval');
  sv.textContent=label;
  sv.style.color=d.state==='opening'?'var(--green)':d.state==='closing'?'var(--red)':'var(--ink)';
  const bv=d.battery;
  const pctB=Math.max(0,Math.round((bv-3.0)/1.2*100));
  $('bv').textContent=bv.toFixed(2);
  $('bfill').style.width=pctB+'%';
  $('bpct').textContent=pctB+'%';
  $('bfill').style.background=pctB>50?'var(--ink)':pctB>20?'var(--amber)':'var(--red)';
  if(d.uptime!==undefined){
    const s=d.uptime,m=Math.floor(s/60),h=Math.floor(m/60);
    $('upval').textContent=h>0?pad(h)+':'+pad(m%60)+':'+pad(s%60):pad(m%60)+':'+pad(s%60);
  }
}

function commitDrape(pos){
  const drapeW=Math.round(pos*100);
  const openPct=Math.round((1-pos)*100);
  $('drape').style.width=drapeW+'%';
  $('posval').textContent=openPct+'%';
  $('pct-head').textContent=openPct+'%';
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
}

function hideGhost(){
  $('drape-ghost').style.display='none';
}

let clientOffset=0;
let snap=null;
let rafId=null;

function rafLoop(){
  if(!snap || snap.state==='idle'){rafId=null;return;}
  const espElapsed=(performance.now()-clientOffset)-snap.moveStart;
  const fraction=Math.min(Math.max(espElapsed/snap.moveDur,0),1);
  let ghostPos;
  if(snap.state==='closing'){
    ghostPos=Math.min(snap.position+(1.0-snap.position)*fraction,1.0);
  } else {
    ghostPos=Math.max(snap.position-snap.position*fraction,0.0);
  }
  setGhost(ghostPos, snap.state);
  rafId=requestAnimationFrame(rafLoop);
}

function startRaf(){
  if(rafId) cancelAnimationFrame(rafId);
  rafId=requestAnimationFrame(rafLoop);
}

async function poll(){
  try{
    const fetchStart=performance.now();
    const r=await fetch('/status');
    if(!r.ok) throw new Error();
    const d=await r.json();
    const rtt=performance.now()-fetchStart;
    clientOffset=performance.now()-(d.now+rtt/2);
    snap=d;
    applySnap(d);

    if(d.state==='idle'){
      if(rafId){cancelAnimationFrame(rafId);rafId=null;}
      hideGhost();
      commitDrape(d.position);
    } else {
      startRaf();
    }

    $('conn-online').style.display='flex';
    $('conn-offline').style.display='none';
  } catch(e){
    $('conn-online').style.display='none';
    $('conn-offline').style.display='flex';
  }
}

function clock(){$('ts').textContent=ts();}

// Display IP
fetch('/ip').then(r=>r.text()).then(ip=>$('ipDisplay').textContent=ip).catch(()=>{});

logAdd('system boot','ok');
logAdd('web dashboard ready','ok');
poll();
setInterval(poll,2000);
setInterval(clock,1000);
clock();
</script>
</body>
</html>
)rawhtml";

// ---------- SETUP PAGE (configuration form) ----------
const char PAGE_SETUP[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VerhoBot Setup</title>
<style>
body{font-family:sans-serif;background:#f5f4f0;padding:2rem;max-width:500px;margin:auto}
h1{color:#b84a00}
label{display:block;margin-top:1rem;font-weight:bold}
input{width:100%;padding:0.5rem;font-size:1rem;border:1px solid #ccc}
button{margin-top:1.5rem;padding:0.7rem 2rem;background:#b84a00;color:#fff;border:none;font-size:1.2rem;cursor:pointer}
button:hover{background:#a03d00}
a{display:inline-block;margin-top:1rem;color:#b84a00}
</style>
</head>
<body>
<h1>⚙️ VerhoBot Setup</h1>
<form action="/save" method="POST">
  <label>WiFi SSID</label>
  <input name="ssid" required>
  <label>WiFi Password</label>
  <input name="pass" type="password" required>
  <label>Open at (HH:MM, 24h)</label>
  <input name="openTime" placeholder="07:00" pattern="[0-9]{2}:[0-9]{2}" required>
  <label>Close at (HH:MM, 24h)</label>
  <input name="closeTime" placeholder="22:00" pattern="[0-9]{2}:[0-9]{2}" required>
  <button type="submit">Save & Restart</button>
</form>
<a href="/">&larr; Back to Dashboard</a>
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

    saveConfig(ssid, pass, oh, om, ch, cm);
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

void handleStatus() {
    uint32_t now = millis();
    String json = "{";
    json += "\"state\":\"" + String(state == OPENING ? "opening" : state == CLOSING ? "closing" : "idle") + "\",";
    json += "\"position\":"    + String(curtainPos, 3)         + ",";
    json += "\"battery\":"     + String(readBatteryVoltage(), 3) + ",";
    json += "\"uptime\":"      + String(now / 1000)             + ",";
    json += "\"moveStart\":"   + String(moveStart)              + ",";
    json += "\"moveDur\":"     + String(moveDuration)           + ",";
    json += "\"now\":"         + String(now);
    json += "}";
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
}

void handleIP() {
    server.send(200, "text/plain", WiFi.localIP().toString());
}

void startWebServer() {
    server.on("/",        handleRoot);
    server.on("/setup",   handleSetup);
    server.on("/save",    HTTP_POST, handleSave);
    server.on("/open",    handleOpen);
    server.on("/close",   handleClose);
    server.on("/stop",    handleStop);
    server.on("/status",  handleStatus);
    server.on("/ip",      handleIP);
    server.begin();
    Serial.println("Web server started");
}

// ============================================================
//  Main Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n=== VerhoBot v0.2 (deep_sleep + dashboard) ===");

    pinMode(AIN1, OUTPUT);
    pinMode(AIN2, OUTPUT);
    pinMode(PWMA, OUTPUT);
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

    // ---- CASE 2: Woken by button ----
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        Serial.println("Woken by button! Toggling curtain.");
        if (curtainPos < 0.1f) startClose(CURTAIN_TRAVEL_TIME);
        else startOpen(CURTAIN_TRAVEL_TIME);
        // Connect to WiFi so dashboard works
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(250); attempts++; }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("STA IP: "); Serial.println(WiFi.localIP());
            syncTime(); // optional
        }
        startWebServer();
        awakeStart = millis();
        return;
    }

    // ---- CASE 3: Woken by timer (scheduled) ----
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Woken by timer! Connecting to WiFi...");
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(250); attempts++; }
        if (WiFi.status() == WL_CONNECTED) {
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
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(250); attempts++; }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("STA IP: "); Serial.println(WiFi.localIP());
        syncTime();
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

    // ---- AP mode: stay awake forever ----
    if (WiFi.getMode() == WIFI_AP) {
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
