#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include "pins.h" 

WebServer server(80);

const char* ssid     = "verhobot";
const char* password = "12345678";

// ======================================================
// Configuration
// ======================================================

const uint32_t CURTAIN_TRAVEL_TIME = 4000;
const uint32_t MAX_RUN_TIME        = 8000;
const float    BATTERY_CAL         = 2.08f;
const uint8_t  ADC_SAMPLES         = 16;

// ======================================================
// State Machine
// ======================================================

enum CurtainState { IDLE, OPENING, CLOSING };

CurtainState state = IDLE;

uint32_t moveStart    = 0;
uint32_t moveDuration = 0;

// I use this to estimate curtain position because we do not have any position encoder in this version yet: 0.0 = fully open, 1.0 = fully closed
float curtainPos = 0.0f;

// ======================================================
// Motor Control
// ======================================================

void motorOpen()
{
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);
    digitalWrite(PWMA, HIGH);
}

void motorClose()
{
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
    digitalWrite(PWMA, HIGH);
}

void motorStop()
{
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
    digitalWrite(PWMA, LOW);
}

// ======================================================
// Movement Control (NON-BLOCKING i used the fa usual millis() timing method to avoid blocking the main loop with delay())
// ======================================================

void startOpen(uint32_t ms)
{
    if (curtainPos <= 0.0f) return;
    motorOpen();
    state        = OPENING;
    moveStart    = millis();
    moveDuration = ms;
}

void startClose(uint32_t ms)
{
    if (curtainPos >= 1.0f) return;
    motorClose();
    state        = CLOSING;
    moveStart    = millis();
    moveDuration = ms;
}

void stopMotor()
{
    if (state != IDLE)
    {
        float fraction = (float)(millis() - moveStart) / (float)CURTAIN_TRAVEL_TIME;
        fraction = min(fraction, 1.0f);
        if (state == OPENING) curtainPos = max(curtainPos - fraction, 0.0f);
        if (state == CLOSING) curtainPos = min(curtainPos + fraction, 1.0f);
    }
    motorStop();
    state = IDLE;
}

// ======================================================
// Update Loop (handles auto stop)
// ======================================================

void updateMotor()
{
    if (state == IDLE) return;
    uint32_t elapsed = millis() - moveStart;
    if (elapsed >= moveDuration || elapsed >= MAX_RUN_TIME)
        stopMotor();
}

// ======================================================
// Battery Monitoring (averaged) - readBatteryVoltage() returns the latest voltage reading in volts simple voltage divider is used here 
// ======================================================

float readBatteryVoltage()
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ADC_SAMPLES; i++) sum += analogRead(BATTERY_ADC);
    float adcVoltage = ((float)(sum / ADC_SAMPLES) / 4095.0f) * 3.3f;
    return adcVoltage * BATTERY_CAL;
}

// ======================================================
// Helpers
// ======================================================

String getStateText()
{
    switch (state)
    {
        case OPENING: return "opening";
        case CLOSING: return "closing";
        default:      return "idle";
    }
}

// ======================================================
// Web Page - this is the dashboard HTML served to the browser, embedded as a raw string literal for simplicity. It uses vanilla JS to poll the /status endpoint every 2s and update the UI in real-time. The "ghost drape" animation i made this using claude ai
// ======================================================

const char PAGE[] PROGMEM = R"rawhtml(
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

/* header */
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

/* section label */
.section{
  font-family:var(--mono);font-size:.52rem;letter-spacing:.2em;text-transform:uppercase;
  color:var(--orange);margin-bottom:8px;display:flex;align-items:center;gap:8px;
}
.section::after{content:'';flex:1;height:1px;background:var(--rule);}

/* curtain block */
.curtain-block{border:1px solid var(--ink2);margin-bottom:1.25rem;}
.curtain-head{
  background:var(--ink);color:#f5f4f0;
  font-family:var(--mono);font-size:.58rem;letter-spacing:.18em;
  padding:5px 10px;display:flex;justify-content:space-between;
}
.curtain-head span:last-child{color:var(--orange);}
.curtain-vis{position:relative;height:88px;background:var(--surface);overflow:hidden;}
.curtain-vis::before{content:'';position:absolute;top:0;left:0;right:0;height:3px;background:var(--ink);}
/* real drape — committed firmware position, no transition */
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
/* ghost drape — animated target position while motor is running */
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

/* metrics */
.metrics{display:grid;grid-template-columns:1fr 1fr 1fr;border:1px solid var(--ink2);margin-bottom:1.25rem;}
.metric{padding:.75rem .9rem;border-right:1px solid var(--rule);}
.metric:last-child{border-right:none;}
.m-label{font-family:var(--mono);font-size:.46rem;letter-spacing:.18em;text-transform:uppercase;color:var(--mute);margin-bottom:5px;}
.m-val{font-family:var(--mono);font-size:.92rem;font-weight:700;line-height:1;}
.m-unit{font-family:var(--mono);font-size:.48rem;color:var(--mute);margin-left:1px;}
.bbar-row{margin-top:5px;display:flex;align-items:center;gap:5px;}
.bbar-track{flex:1;height:3px;background:var(--rule2);}
.bbar-fill{height:100%;background:var(--ink);transition:width .6s ease;}

