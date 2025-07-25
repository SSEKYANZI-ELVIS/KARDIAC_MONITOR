/*
 * Cardiac Monitor for Arduino Uno - Complete System
 * Modified for 8-bit parallel TFT using MCUFRIEND_kbv library
 * 
 * Hardware Requirements:
 * - Arduino Uno
 * - MAX30102 Heart Rate/SpO2 Sensor
 * - 8-bit parallel TFT Display (MCUFRIEND compatible)
 * - XPT2046 Touch Controller
 * - Buzzer
 * 
 * WARNING: This is for educational purposes only. Not for medical use.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <XPT2046_Touchscreen.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"

// ==================== PIN DEFINITIONS ====================
// TFT pins are defined internally by MCUFRIEND library
#define TOUCH_CS  7
#define TOUCH_IRQ 255  // Not used

#define SDA_PIN   A4
#define SCL_PIN   A5

#define BATTERY_PIN A0
#define BUZZER_PIN  6

// ==================== COLOR DEFINITIONS ====================
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF
#define GRAY    0x7BEF
#define DARKGRAY 0x39E7
#define ORANGE  0xFD20
#define PURPLE  0x780F

// ==================== CONFIGURATION ====================
#define FIRMWARE_VERSION "1.0.0"
#define DEVICE_NAME "CardiacMonitor"
#define SENSOR_UPDATE_INTERVAL 100  // ms
#define DISPLAY_UPDATE_INTERVAL 100 // ms
#define DATA_LOG_INTERVAL 1000      // ms

#define SAMPLE_RATE 100
#define BUFFER_SIZE 100  // Reduced for Uno memory
#define FINGER_THRESHOLD 50000
#define SPO2_BUFFER_SIZE 50

#define MAX_ALERTS 5    // Reduced for Uno memory
#define MAX_HISTORY 20  // Reduced for Uno memory
#define DATA_BUFFER_SIZE 50  // Reduced for Uno memory
#define MAX_STRING 64   // Reduced for Uno memory

// ==================== DATA STRUCTURES ====================
typedef struct {
    float heartRateMin;
    float heartRateMax;
    float spO2Min;
    float batteryMin;
    bool enabled;
} AlertThresholds;

typedef struct {
    float heartRate;
    float spO2;
    float batteryLevel;
    bool isFingerDetected;
    unsigned long timestamp;
} VitalSigns;

#define ALERT_LEVEL_INFO 0
#define ALERT_LEVEL_WARNING 1
#define ALERT_LEVEL_CRITICAL 2

typedef struct {
    int level;
    char message[MAX_STRING];
    unsigned long timestamp;
    bool acknowledged;
} Alert;

#define SYSTEM_STATE_INITIALIZING 0
#define SYSTEM_STATE_RUNNING 1
#define SYSTEM_STATE_SETTINGS 2
#define SYSTEM_STATE_HISTORY 3
#define SYSTEM_STATE_ERROR 4
#define SYSTEM_STATE_SLEEP 5

#define SCREEN_TYPE_MAIN 0
#define SCREEN_TYPE_SETTINGS 1
#define SCREEN_TYPE_HISTORY 2

// ==================== GLOBAL OBJECTS ====================
MCUFRIEND_kbv tft;
XPT2046_Touchscreen ts(TOUCH_CS);
MAX30105 particleSensor;

// ==================== GLOBAL VARIABLES ====================
int currentState = SYSTEM_STATE_INITIALIZING;
int currentScreen = SCREEN_TYPE_MAIN;
VitalSigns currentVitals;
AlertThresholds alertThresholds;

unsigned long lastSensorUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastDataLog = 0;
unsigned long lastAlertCheck = 0;

uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
int32_t bufferLength = BUFFER_SIZE;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;
int bufferIndex = 0;
bool fingerDetected = false;

int screenBrightness = 128;
bool displayOn = true;
unsigned long lastTouchTime = 0;
const unsigned long SCREEN_TIMEOUT = 30000;

Alert activeAlerts[MAX_ALERTS];
int activeAlertsCount = 0;
Alert alertHistory[MAX_HISTORY];
int alertHistoryCount = 0;
unsigned long lastAlertTime = 0;
const unsigned long ALERT_COOLDOWN = 5000;

VitalSigns dataBuffer[DATA_BUFFER_SIZE];
int dataBufferCount = 0;

// Simulated preferences for Uno
typedef struct {
    float hr_min;
    float hr_max;
    float spo2_min;
    float bat_min;
    bool alerts_en;
    int brightness;
} Preferences;

Preferences preferences;

// ==================== FUNCTION PROTOTYPES ====================
bool initializeDisplay(void);
void showSplashScreen(void);
void showMainScreen(void);
void drawStatusBar(void);
void drawVitalSignsLayout(void);
void drawMainButtons(void);
void updateVitalSigns(void);
void drawWaveform(void);
void showError(const char* title, const char* message);
void showSettingsScreen(void);
void showHistoryScreen(void);
void drawHeart(int x, int y, uint16_t color);
bool initializeSensor(void);
void updateSensors(void);
float readBatteryLevel(void);
void handleTouch(void);
void handleTouchEvent(int x, int y);
void handleMainScreenTouch(int x, int y);
void handleSettingsScreenTouch(int x, int y);
void handleHistoryScreenTouch(int x, int y);
void handleScreenTimeout(void);
void logData(void);
void checkAlerts(void);
void triggerAlert(int level, const char* message);
void playAlertSound(int level);
void showAlert(const char* message, int level);
void removeOldAlerts(void);
void loadSettings(void);
void saveSettings(void);
void updateDisplay(void);
void formatTime(unsigned long timestamp, char* output);
void printSystemInfo(void);
void performSelfTest(void);
void handleSerialCommands(void);
void handleLowPowerMode(void);
void checkMemoryUsage(void);
void initializeSystem(void);
void handleSystemError(const char* errorMessage);

// ==================== SETUP FUNCTION ====================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Cardiac Monitor v1.0 ===");
    Serial.println("Initializing system...");
    
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BATTERY_PIN, INPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    loadSettings();
    
    if (!initializeDisplay()) {
        Serial.println("FATAL: Display initialization failed");
        while (true) delay(1000);
    }
    
    showSplashScreen();
    delay(2000);
    
    if (!initializeSensor()) {
        showError("Sensor Error", "Failed to initialize MAX30102");
        delay(5000);
    }
    
    currentState = SYSTEM_STATE_RUNNING;
    currentScreen = SCREEN_TYPE_MAIN;
    showMainScreen();
    
    Serial.println("System initialization complete");
    Serial.println("=================================");
}

// ==================== MAIN LOOP ====================
void loop() {
    unsigned long currentTime = millis();
    
    handleTouch();
    
    if (currentTime - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        lastSensorUpdate = currentTime;
        updateSensors();
    }
    
    if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        lastDisplayUpdate = currentTime;
        updateDisplay();
    }
    
    if (currentTime - lastDataLog >= DATA_LOG_INTERVAL) {
        lastDataLog = currentTime;
        logData();
    }
    
    if (currentTime - lastAlertCheck >= 1000) {
        lastAlertCheck = currentTime;
        checkAlerts();
    }
    
    handleScreenTimeout();
    
    handleSerialCommands();
    checkMemoryUsage();
    handleLowPowerMode();
    
    delay(10);
}

// ==================== DISPLAY FUNCTIONS ====================
bool initializeDisplay() {
    Serial.println("Initializing display...");
    
    uint16_t ID = tft.readID();
    if (ID == 0xD3D3) {  // Common value for ILI9341
        ID = 0x9341;     // Force ID if display not properly detected
    }
    
    tft.begin(ID);
    tft.setRotation(1);
    tft.fillScreen(BLACK);
    
    if (!ts.begin()) {
        Serial.println("Touch screen initialization failed");
        return false;
    }
    
    ts.setRotation(1);
    
    Serial.println("Display initialized successfully");
    return true;
}

void showSplashScreen() {
    tft.fillScreen(BLACK);
    tft.setTextColor(WHITE);
    tft.setTextSize(3);
    
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds("Cardiac Monitor", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((240 - w) / 2, 60);
    tft.println("Cardiac Monitor");
    
    tft.setTextSize(2);
    tft.getTextBounds("v1.0", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((240 - w) / 2, 100);
    tft.println("v1.0");
    
    drawHeart(120, 140, RED);
    
    tft.setTextSize(1);
    tft.setTextColor(GRAY);
    tft.setCursor(10, 200);
    tft.println("For Professional use only");
    tft.setCursor(30, 215);
    tft.println("Used for medical diagnosis");
}

void showMainScreen() {
    currentScreen = SCREEN_TYPE_MAIN;
    tft.fillScreen(BLACK);
    
    tft.fillRect(0, 0, 240, 30, BLUE);
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 8);
    tft.println("Cardiac Monitor");
    
    drawStatusBar();
    drawVitalSignsLayout();
    drawMainButtons();
}

void drawStatusBar() {
    tft.setTextSize(1);
    tft.setTextColor(WHITE);
    tft.setCursor(180, 10);
    
    float batteryLevel = readBatteryLevel();
    uint16_t batteryColor = batteryLevel > 20 ? GREEN : RED;
    tft.setTextColor(batteryColor);
    tft.setCursor(200, 20);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)batteryLevel);
    tft.println(buf);
}

void drawVitalSignsLayout() {
    tft.drawRect(10, 40, 100, 80, WHITE);
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.setCursor(15, 45);
    tft.println("Heart Rate");
    tft.setCursor(30, 55);
    tft.println("(BPM)");
    
    tft.drawRect(130, 40, 100, 80, WHITE);
    tft.setCursor(150, 45);
    tft.println("SpO2 (%)");
    
    tft.drawRect(10, 130, 220, 60, WHITE);
    tft.setCursor(15, 135);
    tft.println("Waveform");
}

void drawMainButtons() {
    tft.fillRect(10, 200, 60, 30, GRAY);
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.setCursor(20, 212);
    tft.println("Settings");
    
    tft.fillRect(90, 200, 60, 30, GRAY);
    tft.setCursor(100, 212);
    tft.println("History");
    
    tft.fillRect(170, 200, 60, 30, GRAY);
    tft.setCursor(180, 212);
    tft.println("Info");
}

void updateVitalSigns() {
    tft.fillRect(15, 60, 90, 50, BLACK);
    tft.setTextColor(RED);
    tft.setTextSize(3);
    tft.setCursor(20, 70);
    char buf[16];
    if (currentVitals.isFingerDetected && currentVitals.heartRate > 0) {
        snprintf(buf, sizeof(buf), "%d", (int)currentVitals.heartRate);
    } else {
        strcpy(buf, "--");
    }
    tft.print(buf);
    
    tft.fillRect(135, 60, 90, 50, BLACK);
    tft.setTextColor(BLUE);
    tft.setCursor(140, 70);
    if (currentVitals.isFingerDetected && currentVitals.spO2 > 0) {
        snprintf(buf, sizeof(buf), "%d", (int)currentVitals.spO2);
    } else {
        strcpy(buf, "--");
    }
    tft.print(buf);
    
    tft.setTextSize(1);
    tft.setTextColor(currentVitals.isFingerDetected ? GREEN : RED);
    tft.setCursor(15, 105);
    tft.fillRect(15, 105, 100, 10, BLACK);
    tft.println(currentVitals.isFingerDetected ? "Finger detected" : "Place finger");
}

void drawWaveform() {
    static int waveformX = 15;
    static int lastY = 160;
    
    tft.drawPixel(waveformX, lastY, BLACK);
    
    if (currentVitals.isFingerDetected) {
        int waveY = 160 + (int)(sin(millis() * 0.01) * 20);
        tft.drawPixel(waveformX, waveY, GREEN);
        lastY = waveY;
    }
    
    waveformX++;
    if (waveformX > 225) {
        waveformX = 15;
        tft.fillRect(15, 140, 210, 45, BLACK);
    }
}

void showError(const char* title, const char* message) {
    tft.fillScreen(BLACK);
    tft.setTextColor(RED);
    tft.setTextSize(2);
    
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((240 - w) / 2, 60);
    tft.println(title);
    
    tft.setTextSize(1);
    tft.setTextColor(WHITE);
    tft.setCursor(10, 100);
    tft.println(message);
}

void showSettingsScreen() {
    currentScreen = SCREEN_TYPE_SETTINGS;
    tft.fillScreen(COLOR_BLACK);
    
    tft.fillRect(0, 0, 240, 30, COLOR_BLUE);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 8);
    tft.println("Settings");
    
    tft.fillRect(200, 5, 40, 20, COLOR_GRAY);
    tft.setTextSize(1);
    tft.setCursor(210, 10);
    tft.println("Back");
    
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    
    tft.setCursor(10, 50);
    tft.println("Alert Thresholds:");
    
    char buf[32];
    tft.setCursor(20, 70);
    snprintf(buf, sizeof(buf), "HR: %d - %d BPM", 
             (int)alertThresholds.heartRateMin, (int)alertThresholds.heartRateMax);
    tft.println(buf);
    
    tft.setCursor(20, 90);
    snprintf(buf, sizeof(buf), "SpO2 Min: %d%%", (int)alertThresholds.spO2Min);
    tft.println(buf);
    
    tft.setCursor(20, 110);
    snprintf(buf, sizeof(buf), "Battery Min: %d%%", (int)alertThresholds.batteryMin);
    tft.println(buf);
    
    tft.setCursor(10, 140);
    snprintf(buf, sizeof(buf), "Brightness: %d", screenBrightness);
    tft.println(buf);
    
    tft.fillRect(10, 170, 100, 30, COLOR_GREEN);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(20, 182);
    tft.println("Export");
    
    tft.fillRect(130, 170, 100, 30, COLOR_RED);
    tft.setCursor(150, 182);
    tft.println("Clear");
}

void showHistoryScreen() {
    currentScreen = SCREEN_TYPE_HISTORY;
    tft.fillScreen(COLOR_BLACK);
    
    tft.fillRect(0, 0, 240, 30, COLOR_BLUE);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 8);
    tft.println("Data History");
    
    tft.fillRect(200, 5, 40, 20, COLOR_GRAY);
    tft.setTextSize(1);
    tft.setCursor(210, 10);
    tft.println("Back");
    
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.println("Recent Readings:");
    
    int y = 60;
    int count = dataBufferCount < 6 ? dataBufferCount : 6;
    
    for (int i = dataBufferCount - count; i < dataBufferCount && y < 200; i++) {
        if (i >= 0) {
            VitalSigns* data = &dataBuffer[i];
            char timeStr[16];
            formatTime(data->timestamp, timeStr);
            char buf[64];
            snprintf(buf, sizeof(buf), "%s HR:%d", timeStr, (int)data->heartRate);
            tft.setCursor(10, y);
            tft.println(buf);
            y += 15;
            
            snprintf(buf, sizeof(buf), "SpO2:%d Bat:%d%%", (int)data->spO2, (int)data->batteryLevel);
            tft.setCursor(10, y);
            tft.println(buf);
            y += 15;
        }
    }
    
    if (dataBufferCount == 0) {
        tft.setCursor(10, 60);
        tft.println("No data available");
    }
}
void drawHeart(int x, int y, uint16_t color) {
    tft.fillCircle(x - 8, y - 5, 8, color);
    tft.fillCircle(x + 8, y - 5, 8, color);
    tft.fillTriangle(x - 15, y, x + 15, y, x, y + 15, color);
}

// ==================== SENSOR FUNCTIONS ====================
bool initializeSensor() {
    Serial.println("Initializing MAX30102 sensor...");
    
    Wire.begin();
    
    if (!particleSensor.begin()) {
        Serial.println("MAX30102 not found");
        return false;
    }
    
    // Setup with default settings
    byte ledBrightness = 0x1F; // Options: 0=Off to 255=50mA
    byte sampleAverage = 4; // Options: 1, 2, 4, 8, 16, 32
    byte ledMode = 2; // Options: 1 = Red only, 2 = Red + IR
    int sampleRate = 100; // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
    int pulseWidth = 411; // Options: 69, 118, 215, 411
    int adcRange = 4096; // Options: 2048, 4096, 8192, 16384
    
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
    
    Serial.println("MAX30102 initialized successfully");
    return true;
}

void updateSensors() {
    currentVitals.batteryLevel = readBatteryLevel();
    currentVitals.timestamp = millis();
    
    if (particleSensor.available()) {
        redBuffer[bufferIndex] = particleSensor.getRed();
        irBuffer[bufferIndex] = particleSensor.getIR();
        
        fingerDetected = (irBuffer[bufferIndex] > FINGER_THRESHOLD);
        currentVitals.isFingerDetected = fingerDetected;
        
        bufferIndex++;
        
        if (bufferIndex >= BUFFER_SIZE) {
            bufferIndex = 0;
            
            if (fingerDetected) {
                maxim_heart_rate_and_oxygen_saturation(
                    irBuffer, bufferLength, redBuffer,
                    &spo2, &validSPO2, &heartRate, &validHeartRate
                );
                
                if (validHeartRate && heartRate > 0 && heartRate < 200) {
                    currentVitals.heartRate = heartRate;
                }
                
                if (validSPO2 && spo2 > 0 && spo2 <= 100) {
                    currentVitals.spO2 = spo2;
                }
            } else {
                currentVitals.heartRate = 0;
                currentVitals.spO2 = 0;
            }
        }
        
        particleSensor.nextSample();
    }
}

float readBatteryLevel() {
    int rawValue = analogRead(BATTERY_PIN);
    float voltage = (rawValue / 1023.0) * 5.0;
    float percentage = ((voltage - 3.0) / 1.2) * 100.0;
    return percentage < 0 ? 0 : (percentage > 100 ? 100 : percentage);
}

// ==================== TOUCH HANDLING ====================
void handleTouch() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        
        // Map touch coordinates to screen coordinates
        int x = map(p.x, 200, 3700, 0, 240);
        int y = map(p.y, 240, 3800, 0, 320);
        
        lastTouchTime = millis();
        
        if (!displayOn) {
            displayOn = true;
            return;
        }
        
        handleTouchEvent(x, y);
        
        delay(200);
    }
}

void handleTouchEvent(int x, int y) {
    switch (currentScreen) {
        case SCREEN_TYPE_MAIN:
            handleMainScreenTouch(x, y);
            break;
        case SCREEN_TYPE_SETTINGS:
            handleSettingsScreenTouch(x, y);
            break;
        case SCREEN_TYPE_HISTORY:
            handleHistoryScreenTouch(x, y);
            break;
    }
}

void handleMainScreenTouch(int x, int y) {
    if (x >= 10 && x <= 70 && y >= 200 && y <= 230) {
        showSettingsScreen();
    }
    else if (x >= 90 && x <= 150 && y >= 200 && y <= 230) {
        showHistoryScreen();
    }
    else if (x >= 170 && x <= 230 && y >= 200 && y <= 230) {
        printSystemInfo();
    }
}

void handleSettingsScreenTouch(int x, int y) {
    if (x >= 200 && x <= 240 && y >= 5 && y <= 25) {
        showMainScreen();
    }
    else if (x >= 10 && x <= 110 && y >= 170 && y <= 200) {
        exportData();
    }
    else if (x >= 130 && x <= 230 && y >= 170 && y <= 200) {
        clearData();
    }
}

void handleHistoryScreenTouch(int x, int y) {
    if (x >= 200 && x <= 240 && y >= 5 && y <= 25) {
        showMainScreen();
    }
}

void handleScreenTimeout() {
    if (displayOn && (millis() - lastTouchTime > SCREEN_TIMEOUT)) {
        displayOn = false;
        tft.fillScreen(COLOR_BLACK);
    }
}

// ==================== DATA LOGGING ====================
void logData() {
    if (currentVitals.isFingerDetected && currentVitals.heartRate > 0) {
        if (dataBufferCount < DATA_BUFFER_SIZE) {
            dataBuffer[dataBufferCount++] = currentVitals;
        } else {
            memmove(dataBuffer, dataBuffer + 1, (DATA_BUFFER_SIZE - 1) * sizeof(VitalSigns));
            dataBuffer[DATA_BUFFER_SIZE - 1] = currentVitals;
        }
    }
}

// ==================== ALERT SYSTEM ====================
void checkAlerts() {
    if (!alertThresholds.enabled) return;
    
    char message[MAX_STRING];
    if (currentVitals.isFingerDetected && currentVitals.heartRate > 0) {
        if (currentVitals.heartRate < alertThresholds.heartRateMin || 
            currentVitals.heartRate > alertThresholds.heartRateMax) {
            snprintf(message, sizeof(message), "HR: %d BPM", (int)currentVitals.heartRate);
            int level = (currentVitals.heartRate < 50 || currentVitals.heartRate > 120) ? 
                        ALERT_LEVEL_CRITICAL : ALERT_LEVEL_WARNING;
            triggerAlert(level, message);
        }
    }
    
    if (currentVitals.isFingerDetected && currentVitals.spO2 > 0) {
        if (currentVitals.spO2 < alertThresholds.spO2Min) {
            snprintf(message, sizeof(message), "Low SpO2: %d%%", (int)currentVitals.spO2);
            int level = (currentVitals.spO2 < 90) ? ALERT_LEVEL_CRITICAL : ALERT_LEVEL_WARNING;
            triggerAlert(level, message);
        }
    }
    
    if (currentVitals.batteryLevel < alertThresholds.batteryMin) {
        snprintf(message, sizeof(message), "Low battery: %d%%", (int)currentVitals.batteryLevel);
        int level = (currentVitals.batteryLevel < 10) ? ALERT_LEVEL_CRITICAL : ALERT_LEVEL_WARNING;
        triggerAlert(level, message);
    }
    
    removeOldAlerts();
}

void triggerAlert(int level, const char* message) {
    if (millis() - lastAlertTime < ALERT_COOLDOWN) return;
    
    if (activeAlertsCount < MAX_ALERTS) {
        Alert alert;
        alert.level = level;
        strncpy(alert.message, message, MAX_STRING);
        alert.timestamp = millis();
        alert.acknowledged = false;
        activeAlerts[activeAlertsCount++] = alert;
    }
    
    if (alertHistoryCount < MAX_HISTORY) {
        Alert alert;
        alert.level = level;
        strncpy(alert.message, message, MAX_STRING);
        alert.timestamp = millis();
        alert.acknowledged = false;
        alertHistory[alertHistoryCount++] = alert;
    } else {
        memmove(alertHistory, alertHistory + 1, (MAX_HISTORY - 1) * sizeof(Alert));
        Alert alert;
        alert.level = level;
        strncpy(alert.message, message, MAX_STRING);
        alert.timestamp = millis();
        alert.acknowledged = false;
        alertHistory[MAX_HISTORY - 1] = alert;
    }
    
    lastAlertTime = millis();
    
    playAlertSound(level);
    showAlert(message, level);
    
    Serial.print("ALERT [");
    Serial.print(level == ALERT_LEVEL_CRITICAL ? "CRITICAL" : 
                level == ALERT_LEVEL_WARNING ? "WARNING" : "INFO");
    Serial.print("]: ");
    Serial.println(message);
}

void playAlertSound(int level) {
    int beepCount = 1;
    int beepDuration = 200;
    
    switch (level) {
        case ALERT_LEVEL_CRITICAL:
            beepCount = 3;
            beepDuration = 500;
            break;
        case ALERT_LEVEL_WARNING:
            beepCount = 2;
            beepDuration = 300;
            break;
        default:
            beepCount = 1;
            beepDuration = 200;
            break;
    }
    
    for (int i = 0; i < beepCount; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(beepDuration);
        digitalWrite(BUZZER_PIN, LOW);
        if (i < beepCount - 1) delay(200);
    }
}

void showAlert(const char* message, int level) {
    uint16_t alertColor = COLOR_YELLOW;
    switch (level) {
        case ALERT_LEVEL_CRITICAL:
            alertColor = COLOR_RED;
            break;
        case ALERT_LEVEL_WARNING:
            alertColor = COLOR_ORANGE;
            break;
        default:
            alertColor = COLOR_YELLOW;
            break;
    }
    
    tft.fillRect(0, 30, 240, 25, alertColor);
    tft.setTextColor(COLOR_BLACK);
    tft.setTextSize(1);
    tft.setCursor(5, 38);
    tft.println(message);
    
    static unsigned long alertDisplayTime = millis();
    if (millis() - alertDisplayTime > 5000) {
        tft.fillRect(0, 30, 240, 25, COLOR_BLACK);
        alertDisplayTime = millis();
    }
}

void removeOldAlerts() {
    unsigned long currentTime = millis();
    int newCount = 0;
    for (int i = 0; i < activeAlertsCount; i++) {
        if (!activeAlerts[i].acknowledged && 
            (currentTime - activeAlerts[i].timestamp <= 30000)) {
            activeAlerts[newCount++] = activeAlerts[i];
        }
    }
    activeAlertsCount = newCount;
}

// ==================== SETTINGS MANAGEMENT ====================
void loadSettings() {
    // Default settings
    alertThresholds.heartRateMin = 60.0f;
    alertThresholds.heartRateMax = 100.0f;
    alertThresholds.spO2Min = 95.0f;
    alertThresholds.batteryMin = 20.0f;
    alertThresholds.enabled = true;
    screenBrightness = 128;
    
    Serial.println("Loaded default settings");
}

void saveSettings() {
    Serial.println("Settings saved");
}

// ==================== UTILITY FUNCTIONS ====================
void updateDisplay() {
    if (!displayOn) return;
    
    switch (currentScreen) {
        case SCREEN_TYPE_MAIN:
            updateVitalSigns();
            drawWaveform();
            drawStatusBar();
            break;
        case SCREEN_TYPE_SETTINGS:
        case SCREEN_TYPE_HISTORY:
            break;
    }
}

void formatTime(unsigned long timestamp, char* output) {
    unsigned long seconds = timestamp / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    
    seconds = seconds % 60;
    minutes = minutes % 60;
    hours = hours % 24;
    
    snprintf(output, 16, "%02lu:%02lu:%02lu", hours, minutes, seconds);
}

void printSystemInfo() {
    Serial.println("\n=== System Information ===");
    Serial.print("Firmware Version: ");
    Serial.println(FIRMWARE_VERSION);
    Serial.print("Device Name: ");
    Serial.println(DEVICE_NAME);
    Serial.print("Free RAM: ");
    Serial.println(freeMemory());
    
    Serial.print("Sensor Status: ");
    Serial.println(particleSensor.begin() ? "Connected" : "Disconnected");
    Serial.print("Display Status: Active\n");
    Serial.print("Touch Status: ");
    Serial.println(ts.begin() ? "Active" : "Inactive");
    Serial.print("Data Buffer: ");
    Serial.print(dataBufferCount);
    Serial.print("/");
    Serial.print(DATA_BUFFER_SIZE);
    Serial.println(" entries");
    Serial.print("Active Alerts: ");
    Serial.println(activeAlertsCount);
    Serial.println("========================");
}

void performSelfTest() {
    Serial.println("Performing system self-test...");
    
    bool testsPassed = true;
    
    Serial.print("Testing display... ");
    tft.fillScreen(COLOR_RED);
    delay(500);
    tft.fillScreen(COLOR_GREEN);
    delay(500);
    tft.fillScreen(COLOR_BLUE);
    delay(500);
    tft.fillScreen(COLOR_BLACK);
    Serial.println("OK");
    
    Serial.print("Testing touch controller... ");
    if (ts.begin()) {
        Serial.println("OK");
    } else {
        Serial.println("FAILED");
        testsPassed = false;
    }
    
    Serial.print("Testing MAX30102 sensor... ");
    if (particleSensor.begin()) {
        Serial.println("OK");
    } else {
        Serial.println("FAILED");
        testsPassed = false;
    }
    
    Serial.print("Testing buzzer... ");
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("OK");
    
    Serial.print("Testing battery monitor... ");
    float batteryLevel = readBatteryLevel();
    if (batteryLevel >= 0 && batteryLevel <= 100) {
        Serial.print("OK (");
        Serial.print(batteryLevel);
        Serial.println("%)");
    } else {
        Serial.println("WARNING - Unusual reading");
    }
    
    Serial.print("Self-test ");
    Serial.println(testsPassed ? "PASSED" : "FAILED");
    
    if (testsPassed) {
        for (int i = 0; i < 3; i++) {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(100);
            digitalWrite(BUZZER_PIN, LOW);
            delay(100);
        }
    } else {
        for (int i = 0; i < 5; i++) {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(200);
            digitalWrite(BUZZER_PIN, LOW);
            delay(200);
        }
    }
}

void handleSerialCommands() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.toLowerCase();
        command.trim();
        
        if (command == "help") {
            Serial.println("\n=== Available Commands ===");
            Serial.println("help - Show this help message");
            Serial.println("info - Show system information");
            Serial.println("test - Perform self-test");
            Serial.println("reset - Reset system");
            Serial.println("data - Show current readings");
            Serial.println("export - Export data to serial");
            Serial.println("clear - Clear data buffer");
            Serial.println("alerts - Show active alerts");
            Serial.println("========================");
        }
        else if (command == "info") {
            printSystemInfo();
        }
        else if (command == "test") {
            performSelfTest();
        }
        else if (command == "reset") {
            Serial.println("Resetting system...");
            asm volatile ("  jmp 0");  
        }
        else if (command == "data") {
            Serial.print("Heart Rate: ");
            Serial.print(currentVitals.heartRate);
            Serial.println(" BPM");
            Serial.print("SpO2: ");
            Serial.print(currentVitals.spO2);
            Serial.println("%");
            Serial.print("Battery: ");
            Serial.print(currentVitals.batteryLevel);
            Serial.println("%");
            Serial.print("Finger Detected: ");
            Serial.println(currentVitals.isFingerDetected ? "Yes" : "No");
        }
        else if (command == "export") {
            Serial.println("Timestamp,HeartRate,SpO2,BatteryLevel");
            for (int i = 0; i < dataBufferCount; i++) {
                Serial.print(dataBuffer[i].timestamp);
                Serial.print(",");
                Serial.print(dataBuffer[i].heartRate);
                Serial.print(",");
                Serial.print(dataBuffer[i].spO2);
                Serial.print(",");
                Serial.println(dataBuffer[i].batteryLevel);
            }
        }
        else if (command == "clear") {
            dataBufferCount = 0;
            Serial.println("Data buffer cleared");
        }
        else if (command == "alerts") {
            Serial.print("Active Alerts: ");
            Serial.println(activeAlertsCount);
            for (int i = 0; i < activeAlertsCount; i++) {
                Serial.print("- ");
                Serial.print(activeAlerts[i].level == ALERT_LEVEL_CRITICAL ? "CRITICAL" :
                            activeAlerts[i].level == ALERT_LEVEL_WARNING ? "WARNING" : "INFO");
                Serial.print(": ");
                Serial.println(activeAlerts[i].message);
            }
        }
        else {
            Serial.println("Unknown command. Type 'help' for available commands.");
        }
    }
}

void exportData() {
    Serial.println("Exporting data...");
    
    tft.fillRect(50, 100, 140, 60, COLOR_GREEN);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(2);
    tft.setCursor(60, 120);
    tft.println("Data Exported!");
    
    delay(2000);
    showSettingsScreen();
}

void clearData() {
    Serial.println("Clearing data...");
    dataBufferCount = 0;
    
    tft.fillRect(50, 100, 140, 60, COLOR_RED);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(2);
    tft.setCursor(70, 120);
    tft.println("Data Cleared!");
    
    delay(2000);
    showSettingsScreen();
}

void handleLowPowerMode() {
    if (currentVitals.batteryLevel < 10) {
        Serial.println("Entering low power mode...");
        
        screenBrightness = 50;
        
        tft.fillRect(0, 0, 240, 20, COLOR_RED);
        tft.setTextColor(COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(5, 5);
        tft.println("LOW POWER MODE");
    }
}

void checkMemoryUsage() {
    static unsigned long lastMemCheck = 0;
    
    if (millis() - lastMemCheck > 30000) {
        lastMemCheck = millis();
        
        int freeMem = freeMemory();
        if (freeMem < 500) {
            Serial.print("WARNING: Low memory - ");
            Serial.print(freeMem);
            Serial.println(" bytes free");
            
            if (dataBufferCount > 25) {
                memmove(dataBuffer, dataBuffer + 10, (dataBufferCount - 10) * sizeof(VitalSigns));
                dataBufferCount -= 10;
                Serial.println("Cleaned up data buffer to free memory");
            }
            
            if (alertHistoryCount > 10) {
                memmove(alertHistory, alertHistory + 5, (alertHistoryCount - 5) * sizeof(Alert));
                alertHistoryCount -= 5;
                Serial.println("Cleaned up alert history to free memory");
            }
        }
    }
}

void initializeSystem() {
    Serial.println("Reinitializing system...");
    
    currentState = SYSTEM_STATE_INITIALIZING;
    currentScreen = SCREEN_TYPE_MAIN;
    
    bufferIndex = 0;
    memset(irBuffer, 0, sizeof(irBuffer));
    memset(redBuffer, 0, sizeof(redBuffer));
    
    lastSensorUpdate = 0;
    lastDisplayUpdate = 0;
    lastDataLog = 0;
    lastAlertCheck = 0;
    
    currentState = SYSTEM_STATE_RUNNING;
    showMainScreen();
    
    Serial.println("System reinitialization complete");
}

void handleSystemError(const char* errorMessage) {
    Serial.print("SYSTEM ERROR: ");
    Serial.println(errorMessage);
    
    currentState = SYSTEM_STATE_ERROR;
    
    showError("System Error", errorMessage);
    
    for (int i = 0; i < 5; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(100);
    }
    
    delay(5000);
    
    Serial.println("Attempting system recovery...");
    initializeSystem();
}
// Helper function to check free memory
#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif  // __arm__

int freeMemory() {
  char top;
#ifdef __arm__
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;
#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
}