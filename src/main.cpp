/*
 * Smart Home Automation
 * 
 * Created on : Nov 23, 2022 
 *
 * Author: Parikshit Pagare
 * github.com/parikshitpagare
 * linkedin.com/in/parikshitpagare 
 * 
 * Project link: github.com/parikshitpagare/smart-home-automation-rtos
 * 
 * MIT License 

 * Copyright (c) 2024 Parikshit Pagare

 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "BluetoothSerial.h"
#include <Wire.h>
#include "DHT.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Ticker.h>
#include <ESP32Servo.h>  // Add servo library for ESP32
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Bluetooth not available or not enabled. It is only available for the ESP32 chip.
#endif

/* Using core 1 of ESP32 */
#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

/* Sensor pins */
#define DHTPIN 33                 // DHT22 temperature sensor
#define DHTTYPE DHT22
#define lightSensor 32            // LDR sensor (moved to ADC1 for WiFi compatibility)
#define smokeSensor 34            // MQ2 smoke and gas sensor (moved to ADC1 for WiFi compatibility)
#define touchButton 4             // Push button for touch simulation (GPIO 4)
#define echo 2                    // Ultrasonic sensor echo pin
#define trigger 15                // Ultrasonic sensor trigger pin         
/* Control pins */          
#define fanServo 17               // Servo motor for fan simulation
#define lightRelay 16             // Relay for LED light control

/* Buzzer pins */
#define smokeBuzzer 14            // Buzzer for alerting smoke or gas
#define touchBuzzer 27            // Buzzer for alerting touch

/* Led pins */
#define smokeLed 5                // Led for alerting smoke
#define touchLed 19               // Led for alerting touch
#define ultrasonicLed 18          // Led for alerting when someone in range

/* Setting OLED display parameters */
#define SCREEN_WIDTH 128          // OLED display width, in pixels
#define SCREEN_HEIGHT 64          // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C       // i2c address for OLED display
#define OLED_RESET -1             // No reset pin used

/* WiFi credentials */
const char* ssid = "Wokwi-GUEST";
const char* password = "";

/* Defining objects */
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BluetoothSerial SerialBT;                                 
Ticker ultrasonic;
Servo fanServoMotor;  // Servo object for fan simulation
AsyncWebServer server(80);  // Web server on port 80

/* Non-blocking servo sweep variables */
unsigned long servoLastUpdate = 0;
int servoCurrentPos = 0;
int servoTargetPos = 0;
bool servoSweepActive = false;
bool servoDirection = true;  // true = forward (0->180), false = backward (180->0)
enum ServoState { SERVO_OFF, SERVO_SWEEP_UP, SERVO_SWEEP_DOWN, SERVO_HOLD };
ServoState servoState = SERVO_OFF;

/* Defining queues */
static QueueHandle_t tempReading;
static QueueHandle_t lightReading;
static QueueHandle_t smokeAlarm;
static QueueHandle_t touchAlarm;

/* Defining task handles */
TaskHandle_t autoFan_handle = NULL;
TaskHandle_t autoLight_handle = NULL;

/* Status for OLED display indicators */
bool fanStatus = false;
bool lightStatus = false;
bool smokeStatus = false;
bool touchStatus = false;
bool ultrasonicStatus = false;

/* Web control variables */
bool manualLightControl = false;
bool manualFanControl = false;
bool securitySystemEnabled = true;
bool autoMode = true;

/* Current sensor readings for web display */
float currentTemperature = 0;
int currentLightLevel = 0;
int currentSmokeLevel = 0;
int currentDistance = 0;

/* Static variables for smoke detection to reduce stack usage */
bool lastSmokeStatus = false;
unsigned long lastSmokeLogTime = 0;

/*
* ---------------------------------------------------------------------------------------------------------------------------------
* Beautiful Web Interface HTML
* ---------------------------------------------------------------------------------------------------------------------------------
*/

