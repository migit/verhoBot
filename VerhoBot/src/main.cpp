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

// Web server (only used in AP mode)
WebServer server(80);

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
}

void startClose(uint32_t ms) {
    if (curtainPos >= 0.99f) return;
    motorClose();
    state = CLOSING;
    moveStart = millis();
    moveDuration = ms;
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
    while (now < 8 * 3600 * 2 && attempts < 30) {  // wait max ~7.5s
        delay(250);
        now = time(nullptr);
        attempts++;
    }
    return (now > 8 * 3600);  // simple sanity check (year > 2020)
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
    
    // Determine next event: if curtain is open (>50%) → schedule close, else open
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
    if (targetTime <= now) targetTime += 86400;  // next day
    
    return (uint64_t)(targetTime - now);
}

void enterDeepSleep() {
    Serial.println("Entering deep sleep...");
    delay(100);  // flush serial

    // Save position to RTC
    rtcCurtainPos = curtainPos;

    // Disable motor driver (save ~2mA)
    motorStop();
    digitalWrite(STBY, LOW);

    // Turn off WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Configure wake-up sources
    // 1. Timer – next scheduled event
    uint64_t sleepSec = calculateSleepSeconds();
    esp_sleep_enable_timer_wakeup(sleepSec * 1000000ULL);

    // 2. GPIO button (active LOW)
    esp_sleep_enable_ext1_wakeup(1ULL << WAKE_BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_LOW);

    Serial.printf("Sleeping for %llu seconds until %02d:%02d\n", sleepSec, 
                  (curtainPos > 0.5f) ? closeHour : openHour,
                  (curtainPos > 0.5f) ? closeMin  : openMin);
    Serial.flush();

    esp_deep_sleep_start();
    // never reached
}

// ============================================================
//  Web Server – Configuration Portal (only used in AP mode)
// ============================================================
const char PAGE_AP[] PROGMEM = R"rawhtml(
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
</body>
</html>
)rawhtml";

void handleRoot() {
    server.send_P(200, "text/html", PAGE_AP);
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
    String response = "<html><body><h2>Saved!</h2><p>Restarting...</p>"
                      "<meta http-equiv='refresh' content='3;url=/'>"
                      "</body></html>";
    server.send(200, "text/html", response);

    delay(500);
    ESP.restart();  // reboot to apply new config
}

void startAPMode() {
    WiFi.softAP("VerhoBot-Setup", "12345678");
    Serial.println("AP started: VerhoBot-Setup");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
    Serial.println("Web server started");
}

// ============================================================
//  Main Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n=== VerhoBot v0.2 (deep_sleep) ===");

    // ---- Pin configuration ----
    pinMode(AIN1, OUTPUT);
    pinMode(AIN2, OUTPUT);
    pinMode(PWMA, OUTPUT);
    pinMode(STBY, OUTPUT);
    digitalWrite(STBY, HIGH);
    motorStop();

    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);

    // ---- Check for factory reset (hold button 3s) ----
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

    // ---- Restore curtain position from RTC ----
    curtainPos = rtcCurtainPos;
    rtcBootCount++;
    Serial.print("Boot #"); Serial.println(rtcBootCount);
    Serial.print("Curtain position: "); Serial.println(curtainPos * 100);

    // ---- Load configuration from Preferences ----
    loadConfig();

    // ---- Determine wake-up cause ----
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // ---- CASE 1: No configuration → start AP ----
    if (!configured) {
        Serial.println("No config found. Starting AP mode.");
        startAPMode();
        return;  // stay in AP mode, loop() will handle web server
    }

    // ---- CASE 2: Woken by button ----
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        Serial.println("Woken by button! Toggling curtain.");
        // Toggle curtain
        if (curtainPos < 0.1f) {
            startClose(CURTAIN_TRAVEL_TIME);
        } else {
            startOpen(CURTAIN_TRAVEL_TIME);
        }
        // We'll process the movement in loop(), then go to sleep
        // No WiFi/NTP needed for button toggle
        return;  // go to loop() to handle movement
    }

    // ---- CASE 3: Woken by timer (scheduled event) ----
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Woken by timer! Connecting to WiFi...");
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(250);
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi connected, syncing NTP...");
            if (syncTime()) {
                Serial.println("NTP synced: " + getTimeString());
                // Determine if we should open or close based on current time vs schedule
                time_t now = time(nullptr);
                struct tm *tm_now = localtime(&now);
                int currentMin = tm_now->tm_hour * 60 + tm_now->tm_min;
                int openMinTotal = openHour * 60 + openMin;
                int closeMinTotal = closeHour * 60 + closeMin;

                // If current time is between open and close → curtain should be open
                bool shouldBeOpen = (currentMin >= openMinTotal && currentMin < closeMinTotal);
                if (shouldBeOpen && curtainPos > 0.1f) {
                    startOpen(CURTAIN_TRAVEL_TIME);
                } else if (!shouldBeOpen && curtainPos < 0.9f) {
                    startClose(CURTAIN_TRAVEL_TIME);
                } else {
                    Serial.println("Curtain already in correct position.");
                }
            } else {
                Serial.println("NTP sync failed. Will retry next wake.");
            }
        } else {
            Serial.println("WiFi connection failed. Going back to sleep.");
        }
        // Even if WiFi/NTP failed, we might still want to move? We'll skip to avoid wrong timing.
        // The device will sleep again, and the timer will retry at next event.
        return;  // go to loop() to handle movement (if any) then sleep
    }

    // ---- CASE 4: Normal boot (after restart or power‑on) ----
    Serial.println("Normal boot. Connecting to WiFi...");
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(250);
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected, syncing NTP...");
        if (syncTime()) {
            Serial.println("NTP synced: " + getTimeString());
        } else {
            Serial.println("NTP sync failed.");
        }
    } else {
        Serial.println("WiFi connection failed. Will retry at next wake.");
    }
    // After normal boot, we just go to sleep immediately (no movement)
    // The timer will handle scheduled moves.
}

// ============================================================
//  Main Loop
// ============================================================
void loop() {
    // If we are in AP mode, handle web requests
    if (WiFi.getMode() == WIFI_AP) {
        server.handleClient();
        // AP mode stays awake indefinitely until user configures
        return;
    }

    // Otherwise (STA mode) – handle motor movement
    updateMotor();

    // If motor is idle, go to deep sleep
    if (state == IDLE) {
        // Wait a moment to ensure any serial logs flush
        delay(100);
        enterDeepSleep();
    }
    // If motor is running, we stay awake and loop again
}
