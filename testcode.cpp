#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// OLED configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Ultrasonic sensor pins
#define TRIG_PIN 15
#define ECHO_PIN 2

// DHT22 configuration
#define DHT_PIN 33
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// Gas sensor pin
#define GAS_PIN 25
#define GAS_THRESHOLD 3000 // Analog value for ~800 ppm, considered unsafe

// Photoresistor pin
#define LDR_PIN 26

// Buzzer pin
#define BUZZER_PIN 14

// Display mode for cycling through sensor data
int displayMode = 0;

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  Serial.println("Starting ESP32 Test...");

  // Initialize I2C for OLED
  Wire.begin(21, 22); // SDA = 21, SCL = 22

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Halt if OLED fails
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ESP32 Test");
  display.println("Sensors: OK");
  display.display();
  delay(2000); // Show startup message for 2 seconds

  // Initialize ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize DHT22
  dht.begin();

  // Initialize gas sensor pin
  pinMode(GAS_PIN, INPUT);

  // Initialize photoresistor pin
  pinMode(LDR_PIN, INPUT);

  // Initialize buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off initially

  Serial.println("Setup complete. Starting loop...");
}

float readDistance() {
  // Trigger the ultrasonic sensor
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Measure the echo pulse duration
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // Timeout after 30ms

  // Calculate distance (in cm)
  if (duration == 0) {
    return -1.0; // Indicate error (no echo or timeout)
  }
  float distance = duration * 0.034 / 2; // Speed of sound = 340 m/s
  return distance;
}

void loop() {
  // Read distance from ultrasonic sensor
  float distance = readDistance();

  // Read temperature and humidity from DHT22
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Read gas sensor value
  int gasValue = analogRead(GAS_PIN);

  // Read photoresistor value
  int ldrValue = analogRead(LDR_PIN);

  // Control buzzer based on gas level
  if (gasValue > GAS_THRESHOLD) {
    tone(BUZZER_PIN, 1000); // 1000 Hz tone for alert
    Serial.println("ALERT: High gas level detected!");
  } else {
    noTone(BUZZER_PIN); // Turn off buzzer
  }

  // Output to Serial Monitor
  Serial.println("=== Sensor Readings ===");
  if (distance >= 0 && distance <= 400) {
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" cm");
  } else {
    Serial.println("Distance: Error");
  }

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("DHT22: Error");
  } else {
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" C");
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  }

  Serial.print("Gas Sensor: ");
  Serial.print(gasValue);
  Serial.println(" (analog)");
  if (gasValue > GAS_THRESHOLD) {
    Serial.println("  [WARNING: Gas level above safe threshold]");
  }

  Serial.print("Photoresistor: ");
  Serial.print(ldrValue);
  Serial.println(" (analog)");
  Serial.println();

  // Update OLED display (cycle through modes)
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("ESP32 Sensor Test");

  switch (displayMode) {
    case 0: // Ultrasonic sensor
      display.println("HC-SR04:");
      if (distance >= 0 && distance <= 400) {
        display.print("Distance: ");
        display.print(distance);
        display.println(" cm");
      } else {
        display.println("Error: No reading");
      }
      break;

    case 1: // DHT22
      display.println("DHT22:");
      if (isnan(humidity) || isnan(temperature)) {
        display.println("Error: No reading");
      } else {
        display.print("Temp: ");
        display.print(temperature);
        display.println(" C");
        display.print("Hum: ");
        display.print(humidity);
        display.println(" %");
      }
      break;

    case 2: // Gas sensor
      display.println("Gas Sensor:");
      display.print("Value: ");
      display.print(gasValue);
      display.println(" (analog)");
      if (gasValue > GAS_THRESHOLD) {
        display.println("WARNING: High Gas!");
      }
      break;

    case 3: // Photoresistor
      display.println("Photoresistor:");
      display.print("Value: ");
      display.print(ldrValue);
      display.println(" (analog)");
      break;
  }

  display.display(); // Update OLED

  // Cycle through display modes
  displayMode = (displayMode + 1) % 4;
  delay(2000); // Update every 2 seconds
}