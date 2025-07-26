/*
 * Cardiac Monitor for Arduino Uno - Optimized
 * Uses MCUFRIEND_kbv for 8-bit parallel TFT
 * 
 * Hardware:
 * - Arduino Uno
 * - MAX30102 Sensor
 * - MCUFRIEND TFT
 * - XPT2046 Touch
 * - Buzzer
 * 
 * WARNING: Educational use only. Not for medical use.
 * Remove: C:\Users\USER\OneDrive\Documents\Arduino\libraries\ThingPulse_XPT2046_Touch
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <XPT2046_Touchscreen.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <avr/pgmspace.h>

#ifndef TFT_GRAY
#define TFT_GRAY 0x7BEF
#endif

// Pins
#define TOUCH_CS 7
#define BATTERY_PIN A0
#define BUZZER_PIN 6

// Config
#define SENSOR_INT 100
#define DISP_INT 100
#define BUF_SIZE 25
#define FINGER_THR 50000
#define MAX_ALERT 3
#define MAX_LOG 3
#define MAX_STR 20

// Data Structures
struct VitalSigns {
    uint8_t hr;
    uint8_t spo2;
    uint8_t bat;
    uint8_t finger : 1;
    uint32_t ts;
};

#define ALERT_INFO 0
#define ALERT_WARN 1
#define ALERT_CRIT 2

struct Alert {
    uint8_t level;
    char msg[MAX_STR];
    uint32_t ts;
    uint8_t ack : 1;
};

#define ST_INIT 0
#define ST_RUN 1
#define SCR_MAIN 0

// Objects
MCUFRIEND_kbv tft;
XPT2046_Touchscreen ts(TOUCH_CS);
MAX30105 sensor;

// Variables
uint8_t state = ST_INIT;
uint8_t screen = SCR_MAIN;
VitalSigns vitals;
uint32_t lastSensor = 0, lastDisp = 0, lastAlert = 0, lastAlertTime = 0;
uint32_t irBuf[BUF_SIZE], redBuf[BUF_SIZE];
int32_t bufLen = BUF_SIZE, spo2, hr;
int8_t validSpo2, validHr;
uint8_t bufIdx = 0, alertCnt = 0, alertLogCnt = 0;
bool finger = false;
Alert alerts[MAX_ALERT], alertLog[MAX_LOG];
const uint32_t ALERT_CD = 5000;

// Prototypes
bool initDisp();
void showSplash();
void showMain();
void drawVitals();
void updVitals();
void showErr(const __FlashStringHelper* t, const __FlashStringHelper* m);
bool initSensor();
void updSensors();
void handleTouch();
void chkAlerts();
void trigAlert(uint8_t lvl, const char* msg);
void playAlert(uint8_t lvl);
void showAlert(const char* msg, uint8_t lvl);
void rmAlerts();
void updDisp();
void chkMem();

// Setup
void setup() {
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BATTERY_PIN, INPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    if (!initDisp()) while (true) delay(1000);
    
    showSplash();
    delay(1000);
    
    if (!initSensor()) {
        showErr(F("Err"), F("Sensor Fail"));
        delay(5000);
    }
    
    state = ST_RUN;
    screen = SCR_MAIN;
    showMain();
}

// Loop
void loop() {
    uint32_t now = millis();
    
    handleTouch();
    
    if (now - lastSensor >= SENSOR_INT) {
        lastSensor = now;
        updSensors();
    }
    
    if (now - lastDisp >= DISP_INT) {
        lastDisp = now;
        updDisp();
    }
    
    if (now - lastAlert >= 1000) {
        lastAlert = now;
        chkAlerts();
    }
    
    chkMem();
    delay(10);
}

// Display
bool initDisp() {
    uint16_t ID = tft.readID();
    if (ID == 0xD3D3) ID = 0x9341;
    tft.begin(ID);
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    if (!ts.begin()) return false;
    ts.setRotation(1);
    return true;
}

void showSplash() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(F("Monitor"), 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((240 - w) / 2, 60);
    tft.println(F("Monitor"));
    tft.setCursor(10, 200);
    tft.println(F("Med Use"));
}

void showMain() {
    screen = SCR_MAIN;
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 240, 20, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 5);
    tft.println(F("Monitor"));
    drawVitals();
}

void drawVitals() {
    tft.drawRect(10, 30, 100, 60, TFT_WHITE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(15, 35);
    tft.println(F("HR"));
    tft.drawRect(130, 30, 100, 60, TFT_WHITE);
    tft.setCursor(135, 35);
    tft.println(F("SpO2"));
}

void updVitals() {
    tft.fillRect(15, 50, 90, 30, TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(20, 55);
    char buf[8];
    if (vitals.finger && vitals.hr > 0) {
        snprintf(buf, sizeof(buf), "%d", vitals.hr);
    } else {
        strcpy(buf, "--");
    }
    tft.print(buf);
    
    tft.fillRect(135, 50, 90, 30, TFT_BLACK);
    tft.setTextColor(TFT_BLUE);
    tft.setCursor(140, 55);
    if (vitals.finger && vitals.spo2 > 0) {
        snprintf(buf, sizeof(buf), "%d", vitals.spo2);
    } else {
        strcpy(buf, "--");
    }
    tft.print(buf);
    
    tft.setTextSize(1);
    tft.setTextColor(vitals.finger ? TFT_GREEN : TFT_RED);
    tft.fillRect(15, 95, 100, 10, TFT_BLACK);
    tft.setCursor(15, 95);
    tft.println(vitals.finger ? F("OK") : F("Finger"));
}

void showErr(const __FlashStringHelper* t, const __FlashStringHelper* m) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(1);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((240 - w) / 2, 60);
    tft.println(t);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 100);
    tft.println(m);
}

// Sensor
bool initSensor() {
    Wire.begin();
    if (!sensor.begin()) return false;
    sensor.setup(0x1F, 4, 2, 100, 411, 4096);
    sensor.setPulseAmplitudeRed(0x0A);
    sensor.setPulseAmplitudeGreen(0);
    return true;
}

void updSensors() {
    vitals.bat = (analogRead(BATTERY_PIN) / 1023.0 * 5.0 - 3.0) / 1.2 * 100;
    vitals.bat = vitals.bat > 100 ? 100 : vitals.bat < 0 ? 0 : vitals.bat;
    vitals.ts = millis();
    
    if (sensor.available()) {
        redBuf[bufIdx] = sensor.getRed();
        irBuf[bufIdx] = sensor.getIR();
        finger = (irBuf[bufIdx] > FINGER_THR);
        vitals.finger = finger;
        bufIdx++;
        if (bufIdx >= BUF_SIZE) {
            bufIdx = 0;
            if (finger) {
                maxim_heart_rate_and_oxygen_saturation(
                    (uint16_t*)irBuf, bufLen, (uint16_t*)redBuf, &spo2, &validSpo2, &hr, &validHr
                );
                if (validHr && hr > 0 && hr < 200) vitals.hr = hr;
                if (validSpo2 && spo2 > 0 && spo2 <= 100) vitals.spo2 = spo2;
            } else {
                vitals.hr = 0;
                vitals.spo2 = 0;
            }
        }
        sensor.nextSample();
    }
}

// Touch
void handleTouch() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        int x = map(p.x, 200, 3700, 0, 240);
        int y = map(p.y, 240, 3800, 0, 320);
        delay(200);
    }
}

// Alerts
void chkAlerts() {
    char msg[MAX_STR];
    if (vitals.finger && vitals.hr > 0) {
        if (vitals.hr < 60 || vitals.hr > 100) {
            snprintf(msg, sizeof(msg), "HR:%d", vitals.hr);
            uint8_t lvl = (vitals.hr < 50 || vitals.hr > 120) ? ALERT_CRIT : ALERT_WARN;
            trigAlert(lvl, msg);
        }
    }
    if (vitals.finger && vitals.spo2 > 0) {
        if (vitals.spo2 < 95) {
            snprintf(msg, sizeof(msg), "SpO2:%d", vitals.spo2);
            uint8_t lvl = (vitals.spo2 < 90) ? ALERT_CRIT : ALERT_WARN;
            trigAlert(lvl, msg);
        }
    }
    if (vitals.bat < 20) {
        snprintf(msg, sizeof(msg), "Bat:%d", vitals.bat);
        uint8_t lvl = (vitals.bat < 10) ? ALERT_CRIT : ALERT_WARN;
        trigAlert(lvl, msg);
    }
    rmAlerts();
}

void trigAlert(uint8_t lvl, const char* msg) {
    if (millis() - lastAlertTime < ALERT_CD) return;
    if (alertCnt < MAX_ALERT) {
        alerts[alertCnt].level = lvl;
        strncpy(alerts[alertCnt].msg, msg, MAX_STR);
        alerts[alertCnt].ts = millis();
        alerts[alertCnt].ack = 0;
        alertCnt++;
    }
    if (alertLogCnt < MAX_LOG) {
        alertLog[alertLogCnt].level = lvl;
        strncpy(alertLog[alertLogCnt].msg, msg, MAX_STR);
        alertLog[alertLogCnt].ts = millis();
        alertLog[alertLogCnt].ack = 0;
        alertLogCnt++;
    } else {
        memmove(alertLog, alertLog + 1, (MAX_LOG - 1) * sizeof(Alert));
        alertLog[MAX_LOG - 1].level = lvl;
        strncpy(alertLog[MAX_LOG - 1].msg, msg, MAX_STR);
        alertLog[MAX_LOG - 1].ts = millis();
        alertLog[MAX_LOG - 1].ack = 0;
    }
    lastAlertTime = millis();
    playAlert(lvl);
    showAlert(msg, lvl);
}

void playAlert(uint8_t lvl) {
    uint8_t cnt = lvl == ALERT_CRIT ? 3 : lvl == ALERT_WARN ? 2 : 1;
    uint16_t dur = lvl == ALERT_CRIT ? 100 : 200;
    for (uint8_t i = 0; i < cnt; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(dur);
        digitalWrite(BUZZER_PIN, LOW);
        if (i < cnt - 1) delay(100);
    }
}

void showAlert(const char* msg, uint8_t lvl) {
    uint16_t c = lvl == ALERT_CRIT ? TFT_RED : lvl == ALERT_WARN ? TFT_ORANGE : TFT_YELLOW;
    tft.fillRect(0, 20, 240, 10, c);
    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(5, 22);
    tft.println(msg);
    static uint32_t alertTime = millis();
    if (millis() - alertTime > 5000) {
        tft.fillRect(0, 20, 240, 10, TFT_BLACK);
        alertTime = millis();
    }
}

void rmAlerts() {
    uint32_t now = millis();
    uint8_t newCnt = 0;
    for (uint8_t i = 0; i < alertCnt; i++) {
        if (!alerts[i].ack && (now - alerts[i].ts <= 30000)) {
            alerts[newCnt++] = alerts[i];
        }
    }
    alertCnt = newCnt;
}

// Display
void updDisp() {
    if (screen == SCR_MAIN) {
        updVitals();
        uint8_t bat = vitals.bat;
        tft.setTextSize(1);
        tft.setTextColor(bat > 20 ? TFT_GREEN : TFT_RED);
        tft.setCursor(200, 10);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", bat);
        tft.println(buf);
    }
}

// Memory
void chkMem() {
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        lastCheck = millis();
        int mem = freeMemory();
        if (mem < 100) {
            if (alertLogCnt > 1) {
                memmove(alertLog, alertLog + 1, (alertLogCnt - 1) * sizeof(Alert));
                alertLogCnt--;
            }
        }
    }
}

int freeMemory() {
    char top;
    extern char *__brkval;
    return __brkval ? &top - __brkval : &top - __malloc_heap_start;
}