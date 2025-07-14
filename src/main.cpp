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
#define lightSensor 26            // LDR sensor
#define smokeSensor 25            // MQ2 smoke and gas sensor
#define touchSensor 4             // Touch sensor (GPIO 4)
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

/* Defining objects */
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BluetoothSerial SerialBT;                                 
Ticker ultrasonic;
Servo fanServoMotor;  // Servo object for fan simulation

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
    Serial.print("Raw DHT reading: ");
    Serial.println(t);
    
    if (isnan(t)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      // Don't return, just continue trying
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      continue;
    }    
    /* Send temperature values via bluetooth */
    // SerialBT.print("#");    // Commented out - Bluetooth disabled
    // SerialBT.print(t);      // Commented out - Bluetooth disabled
    // SerialBT.print("?");    // Commented out - Bluetooth disabled
      /* Print temperature and humidity values on serial monitor */
    Serial.print("Temperature: "); 
    Serial.print(t); 
    Serial.println(" °C");
    Serial.println("Sending temperature to queue...");
    xQueueSend (tempReading, (void*)&t, 10);
    
    // Non-blocking fan control based on temperature
    if (t >= 33) {
      if (servoState == SERVO_OFF) {
        Serial.println("Temperature high - starting continuous fan sweep pattern");
        servoState = SERVO_SWEEP_UP;
        servoCurrentPos = 0;
        fanStatus = true;
      }
    }
    else if (t < 33) {
      if (servoState != SERVO_OFF) {
        Serial.println("Temperature normal - stopping fan sweep");
        servoState = SERVO_OFF;
        fanStatus = false; 
      }
    }
    
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
    Serial.print("Light intensity: ");
    Serial.println(lightValue);

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
    Serial.println("autoLight: Waiting for light reading...");
    xQueueReceive(lightReading, (void *)&lightValue, portMAX_DELAY);    
    Serial.print("autoLight: Received light value: ");
    Serial.println(lightValue);
    
    if (lightValue >= 2200) {
      // SerialBT.print("Bulb on?");  // Bluetooth disabled
      // if (!lightStatus) {  // Only turn on if currently off
        Serial.print("Light level low (");
        Serial.print(lightValue);
        Serial.println(") - turning ON LED light");
        turnOnLight();
      // }
    }
    else if (lightValue < 2200) {
      // SerialBT.print("Bulb off?");  // Bluetooth disabled
      // if (lightStatus) {  // Only turn off if currently on
        Serial.print("Light level sufficient (");
        Serial.print(lightValue);
        Serial.println(") - turning OFF LED light");
        turnOffLight();
      // }
    }
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
    Serial.print("Smoke: ");
    Serial.println(smokeValue);
    
    if (smokeValue >= 3200) {
      Serial.println("SMOKE DETECTED! Activating buzzer and LED");
      // SerialBT.print("Smoke active?");  // Bluetooth disabled
      digitalWrite(smokeLed, HIGH);
      tone(smokeBuzzer, 2000);  // Try 2000Hz instead
      smokeStatus = true;      
    }
    else if (smokeValue < 3200) {
      Serial.println("Smoke level normal, turning off buzzer and LED");
      // SerialBT.print("Smoke inactive?");  // Bluetooth disabled
      digitalWrite(smokeLed, LOW);
      noTone(smokeBuzzer);  // Turn off tone
      smokeStatus = false; 
    }
    
    // Update servo sweep to maintain smooth operation
    updateServoSweep();
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);      
  }
}

/* Task for detecting touch using inbuilt touch sensor */
void touchDetect(void *parameter) {
  int touchValue;
  
  while (true) {    touchValue = (touchRead(touchSensor));  
    if (touchValue < 20) {
      // SerialBT.print("Touch active?");  // Bluetooth disabled
      digitalWrite(touchLed, HIGH);
      digitalWrite(touchBuzzer, HIGH);
      touchStatus = true; 
    }
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

  Serial.print("Distance: ");
  Serial.println(distance);
  if (distance > 20) {
    // SerialBT.print("Ultrasonic inactive?");  // Bluetooth disabled
    digitalWrite(ultrasonicLed, LOW);
    ultrasonicStatus = false;
  }
  else if (distance <= 20) {
    // SerialBT.print("Ultrasonic active?");  // Bluetooth disabled
    digitalWrite(ultrasonicLed, HIGH);
    ultrasonicStatus = true;
  }
}

/*
* ---------------------------------------------------------------------------------------------------------------------------------
* App based switch controls
* ---------------------------------------------------------------------------------------------------------------------------------
*/

/* Task for controlling relays and alarms using app */
void switchControl(void *parameter) {
  // Bluetooth disabled temporarily - this task won't do anything for now
  while (true) {
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Just wait
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
    /* Creating tasks */  Serial.println("Creating FreeRTOS tasks...");
  xTaskCreatePinnedToCore (tempRead, "Temp read", 1024, NULL, 1, NULL, app_cpu);
  Serial.println("Created tempRead task");
  
  // xTaskCreatePinnedToCore (autoFan, "Auto fan", 1024, NULL, 1, &autoFan_handle, app_cpu);
  // Serial.println("Created autoFan task - DISABLED, fan control moved to tempRead task");
  
  xTaskCreatePinnedToCore (lightRead, "Light read", 1024, NULL, 1, NULL, app_cpu);
  Serial.println("Created lightRead task");
  
  xTaskCreatePinnedToCore (autoLight, "Auto light", 1024, NULL, 1, &autoLight_handle, app_cpu);
  Serial.println("Created autoLight task");
  
  xTaskCreatePinnedToCore (smokeDetect, "Smoke detect", 1024, NULL, 1, NULL, app_cpu);
  Serial.println("Created smokeDetect task");
  
  xTaskCreatePinnedToCore (touchDetect, "Touch read", 1024, NULL, 1, NULL, app_cpu);
  Serial.println("Created touchDetect task");
  
  xTaskCreatePinnedToCore (switchControl, "Switch control", 2048, NULL, 1, NULL, app_cpu);
  Serial.println("Created switchControl task");
  
  xTaskCreatePinnedToCore (indicatorDisplay, "OLED display", 2048, NULL, 1, NULL, app_cpu);
  Serial.println("Created indicatorDisplay task");
  Serial.println("All tasks created");
    
  // vTaskSuspend (autoFan_handle);     // autoFan task disabled - fan control moved to tempRead task
  // vTaskSuspend (autoLight_handle);   // Light control is now ACTIVE with relay
  Serial.println("Auto LIGHT mode is ACTIVE with relay control, Fan control integrated in temperature task");
  Serial.println("Setup completed successfully!");
}

void loop() {
  // Update servo sweep in main loop to ensure it's always running
  updateServoSweep();
  vTaskDelay(5 / portTICK_PERIOD_MS);  // Small delay to prevent overwhelming the system
  
}

