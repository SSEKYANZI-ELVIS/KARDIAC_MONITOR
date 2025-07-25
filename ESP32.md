Cardiac Monitor Project - Complete README
Table of Contents
Project Overview

Hardware Requirements

Software Requirements

Installation Guide

Setup Instructions

Usage Guide

Troubleshooting

Safety Information

License

Project Overview
This cardiac monitoring system is an educational project that demonstrates heart rate and SpO2 monitoring using an ESP32 with an 8-bit parallel TFT display. The system features:

Real-time heart rate and blood oxygen level (SpO2) monitoring

Touchscreen interface with multiple screens

Data logging and history viewing

Alert system for abnormal readings

Battery monitoring

WiFi connectivity for remote monitoring

Disclaimer: This is for educational purposes only. Not for medical use.

Hardware Requirements
ESP32 Development Board

MAX30102 Heart Rate/SpO2 Sensor

8-bit parallel TFT Display (MCUFRIEND compatible)

XPT2046 Touch Controller

Buzzer (for alerts)

LiPo Battery with TP4056 Charger

Breadboard and jumper wires

Software Requirements
Arduino IDE (v1.8.x or later)

Required Libraries:

MCUFRIEND_kbv

Adafruit_GFX

XPT2046_Touchscreen

MAX30105 by SparkFun

WiFi (included with ESP32 core)

WebServer (included with ESP32 core)

SPIFFS (included with ESP32 core)

Installation Guide
1. Library Installation
Open Arduino IDE

Go to Sketch > Include Library > Manage Libraries

Search for and install:

MCUFRIEND_kbv

Adafruit GFX Library

XPT2046_Touchscreen

MAX30105 by SparkFun

2. Hardware Connections
Connect components as follows:

Component	ESP32 Pin
MAX30102 SDA	21
MAX30102 SCL	22
Touch CS	15
Touch IRQ	21
Buzzer	25
Battery Monitor	36
The 8-bit parallel TFT connects directly to the MCUFRIEND shield pins.

3. Uploading the Code
Connect ESP32 to computer via USB

Open the cardiac_monitor.ino file in Arduino IDE

Select correct board (ESP32 Dev Module) and port

Click Upload button

Setup Instructions
Initial Setup
After uploading, open Serial Monitor (115200 baud)

System will display splash screen on TFT

Place finger on MAX30102 sensor for readings

WiFi Configuration
On first boot, device will create WiFi hotspot "CardiacMonitor_Setup"

Connect to this network with password "12345678"

Open browser and navigate to http://192.168.4.1

Select your WiFi network and enter password

Device will restart and connect to your network

Calibration
The system includes default settings:

Normal heart rate range: 60-100 BPM

Low SpO2 threshold: 95%

Low battery threshold: 20%

To adjust settings:

Tap "Settings" on main screen

Current thresholds are displayed

Modify values in code (settings section) and reupload

Usage Guide
Main Screen
Displays real-time heart rate and SpO2

Shows waveform visualization

Battery level indicator

WiFi connection status

Three buttons:

Settings: Configure thresholds

History: View past readings

WiFi: Configure WiFi settings

Taking Measurements
Place finger firmly on MAX30102 sensor

Wait 10-15 seconds for stable readings

Keep finger still during measurement

Data Logging
System automatically logs data every second

Stores up to 100 readings

Data persists across reboots using SPIFFS

Export data via web interface or serial commands

Alert System
Three alert levels:

Info: Minor deviations

Warning: Significant deviations

Critical: Dangerous levels

Alerts trigger:

On-screen notification

Buzzer sound pattern

Serial Monitor output

Web Interface
Accessible when connected to WiFi:

View current readings

Export data in CSV format

View historical data

Serial Commands
Accessible via Serial Monitor (115200 baud):

text
help      - Show available commands
info      - Show system information
test      - Perform self-test
reset     - Reset system
wifi      - Show WiFi status
data      - Show current readings
export    - Export data to serial
clear     - Clear data buffer
alerts    - Show active alerts
config    - Enter configuration mode
Troubleshooting
Common Issues
No finger detected:

Ensure finger covers both LEDs on MAX30102

Check sensor connection (SDA to 21, SCL to 22)

Increase FINGER_THRESHOLD in code if needed

Display not working:

Verify TFT is properly seated on shield

Check library installation

Try different display ID in initializeDisplay()

Touch not responding:

Verify touch controller connection (CS to pin 15)

Check touch calibration in handleTouch()

WiFi connection issues:

Ensure correct SSID and password

Check router settings (2.4GHz network recommended)

Restart device and router

Error Messages
"MAX30102 not found": Check I2C connections

"Display initialization failed": Check TFT connections

"SPIFFS initialization failed": Format SPIFFS in Arduino IDE

"Low memory": System will automatically clear older data

Safety Information
This is not a medical device

Do not use for diagnosis or treatment

Always consult healthcare professionals for medical advice

Keep away from water and extreme temperatures

Do not modify hardware for medical applications

Use proper LiPo battery handling precautions

License
This project is open-source for educational purposes. Commercial use prohibited without permission. See full license in repository.

For additional support, please refer to the code comments or open an issue in the project repository. The system includes comprehensive error handling and recovery mechanisms to ensure stable operation.