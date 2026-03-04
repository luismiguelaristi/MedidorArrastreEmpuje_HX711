#include <Arduino.h>
#include "HX711.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = 25;
const int LOADCELL_SCK_PIN = 26;

// Firebeetle ESP32 built-in LED is on GPIO 2
const int STATUS_LED_PIN = 2;

// OLED 0.91" (128x32) I2C display
const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 32;
const int OLED_RESET_PIN = -1;   // Reset pin (or -1 if sharing ESP32 reset)
const uint8_t OLED_I2C_ADDRESS = 0x3C;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

HX711 scale;

float calibration_factor = -1820; // This value is obtained by calibration

// Non-blocking blink state
unsigned long previousBlinkMillis = 0;
const unsigned long blinkIntervalMs = 500;
bool statusLedState = false;

void setup() {
  Serial.begin(115200);
  Serial.println("HX711 calibration sketch");
  Serial.println("Remove all weight from scale");
  Serial.println("After readings begin, place known weight on scale");
  Serial.println("Press + or a to increase calibration factor");
  Serial.println("Press - or z to decrease calibration factor");
  Serial.println("Press t or T to tare");

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // Initialize OLED display over I2C
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("HX711 Scale");
    display.println("Init...");
    display.display();
  }

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale();
  scale.tare(); //Reset the scale to 0

  long zero_factor = scale.read_average(); //Get a baseline reading
  Serial.print("Zero factor: "); //This can be used to remove the need to tare the scale. Useful in permanent scale projects.
  Serial.println(zero_factor);
}

void loop() {
  // Non-blocking status LED blink
  unsigned long currentMillis = millis();
  if (currentMillis - previousBlinkMillis >= blinkIntervalMs) {
    previousBlinkMillis = currentMillis;
    statusLedState = !statusLedState;
    digitalWrite(STATUS_LED_PIN, statusLedState ? HIGH : LOW);
  }

  scale.set_scale(calibration_factor); //Adjust to this calibration factor

  float weight = scale.get_units(5); // average of 5 readings

  Serial.print("Reading: ");
  Serial.print(weight, 1);
  Serial.print(" g"); //Change this to kg and re-adjust the calibration factor if you follow SI units.
  Serial.print(" calibration_factor: ");
  Serial.print(calibration_factor);
  Serial.println();

  // Update OLED with current measurement
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("HX711 Scale");
  display.setTextSize(2);
  display.setCursor(0, 12);
  display.print(weight, 1);
  display.print(" g");
  display.display();

  if(Serial.available())
  {
    char temp = Serial.read();
    if(temp == '+' || temp == 'a')
      calibration_factor += 10;
    else if(temp == '-' || temp == 'z')
      calibration_factor -= 10;
    else if(temp == 't' || temp == 'T')
      scale.tare();
  }
}