/* controls */
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
/* open & close — orange hover */
.cmd.open:hover,.cmd.close:hover{background:var(--orange);color:#fff;}
.cmd.open:hover svg,.cmd.close:hover svg{stroke:#fff;}
/* stop — ink hover */
.cmd.stop:hover{background:var(--ink);color:#f5f4f0;}
.cmd.stop:hover svg{stroke:#f5f4f0;}

/* log */
.log-block{border:1px solid var(--rule);margin-bottom:1.25rem;}
.log-head{font-family:var(--mono);font-size:.52rem;letter-spacing:.18em;text-transform:uppercase;color:var(--mute);padding:5px 10px;border-bottom:1px solid var(--rule);display:flex;justify-content:space-between;}
#log{padding:8px 10px;font-family:var(--mono);font-size:.58rem;color:var(--mute);line-height:2;min-height:80px;}
.lt{color:var(--rule);margin-right:8px;}
.ok{color:var(--green);}
.er{color:var(--red);}

/* footer */
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
    <div class="ip">192.168.4.1</div>
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

// Curtain rings
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

// ── live state from last poll ──────────────────────────
// pos: 0.0=fully open, 1.0=fully closed
// drape width  = pos*100%        (grows right as curtain closes)
// displayed %  = (1-pos)*100     (100=open, 0=closed)

function posLabel(pos, state){
  if(state==='opening') return 'OPENING';
  if(state==='closing') return 'CLOSING';
  if(pos<=0.01) return 'OPEN';
  if(pos>=0.99) return 'CLOSED';
  return Math.round((1-pos)*100)+'% OPEN';
}

// Apply battery, uptime, state labels from poll snapshot
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

// Commit real drape to firmware position (only called when idle)
function commitDrape(pos){
  const drapeW=Math.round(pos*100);
  const openPct=Math.round((1-pos)*100);
  $('drape').style.width=drapeW+'%';
  $('posval').textContent=openPct+'%';
  $('pct-head').textContent=openPct+'%';
}

// Move ghost drape and update % counter — called every rAF tick
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

// ── rAF ghost loop ────────────────────────────────────
// Ghost mirrors the ESP's motor progress in real time.
// Real drape stays at last known committed position.
// When motor finishes (poll returns idle), ghost hides
// and real drape snaps to the final committed position.
let clientOffset=0;
let snap=null;
let rafId=null;

function rafLoop(){
  if(!snap || snap.state==='idle'){rafId=null;return;}

  // Estimate elapsed ms on ESP since move started
  const espElapsed=(performance.now()-clientOffset)-snap.moveStart;
  const fraction=Math.min(Math.max(espElapsed/snap.moveDur,0),1);

  // Ghost interpolates from snap.position toward end
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

// ── poll every 2s ─────────────────────────────────────
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
      // Motor stopped: commit real drape, hide ghost
      if(rafId){cancelAnimationFrame(rafId);rafId=null;}
      hideGhost();
      commitDrape(d.position);
    } else {
      // Motor running: keep real drape frozen, animate ghost
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

logAdd('system boot','ok');
logAdd('wifi ap: verhobot','ok');
logAdd('webserver :80 ready','ok');
poll();
setInterval(poll,2000);
setInterval(clock,1000);
clock();
</script>
</body>
</html>
)rawhtml";

// ======================================================
// Web Handlers hereeee!
// ======================================================

void handleRoot()
{
    server.send_P(200, "text/html", PAGE);
}

void handleOpen()
{
    startOpen(CURTAIN_TRAVEL_TIME);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleClose()
{
    startClose(CURTAIN_TRAVEL_TIME);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleStop()
{
    stopMotor();
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleStatus()
{
    uint32_t now = millis();

    String json = "{";
    json += "\"state\":\"" + getStateText() + "\",";
    json += "\"position\":"    + String(curtainPos, 3)         + ",";
    json += "\"battery\":"     + String(readBatteryVoltage(), 3) + ",";
    json += "\"uptime\":"      + String(now / 1000)             + ",";
    // These three let the browser interpolate position in real-time:
    json += "\"moveStart\":"   + String(moveStart)              + ",";
    json += "\"moveDur\":"     + String(moveDuration)           + ",";
    json += "\"now\":"         + String(now);
    json += "}";

    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
}

// ======================================================
// Setup
// ======================================================

void setup()
{
    Serial.begin(115200);

    pinMode(AIN1, OUTPUT);
    pinMode(AIN2, OUTPUT);
    pinMode(PWMA, OUTPUT);
    pinMode(STBY, OUTPUT);

    digitalWrite(STBY, HIGH);

    motorStop();

    WiFi.softAP(ssid, password);

    Serial.println();
    Serial.println("================================");
    Serial.println("VerhoBot v0.1.0-beta1 Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("================================");

    server.on("/",       handleRoot);
    server.on("/open",   handleOpen);
    server.on("/close",  handleClose);
    server.on("/stop",   handleStop);
    server.on("/status", handleStatus);

    server.begin();
}

// ======================================================
// Main Loop
// ======================================================

void loop()
{
    server.handleClient();
    updateMotor();
}