const char* webPageHTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Home Control</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            color: #333;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }
        
        .header {
            text-align: center;
            color: white;
            margin-bottom: 30px;
        }
        
        .header h1 {
            font-size: 3em;
            margin-bottom: 10px;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
        }
        
        .header p {
            font-size: 1.2em;
            opacity: 0.9;
        }
        
        .dashboard {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        
        .card {
            background: rgba(255, 255, 255, 0.95);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 25px;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);
            border: 1px solid rgba(255, 255, 255, 0.2);
            transition: transform 0.3s ease, box-shadow 0.3s ease;
        }
        
        .card:hover {
            transform: translateY(-5px);
            box-shadow: 0 12px 40px rgba(0, 0, 0, 0.15);
        }
        
        .card-header {
            display: flex;
            align-items: center;
            margin-bottom: 20px;
        }
        
        .card-icon {
            font-size: 2.5em;
            margin-right: 15px;
            width: 60px;
            height: 60px;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 15px;
            color: white;
        }
        
        .temperature-icon { background: linear-gradient(45deg, #ff6b6b, #ee5a24); }
        .light-icon { background: linear-gradient(45deg, #feca57, #ff9ff3); }
        .security-icon { background: linear-gradient(45deg, #48dbfb, #0abde3); }
        .fan-icon { background: linear-gradient(45deg, #1dd1a1, #10ac84); }
        .smoke-icon { background: linear-gradient(45deg, #ff6348, #e17055); }
        
        .card-title {
            font-size: 1.4em;
            font-weight: 600;
            color: #2c3e50;
        }
        
        .sensor-value {
            font-size: 2.2em;
            font-weight: bold;
            margin: 15px 0;
            color: #2c3e50;
        }
        
        .status-indicator {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
        }
        
        .status-on { background: #2ecc71; }
        .status-off { background: #e74c3c; }
        .status-warning { background: #f39c12; }
        
        .toggle-switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 34px;
            margin: 10px 0;
        }
        
        .toggle-switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: #ccc;
            transition: 0.4s;
            border-radius: 34px;
        }
        
        .slider:before {
            position: absolute;
            content: "";
            height: 26px;
            width: 26px;
            left: 4px;
            bottom: 4px;
            background: white;
            transition: 0.4s;
            border-radius: 50%;
        }
        
        input:checked + .slider {
            background: #2ecc71;
        }
        
        input:checked + .slider:before {
            transform: translateX(26px);
        }
        
        .control-button {
            background: linear-gradient(45deg, #667eea, #764ba2);
            color: white;
            border: none;
            padding: 12px 25px;
            border-radius: 25px;
            font-size: 1em;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            margin: 5px;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.2);
        }
        
        .control-button:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(0, 0, 0, 0.3);
        }
        
        .control-button:active {
            transform: translateY(0);
        }
        
        .emergency-button {
            background: linear-gradient(45deg, #e74c3c, #c0392b);
            font-size: 1.1em;
            padding: 15px 30px;
        }
        
        .mode-selector {
            display: flex;
            background: #ecf0f1;
            border-radius: 25px;
            overflow: hidden;
            margin: 15px 0;
        }
        
        .mode-option {
            flex: 1;
            padding: 12px 20px;
            background: transparent;
            border: none;
            cursor: pointer;
            transition: all 0.3s ease;
            font-weight: 600;
        }
        
        .mode-option.active {
            background: linear-gradient(45deg, #667eea, #764ba2);
            color: white;
        }
        
        .footer {
            text-align: center;
            color: white;
            margin-top: 40px;
            opacity: 0.8;
        }
        
        @media (max-width: 768px) {
            .header h1 { font-size: 2em; }
            .dashboard { grid-template-columns: 1fr; }
            .card { padding: 20px; }
        }
        
        .refresh-icon {
            animation: spin 2s linear infinite;
        }
        
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🏠 Smart Home Control</h1>
            <p>Intelligent Automation & Security System</p>
        </div>
        
        <div class="dashboard">
            <!-- Temperature Card -->
            <div class="card" id="fanCard">
                <div class="card-header">
                    <div class="card-icon temperature-icon">🌡️</div>
                    <div class="card-title">Temperature</div>
                </div>
                <div class="sensor-value" id="temperature">--°C</div>
                <div>
                    <span class="status-indicator" id="fanStatus"></span>
                    <span id="fanStatusText">Fan Status</span>
                </div>
                <div class="mode-selector">
                    <button class="mode-option active" onclick="setFanMode('auto')" id="fanAutoBtn">Auto</button>
                    <button class="mode-option" onclick="setFanMode('manual')" id="fanManualBtn">Manual</button>
                </div>
                <button class="control-button" onclick="toggleFan()" id="fanButton">Toggle Fan</button>
            </div>
            
            <!-- Light Card -->
            <div class="card" id="lightCard">
                <div class="card-header">
                    <div class="card-icon light-icon">💡</div>
                    <div class="card-title">Lighting</div>
                </div>
                <div class="sensor-value" id="lightLevel">-- lux</div>
                <div>
                    <span class="status-indicator" id="lightStatus"></span>
                    <span id="lightStatusText">Light Status</span>
                </div>
                <div class="mode-selector">
                    <button class="mode-option active" onclick="setLightMode('auto')" id="lightAutoBtn">Auto</button>
                    <button class="mode-option" onclick="setLightMode('manual')" id="lightManualBtn">Manual</button>
                </div>
                <button class="control-button" onclick="toggleLight()" id="lightButton">Toggle Light</button>
            </div>
            
            <!-- Security Card -->
            <div class="card">
                <div class="card-header">
                    <div class="card-icon security-icon">🔒</div>
                    <div class="card-title">Security System</div>
                </div>
                <div>
                    <span class="status-indicator" id="touchStatus"></span>
                    <span id="touchStatusText">Intrusion Alert</span>
                </div>
                <div>
                    <span class="status-indicator" id="systemStatus"></span>
                    <span id="systemStatusText">System Armed</span>
                </div>
                <div style="margin: 15px 0;">
                    <strong>Security Monitoring:</strong>
                    <label class="toggle-switch" style="margin-left: 10px;">
                        <input type="checkbox" id="securityToggle" onchange="toggleSecurity()" checked>
                        <span class="slider"></span>
                    </label>
                    <span style="margin-left: 10px; font-size: 0.9em;">Enable/Disable intrusion detection</span>
                </div>
                <button class="control-button emergency-button" onclick="resetAlarms()">🚨 Reset All Alarms</button>
            </div>
            
            <!-- Smoke Detection Card -->
            <div class="card">
                <div class="card-header">
                    <div class="card-icon smoke-icon">💨</div>
                    <div class="card-title">Air Quality</div>
                </div>
                <div class="sensor-value" id="smokeLevel">-- ppm</div>
                <div>
                    <span class="status-indicator" id="smokeStatus"></span>
                    <span id="smokeStatusText">Air Quality</span>
                </div>
                <div>
                    <span class="status-indicator" id="distanceStatus"></span>
                    <span>Motion: <span id="distance">-- cm</span></span>
                </div>
            </div>
        </div>
        
        <div class="footer">
            <p>🔄 Auto-refresh every 2 seconds | Last updated: <span id="lastUpdate">--</span></p>
            <p>Mohammad's Smart Home System | ESP32 Based</p>
        </div>
    </div>

    <script>
        // Update data every 2 seconds
        setInterval(updateData, 2000);
        updateData(); // Initial load
        
        async function updateData() {
            try {
                const response = await fetch('/data');
                const data = await response.json();
                
                // Update sensor values
                document.getElementById('temperature').textContent = data.temperature + '°C';
                document.getElementById('lightLevel').textContent = data.lightLevel + ' lux';
                document.getElementById('smokeLevel').textContent = data.smokeLevel + ' ppm';
                document.getElementById('distance').textContent = data.distance + ' cm';
                
                // Update status indicators
                updateStatus('fanStatus', 'fanStatusText', data.fanStatus, 'Fan ');
                updateStatus('lightStatus', 'lightStatusText', data.lightStatus, 'Light ');
                updateStatus('touchStatus', 'touchStatusText', data.touchStatus, 'Touch ');
                updateStatus('smokeStatus', 'smokeStatusText', data.smokeStatus, 'Air ');
                updateStatus('distanceStatus', null, data.ultrasonicStatus, '');
                updateStatus('systemStatus', 'systemStatusText', data.securityEnabled, 'Security ');
                
                // Update last refresh time
                document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
                
            } catch (error) {
                console.error('Error fetching data:', error);
            }
        }
        
        function updateStatus(indicatorId, textId, status, prefix) {
            const indicator = document.getElementById(indicatorId);
            const text = textId ? document.getElementById(textId) : null;
            
            if (status) {
                indicator.className = 'status-indicator status-on';
                if (text) text.textContent = prefix + 'ON';
            } else {
                indicator.className = 'status-indicator status-off';
                if (text) text.textContent = prefix + 'OFF';
            }
        }
        
        async function toggleFan() {
            await fetch('/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({action: 'toggleFan'})
            });
            updateData();
        }
        
        async function toggleLight() {
            await fetch('/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({action: 'toggleLight'})
            });
            updateData();
        }
        
        async function toggleSecurity() {
            const enabled = document.getElementById('securityToggle').checked;
            await fetch('/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({action: 'setSecurity', value: enabled})
            });
            updateData();
        }
        
        async function resetAlarms() {
            await fetch('/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({action: 'resetAlarms'})
            });
            updateData();
        }
        
        async function setFanMode(mode) {
            // Update button states
            document.getElementById('fanAutoBtn').classList.remove('active');
            document.getElementById('fanManualBtn').classList.remove('active');
            if (mode === 'auto') {
                document.getElementById('fanAutoBtn').classList.add('active');
            } else {
                document.getElementById('fanManualBtn').classList.add('active');
            }
            
            await fetch('/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({action: 'setFanMode', value: mode})
            });
            updateData();
        }
        
        async function setLightMode(mode) {
            // Update button states
            document.getElementById('lightAutoBtn').classList.remove('active');
            document.getElementById('lightManualBtn').classList.remove('active');
            if (mode === 'auto') {
                document.getElementById('lightAutoBtn').classList.add('active');
            } else {
                document.getElementById('lightManualBtn').classList.add('active');
            }
            
            await fetch('/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({action: 'setLightMode', value: mode})
            });
            updateData();
        }
    </script>
</body>
</html>
)rawliteral";


/*
* ---------------------------------------------------------------------------------------------------------------------------------
* Non-blocking servo sweep function
* ---------------------------------------------------------------------------------------------------------------------------------
*/

void updateServoSweep() {
  unsigned long currentTime = millis();
    // Update servo position every 10ms for faster movement
  if (currentTime - servoLastUpdate >= 10) {
    servoLastUpdate = currentTime;
    
    switch (servoState) {
      case SERVO_OFF:
        fanServoMotor.write(0);
        servoCurrentPos = 0;
        break;
        
      case SERVO_SWEEP_UP:
        if (servoCurrentPos < 180) {
          servoCurrentPos += 3;  // Move 3 degrees at a time for faster speed
          fanServoMotor.write(servoCurrentPos);
        } else {
          servoState = SERVO_SWEEP_DOWN;  // Switch to sweeping down
        }
        break;
        
      case SERVO_SWEEP_DOWN:
        if (servoCurrentPos > 0) {
          servoCurrentPos -= 3;  // Move 3 degrees at a time for faster speed
          fanServoMotor.write(servoCurrentPos);
        } else {
          servoState = SERVO_SWEEP_UP;  // Switch to sweeping up for continuous operation
        }
        break;
        
      case SERVO_HOLD:
        fanServoMotor.write(servoCurrentPos);
        break;
    }
  }
}

/*
* ---------------------------------------------------------------------------------------------------------------------------------
* Temperature monitoring and fan control 
* ---------------------------------------------------------------------------------------------------------------------------------
*/

/* Task for temperature sensing using DHT22 */
void tempRead(void *parameter) {
 int t = 0;
  
  Serial.println("tempRead task started, waiting for DHT sensor to stabilize...");
  // Wait for DHT sensor to stabilize
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  Serial.println("DHT sensor stabilization complete, starting temperature readings...");
  
  while (true) {
    t = dht.readTemperature();
    
    if (isnan(t)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      continue;
    }    
    
    Serial.print("Temperature: "); 
    Serial.print(t); 
    Serial.println(" °C");
    currentTemperature = t;  // Store for web interface
    xQueueSend (tempReading, (void*)&t, 10);
    
    // Non-blocking fan control based on temperature (only in auto mode)
    if (!manualFanControl) {
      if (t >= 33) {
        if (servoState == SERVO_OFF) {
          Serial.println("Auto: Temperature high - starting fan");
          servoState = SERVO_SWEEP_UP;
          servoCurrentPos = 0;
          fanStatus = true;
        }
      }
      else if (t < 33) {
        if (servoState != SERVO_OFF) {
          Serial.println("Auto: Temperature normal - stopping fan");
          servoState = SERVO_OFF;
          fanStatus = false; 
        }
      }
    }
    // If in manual mode, don't change fan state automatically
    
    // Update servo position (non-blocking)
    updateServoSweep();
    
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

/* Task for fan control in auto mode */
void autoFan(void *parameter) {
  int tempValue;
  Serial.println("autoFan task started, waiting for temperature data...");
  
  while (true) { 
    Serial.println("autoFan: Waiting for temperature reading...");
    xQueueReceive(tempReading, (void *)&tempValue, portMAX_DELAY);    
    Serial.print("autoFan: Received temperature: ");
    Serial.print(tempValue);
    Serial.println("°C");
    
    if (tempValue >= 33) {
      // SerialBT.print ("Fan on?");  // Bluetooth disabled
      Serial.println("Temperature high - starting continuous fan sweep");
      if (servoState == SERVO_OFF) {
        servoState = SERVO_SWEEP_UP;
        servoCurrentPos = 0;
      }
      fanStatus = true;
    }
    else if (tempValue < 33) {
      // SerialBT.print ("Fan off?");  // Bluetooth disabled
      Serial.println("Temperature normal - stopping fan sweep");
      servoState = SERVO_OFF;
      fanStatus = false; 
    }
    
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

/*
* ---------------------------------------------------------------------------------------------------------------------------------
* LED light control functions via relay
* ---------------------------------------------------------------------------------------------------------------------------------
*/

/* Turn on LED light via relay */
void turnOnLight() {
  Serial.println("Turning ON LED light via relay");
  Serial.print("Setting relay pin 16 to HIGH (relay ON)");
  digitalWrite(lightRelay, HIGH);  // Relay on (LED on) - inverted logic for this circuit
  lightStatus = true;
  Serial.println(" - LED should be ON now");
}

/* Turn off LED light via relay */
void turnOffLight() {
  Serial.println("Turning OFF LED light via relay");
  Serial.print("Setting relay pin 16 to LOW (relay OFF)");
  digitalWrite(lightRelay, LOW); // Relay off (LED off) - inverted logic for this circuit
  lightStatus = false;
  Serial.println(" - LED should be OFF now");
}

/*
* ---------------------------------------------------------------------------------------------------------------------------------
* LDR based bulb control 
* ---------------------------------------------------------------------------------------------------------------------------------
*/

/* Task for light intensity sensing using LDR */
void lightRead(void *parameter) {
  int lightValue;
  
  while (true) {
    lightValue = analogRead(lightSensor); 
    currentLightLevel = lightValue;  // Store for web interface
    xQueueSend (lightReading, (void*)&lightValue, 10);
    
    // Update servo sweep to maintain smooth operation
    updateServoSweep();
    
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  } 
}

/* Task for bulb control in auto mode */
void autoLight(void *parameter) {
  int lightValue;
  Serial.println("autoLight task started, waiting for light sensor data...");
  
  while (true) { 
    xQueueReceive(lightReading, (void *)&lightValue, portMAX_DELAY);    
    
    // Only control light automatically if NOT in manual mode
    if (!manualLightControl) {
      if (lightValue >= 2200) {
        if (!lightStatus) {  // Only turn on if currently off
          Serial.print("Auto: Light level low (");
          Serial.print(lightValue);
          Serial.println(") - turning ON LED light");
          turnOnLight();
        }
      }
      else if (lightValue < 2200) {
        if (lightStatus) {  // Only turn off if currently on
          Serial.print("Auto: Light level sufficient (");
          Serial.print(lightValue);
          Serial.println(") - turning OFF LED light");
          turnOffLight();
        }
      }
    }
    // If in manual mode, don't change light state automatically
    
    vTaskDelay(200 / portTICK_PERIOD_MS);
  } 
}

/*
* ---------------------------------------------------------------------------------------------------------------------------------
* Safety and Security system
* ---------------------------------------------------------------------------------------------------------------------------------
*/

/* Task for detecting smoke or gas using MQ2 sensor */
void smokeDetect(void *parameter) {
  int smokeValue;
  
  while (true) {
    smokeValue = analogRead(smokeSensor); 
    
    currentSmokeLevel = smokeValue;  // Store for web interface
    
    if (smokeValue >= 3200) {
      if (!lastSmokeStatus || (millis() - lastSmokeLogTime > 10000)) {
        Serial.print("SMOKE DETECTED! Level: ");
        Serial.println(smokeValue);
        lastSmokeLogTime = millis();
      }
      digitalWrite(smokeLed, HIGH);
      tone(smokeBuzzer, 2000);
      smokeStatus = true;      
      lastSmokeStatus = true;
    }
    else if (smokeValue < 3200) {
      if (lastSmokeStatus || (millis() - lastSmokeLogTime > 10000)) {
        Serial.print("Smoke level normal: ");
        Serial.println(smokeValue);
        lastSmokeLogTime = millis();
      }
      digitalWrite(smokeLed, LOW);
      noTone(smokeBuzzer);
      smokeStatus = false; 
      lastSmokeStatus = false;
    }
    
    // Update servo sweep to maintain smooth operation
    updateServoSweep();
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);      
  }
}

/* Task for detecting button press (simulating touch) */
void touchDetect(void *parameter) {
  int buttonState;
  int lastButtonState = HIGH;  // Assume button is not pressed initially
  unsigned long lastDebounceTime = 0;
  unsigned long debounceDelay = 50;  // 50ms debounce delay
  
  while (true) {
    buttonState = digitalRead(touchButton);
    
    // Check if button state has changed (with debouncing)
    if (buttonState != lastButtonState) {
      lastDebounceTime = millis();
    }
    
    if ((millis() - lastDebounceTime) > debounceDelay) {
      // Only trigger if security system is enabled
      if (securitySystemEnabled) {
        // Button is pressed (LOW because of pull-up resistor)
        if (buttonState == LOW && !touchStatus) {  // Only trigger if not already active
          Serial.println("SECURITY ALERT! Touch detected - activating alarm");
          digitalWrite(touchLed, HIGH);
          tone(touchBuzzer, 1500);  // Use tone() for active buzzer, 1500Hz frequency
          touchStatus = true;
          Serial.println("Touch alarm is ACTIVE - needs manual reset via app/button");
        }
        // Long press (hold for 3+ seconds) to reset the alarm
        else if (buttonState == LOW && touchStatus) {
          unsigned long pressStart = millis();
          while (digitalRead(touchButton) == LOW && (millis() - pressStart) < 3000) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
          }
          
          if ((millis() - pressStart) >= 3000) {
            Serial.println("Long press detected - RESETTING touch alarm");
            digitalWrite(touchLed, LOW);
            noTone(touchBuzzer);  // Turn off tone
            touchStatus = false;
            Serial.println("Touch alarm has been RESET");
          }
        }
      } else {
        // Security system disabled - ensure alarm is off
        if (touchStatus) {
          digitalWrite(touchLed, LOW);
          noTone(touchBuzzer);
          touchStatus = false;
          Serial.println("Security system disabled - touch alarm deactivated");
        }
      }
    }
    
    lastButtonState = buttonState;
    vTaskDelay(10 / portTICK_PERIOD_MS);  // Small delay to prevent excessive polling
  }
}

/* Task for finding distance using Ultrasonic sensor */
void ultrasonicDetect() { 
  int distance;
  int duration;

  digitalWrite(trigger, LOW);
  delayMicroseconds(2);
  digitalWrite(trigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigger, LOW);

  duration = pulseIn(echo, HIGH);
  distance = (duration / 2) * 0.0343;
  
  currentDistance = distance;  // Store for web interface

  // Reduced logging frequency
  static unsigned long lastLogTime = 0;
  if (millis() - lastLogTime > 5000) {  // Print every 5 seconds
    Serial.print("Distance: ");
    Serial.println(distance);
    lastLogTime = millis();
  }
  
  if (distance > 20) {
    digitalWrite(ultrasonicLed, LOW);
    ultrasonicStatus = false;
  }
  else if (distance <= 20) {
    digitalWrite(ultrasonicLed, HIGH);
    ultrasonicStatus = true;
  }
}

/*
* ---------------------------------------------------------------------------------------------------------------------------------
* Security system control functions
* ---------------------------------------------------------------------------------------------------------------------------------
*/

/* Reset touch alarm manually (normally called from app/web interface) */
void resetTouchAlarm() {
  if (touchStatus) {
    Serial.println("Manual reset of touch alarm requested");
    digitalWrite(touchLed, LOW);
    noTone(touchBuzzer);
    touchStatus = false;
    Serial.println("Touch alarm has been manually RESET");
  }
}

/* Check and maintain security alarms */
void securitySystemMaintenance() {
  // Auto-reset touch alarm after 30 seconds for demo purposes
  static unsigned long touchAlarmStart = 0;
  static bool alarmTimerStarted = false;
  
  if (touchStatus && !alarmTimerStarted && securitySystemEnabled) {
    touchAlarmStart = millis();
    alarmTimerStarted = true;
  }
  
  if (touchStatus && alarmTimerStarted && (millis() - touchAlarmStart) > 30000) {
    resetTouchAlarm();
    alarmTimerStarted = false;
  }
  
  if (!touchStatus) {
    alarmTimerStarted = false;
  }
  
  // If security system is disabled, ensure all alarms are off
  if (!securitySystemEnabled && touchStatus) {
    resetTouchAlarm();
  }
}

/*
* ---------------------------------------------------------------------------------------------------------------------------------
* App based switch controls
* ---------------------------------------------------------------------------------------------------------------------------------
*/

/* Task for controlling relays and alarms using app */
void switchControl(void *parameter) {
  while (true) {
    // Handle security system maintenance
    securitySystemMaintenance();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

/* Task for temperature display on OLED */
void indicatorDisplay(void *parameter) {
  int tempValue;
  Serial.println("indicatorDisplay task started, waiting for temperature data...");

  const unsigned char fanIndicator [] PROGMEM = {
    0x01, 0xf8, 0x00, 0x07, 0x0e, 0x00, 0x0d, 0xc3, 0x00, 0x1b, 0xe1, 0x80, 0x11, 0xe0, 0x80, 0x30, 
	  0xe0, 0xc0, 0x30, 0x66, 0xc0, 0x30, 0x7f, 0xc0, 0x33, 0xff, 0xc0, 0x33, 0xde, 0xc0, 0x17, 0xc0, 
	  0x80, 0x1b, 0x81, 0x80, 0x0f, 0x83, 0x00, 0x06, 0x06, 0x00, 0x03, 0xfc, 0x00, 0x00, 0x00, 0x00, 
	  0x00, 0xf0, 0x00, 0x01, 0xf8, 0x00, 0x07, 0xfe, 0x00, 0x07, 0xfe, 0x00
  };
  
  const unsigned char lightIndicator [] PROGMEM = {
    0x00, 0x60, 0x00, 0x01, 0xfc, 0x00, 0x07, 0xc6, 0x00, 0x07, 0xc2, 0x00, 0x0f, 0xf1, 0x00, 0x0f, 
  	0xf1, 0x00, 0x0f, 0xfb, 0x00, 0x0f, 0xff, 0x00, 0x0f, 0xff, 0x00, 0x07, 0xfe, 0x00, 0x07, 0xfe, 
  	0x00, 0x03, 0xfc, 0x00, 0x01, 0xfc, 0x00, 0x01, 0xf8, 0x00, 0x01, 0x00, 0x00, 0x01, 0x80, 0x00, 
  	0x01, 0xf8, 0x00, 0x01, 0xf8, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00
  };

  const unsigned char smokeIndicator [] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x30, 0x00, 0x00, 0x38, 0x00, 0x00, 0x38, 0x00, 0x02, 
	  0x78, 0x00, 0x07, 0xf8, 0x00, 0x07, 0xfa, 0x00, 0x07, 0x3e, 0x00, 0x07, 0x3f, 0x00, 0x03, 0x0f, 
	  0x00, 0x17, 0x0f, 0x80, 0x1e, 0x0f, 0x80, 0x1e, 0x07, 0x80, 0x1e, 0x07, 0x80, 0x1e, 0x07, 0x80, 
	  0x0e, 0x07, 0x80, 0x0f, 0x0f, 0x00, 0x03, 0xfc, 0x00, 0x00, 0x60, 0x00
  };
  
  const unsigned char touchIndicator [] PROGMEM = {
    0x00, 0x60, 0x00, 0x01, 0xf8, 0x00, 0x03, 0xfc, 0x00, 0x07, 0x8e, 0x00, 0x07, 0x0e, 0x00, 0x06, 
	  0x06, 0x00, 0x06, 0x06, 0x00, 0x00, 0x06, 0x00, 0x00, 0x06, 0x00, 0x00, 0x0f, 0x00, 0x1f, 0xff, 
	  0x80, 0x1f, 0xff, 0x80, 0x1f, 0xff, 0x80, 0x1f, 0xff, 0x80, 0x1f, 0xff, 0x80, 0x1f, 0xff, 0x80, 
	  0x1f, 0xff, 0x80, 0x1f, 0xff, 0x80, 0x00, 0x00, 0x00, 0x1f, 0xff, 0x80
  };

  const unsigned char ultrasonicIndicator [] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x01, 0xf8, 0x00, 0x03, 0xfc, 0x00, 0x03, 0xfc, 0x00, 0x03, 
  	0xfc, 0x00, 0x03, 0xfc, 0x00, 0x03, 0xfc, 0x00, 0x03, 0xfc, 0x00, 0x01, 0xf8, 0x00, 0x01, 0xf8, 
  	0x00, 0x01, 0xf0, 0x00, 0x00, 0xf0, 0x00, 0x00, 0xf0, 0x00, 0x07, 0xfe, 0x00, 0x1f, 0xff, 0x80, 
	  0x3f, 0xff, 0xc0, 0x7f, 0xff, 0xe0, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xf0
  };  while (true) {
    Serial.println("indicatorDisplay: Waiting for temperature reading...");
    
    // Try to receive temperature data with timeout
    if (xQueueReceive(tempReading, (void*)&tempValue, pdMS_TO_TICKS(5000)) == pdTRUE) {
      Serial.print("indicatorDisplay: Received temperature: ");
      Serial.println(tempValue);
    } else {
      Serial.println("indicatorDisplay: Timeout waiting for temperature, using default value");
      tempValue = 25; // Default temperature
    }
    
    display.clearDisplay();                                     // Clear the display
    
    /* Displaying temperature value */
    display.setTextColor(WHITE);                                // Set the color
    display.setTextSize(2);                                     // Set the font size
    display.setCursor(10,10);                                    // Set the cursor coordinates
    display.print("Temp ");
    display.print(tempValue);
    display.print((char)247);
    display.print("C");
    
    if (fanStatus == true) {
      display.drawBitmap(2, 36, fanIndicator, 20, 20, WHITE);
    }
    else if (fanStatus == false) {
      display.setCursor(6, 38);
      display.print("-");
    }

    if (lightStatus == true) {
      display.drawBitmap(28, 36, lightIndicator, 20, 20, WHITE);
    }
    else if (lightStatus == false) {
      display.setCursor(32, 38);
      display.print("-");
    }
    
    if (smokeStatus == true) {
      display.drawBitmap(54, 36, smokeIndicator, 20, 20, WHITE);
    }
    else if (smokeStatus == false) {
      display.setCursor(58, 38);
      display.print("-");
    }
    
    if (touchStatus == true) {
      display.drawBitmap(80, 36, touchIndicator, 20, 20, WHITE);
    }
    else if (touchStatus == false) {
      display.setCursor(84, 38);
      display.print("-");
    }
    
    if (ultrasonicStatus == true) {
      display.drawBitmap(106, 36, ultrasonicIndicator, 20, 20, WHITE);
    }
    else if (ultrasonicStatus == false) {
      display.setCursor(110, 38);
      display.print("-");
    }
    
    display.display();

    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}  

void introDisplay() {
  Serial.println("Starting OLED intro display...");
  
  // Simple startup screen - no complex bitmap
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(10, 10);
  display.println("Smart");
  display.setCursor(10, 30);
  display.println("Home");
  display.display();
  Serial.println("Smart Home text displayed");
  delay(3000);
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(5, 20);
  display.println("Automation System");
  display.setCursor(20, 35);
  display.println("Starting...");
  display.display();
  Serial.println("Starting message displayed");
  delay(2000);
  
  display.clearDisplay();
  Serial.println("OLED intro display completed");
}
/*
* ---------------------------------------------------------------------------------------------------------------------------------
* Setup  
* ---------------------------------------------------------------------------------------------------------------------------------
*/

void setup() {
  // Disable brownout detector first thing
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  Serial.begin(115200);                                     // Serial baud rate
  Serial.println("Starting ESP32 Smart Home Automation...");
  
  // Initialize I2C with SDA=21, SCL=22 BEFORE any other I2C device initialization
  Wire.begin(21, 22);                                       
  Serial.println("I2C initialized");
  
  // Initialize OLED display with error checking
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }  Serial.println("OLED display initialized successfully!");
  
  // Test OLED with intro display first, before other initializations
  Serial.println("Starting intro display...");
  introDisplay();  Serial.println("Intro display completed");
  
  // Initialize other components
  Serial.println("Skipping Bluetooth initialization for now...");
  // SerialBT.begin("ESP32");  // Temporarily commented out - causes hanging
  // Serial.println("The device started, now you can pair it with bluetooth!");
  
  Serial.println("Initializing DHT sensor...");
  dht.begin();
  Serial.println("DHT sensor initialized");
  
  Serial.println("Initializing ultrasonic timer...");
  ultrasonic.attach(1, ultrasonicDetect);
  Serial.println("Ultrasonic timer initialized");  /* Defining pin modes and initializing servo and relay */  
  fanServoMotor.attach(fanServo);  // Attach servo to pin 17
  Serial.println("Fan servo motor attached to pin 17");
  
  pinMode(lightRelay, OUTPUT);     // Configure relay pin for LED control
  Serial.println("Light relay configured on pin 16");
  
  pinMode(touchButton, INPUT_PULLUP);  // Configure button with pull-up resistor
  Serial.println("Touch button configured on pin 4");
  
  pinMode(smokeLed, OUTPUT);
  pinMode(touchLed, OUTPUT);
  pinMode(ultrasonicLed, OUTPUT);
  pinMode(smokeBuzzer, OUTPUT);
  pinMode(touchBuzzer, OUTPUT);
  pinMode(trigger, OUTPUT);
  pinMode(echo, INPUT);
  Serial.println("Pin modes configured");
  /* Initial states */
  fanServoMotor.write(0);  // Set servo to 0° (fan off position)
  Serial.println("Fan servo set to OFF position (0°)");
  
  turnOffLight();  // Turn off LED light initially
  Serial.println("LED light initialized to OFF state");                            
  
  /* Buzzers off at start */
  digitalWrite(touchBuzzer, LOW);                            
  digitalWrite(smokeBuzzer, LOW);                            

  /* Leds off at start */
  digitalWrite(smokeLed, LOW);                           
  digitalWrite(touchLed, LOW);                                   
  digitalWrite(ultrasonicLed, LOW);
  Serial.println("Initial pin states set");


  /* Creating queues */
  tempReading = xQueueCreate(10, sizeof(int));
  lightReading = xQueueCreate(10, sizeof(int));
  Serial.println("Queues created");
    /* Creating tasks */  
  Serial.println("Creating FreeRTOS tasks...");
  xTaskCreatePinnedToCore (tempRead, "Temp read", 2048, NULL, 1, NULL, app_cpu);
  xTaskCreatePinnedToCore (lightRead, "Light read", 2048, NULL, 1, NULL, app_cpu);
  xTaskCreatePinnedToCore (autoLight, "Auto light", 2048, NULL, 1, &autoLight_handle, app_cpu);
  xTaskCreatePinnedToCore (smokeDetect, "Smoke detect", 2048, NULL, 1, NULL, app_cpu);
  xTaskCreatePinnedToCore (touchDetect, "Touch read", 2048, NULL, 1, NULL, app_cpu);
  xTaskCreatePinnedToCore (switchControl, "Switch control", 2048, NULL, 1, NULL, app_cpu);
  xTaskCreatePinnedToCore (indicatorDisplay, "OLED display", 2048, NULL, 1, NULL, app_cpu);
  Serial.println("All tasks created successfully");
    
  Serial.println("✓ Smart home automation system ready!");
  Serial.println("✓ Temperature control: AUTO mode (fan starts at 33°C)");
  Serial.println("✓ Light control: AUTO mode (light on when dark)");
  Serial.println("✓ Security system: ENABLED (intrusion detection active)");
  
  /* Initialize WiFi and Web Server */
  Serial.println("Setting up WiFi and Web Server...");
  
  // Scan for available networks first
  Serial.println("Scanning for WiFi networks...");
  int networkCount = WiFi.scanNetworks();
  if (networkCount == 0) {
    Serial.println("No networks found!");
  } else {
    Serial.print("Found ");
    Serial.print(networkCount);
    Serial.println(" networks:");
    for (int i = 0; i < networkCount; i++) {
      Serial.print("  ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" dBm) ");
      Serial.println(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Encrypted");
    }
  }
  
  // WiFi setup with improved connection logic
  Serial.println("Connecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);  // Set WiFi to station mode
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {  // Increased timeout to 15 seconds
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Print connection status every 5 attempts
    if (attempts % 5 == 0) {
      Serial.println();
      Serial.print("Connection attempt ");
      Serial.print(attempts);
      Serial.print("/30, Status: ");
      switch(WiFi.status()) {
        case WL_IDLE_STATUS: Serial.println("WL_IDLE_STATUS"); break;
        case WL_NO_SSID_AVAIL: Serial.println("WL_NO_SSID_AVAIL - Network not found"); break;
        case WL_SCAN_COMPLETED: Serial.println("WL_SCAN_COMPLETED"); break;
        case WL_CONNECTED: Serial.println("WL_CONNECTED"); break;
        case WL_CONNECT_FAILED: Serial.println("WL_CONNECT_FAILED - Wrong password?"); break;
        case WL_CONNECTION_LOST: Serial.println("WL_CONNECTION_LOST"); break;
        case WL_DISCONNECTED: Serial.println("WL_DISCONNECTED"); break;
        default: Serial.println("Unknown status"); break;
      }
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("Access your Smart Home at: http://" + WiFi.localIP().toString());
    
    // Setup web server routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/html", webPageHTML);
    });
    
    // API endpoint for sensor data
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
      JsonDocument doc;
      
      doc["temperature"] = currentTemperature;
      doc["lightLevel"] = currentLightLevel;
      doc["smokeLevel"] = currentSmokeLevel;
      doc["distance"] = currentDistance;
      doc["fanStatus"] = fanStatus;
      doc["lightStatus"] = lightStatus;
      doc["touchStatus"] = touchStatus;
      doc["smokeStatus"] = smokeStatus;
      doc["ultrasonicStatus"] = ultrasonicStatus;
      doc["securityEnabled"] = securitySystemEnabled;
      doc["autoMode"] = autoMode;
      
      String response;
      serializeJson(doc, response);
      request->send(200, "application/json", response);
    });
    
    // API endpoint for controls
    server.on("/control", HTTP_POST, [](AsyncWebServerRequest *request){
      request->send(200, "application/json", "{\"status\":\"received\"}");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      deserializeJson(doc, (char*)data);
      
      String action = doc["action"];
      
      if (action == "toggleFan") {
        if (manualFanControl) {
          // Toggle manual fan control
          if (servoState == SERVO_OFF) {
            servoState = SERVO_SWEEP_UP;
            fanStatus = true;
            Serial.println("Web: Manual fan turned ON");
          } else {
            servoState = SERVO_OFF;
            fanStatus = false;
            Serial.println("Web: Manual fan turned OFF");
          }
        }
      }
      else if (action == "toggleLight") {
        if (manualLightControl) {
          // Toggle manual light control
          if (lightStatus) {
            turnOffLight();
            Serial.println("Web: Manual light turned OFF");
          } else {
            turnOnLight();
            Serial.println("Web: Manual light turned ON");
          }
        }
      }
      else if (action == "setSecurity") {
        securitySystemEnabled = doc["value"];
        Serial.println("Web: Security system " + String(securitySystemEnabled ? "ENABLED" : "DISABLED"));
      }
      else if (action == "resetAlarms") {
        resetTouchAlarm();
        // Reset smoke alarm if needed
        if (smokeStatus) {
          digitalWrite(smokeLed, LOW);
          noTone(smokeBuzzer);
          smokeStatus = false;
          Serial.println("Web: All alarms reset");
        }
      }
      else if (action == "setFanMode") {
        String mode = doc["value"];
        if (mode == "auto") {
          manualFanControl = false;
          autoMode = true;
          Serial.println("Web: Fan set to AUTO mode");
        } else {
          manualFanControl = true;
          autoMode = false;
          Serial.println("Web: Fan set to MANUAL mode");
        }
      }
      else if (action == "setLightMode") {
        String mode = doc["value"];
        if (mode == "auto") {
          manualLightControl = false;
          Serial.println("Web: Light set to AUTO mode");
        } else {
          manualLightControl = true;
          Serial.println("Web: Light set to MANUAL mode");
        }
      }
    });
    
    server.begin();
    Serial.println("Web server started successfully!");
    Serial.println("Web interface ready!");
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed! Starting in offline mode.");
  }
  
  Serial.println("Setup completed successfully!");
}

void loop() {
  // Update servo sweep in main loop to ensure it's always running
  updateServoSweep();
  vTaskDelay(5 / portTICK_PERIOD_MS);  // Small delay to prevent overwhelming the system
  
}

