// Web-server version: serves a browser page that logs data
// client-side and allows CSV download. The microcontroller only
// provides the latest measurement via HTTP; no history is stored
// on the ESP32.

#include <Arduino.h>
#include "HX711.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = 25;
const int LOADCELL_SCK_PIN = 26;

// Firebeetle ESP32 built-in LED is on GPIO 2
const int STATUS_LED_PIN = 2;

// Tare pushbutton on FireBeetle ESP32 (active LOW with INPUT_PULLUP)
const int TARE_BUTTON_PIN = 4;

// OLED 0.91" (128x32) I2C display
const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 32;
const int OLED_RESET_PIN = -1;   // Reset pin (or -1 if sharing ESP32 reset)
const uint8_t OLED_I2C_ADDRESS = 0x3C;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

HX711 scale;

float calibration_factor = -1820; // This value is obtained by calibration

// WiFi and UDP settings
const char* WIFI_SSID     = "UPB_RoboSub";      // TODO: set your WiFi SSID
const char* WIFI_PASSWORD = "amasd2025";  // TODO: set your WiFi password

// const char* WIFI_SSID     = "TP_Cabanota";      // TODO: set your WiFi SSID
// const char* WIFI_PASSWORD = "Wsx12345678";  // TODO: set your WiFi password


// HTTP server on port 80
WebServer server(80);

// Latest measurement (for /data endpoint only)
float lastWeight = 0.0f;
unsigned long lastTimestampMs = 0;

// Non-blocking blink state
unsigned long previousBlinkMillis = 0;
const unsigned long blinkIntervalMs = 500;
bool statusLedState = false;

// Debounce state for tare button
const unsigned long tareDebounceMs = 40;
bool tareButtonStableState = HIGH;
bool tareButtonLastReading = HIGH;
unsigned long tareButtonLastChangeMs = 0;

// Keep tare confirmation message on OLED for 1 second
unsigned long tareMessageUntilMs = 0;


// Simple HTML+JS page that logs data in the browser and
// lets the user download a CSV without storing history on the ESP32.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <title>Medidor de fuerza de arrastre</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 16px; background: #111; color: #eee; }
    h1 { margin-bottom: 0.2em; }
    .info { margin-bottom: 1em; }
    table { border-collapse: collapse; width: 100%; max-width: 800px; }
    th, td { border: 1px solid #444; padding: 4px 8px; font-size: 0.9em; }
    th { background: #222; }
    tr:nth-child(even) { background: #181818; }
    tr:nth-child(odd) { background: #101010; }
    button { margin-right: 8px; padding: 6px 12px; }
  </style>
</head>
<body>
  <h1>Medidor de fuerza de arrastre</h1>
  <div class="info">
    <div>Marca de tiempo (ms) y fuerza (g-f).</div>
    <div>Estado: <span id="status">Connecting...</span></div>
  </div>
  <div>
    <button id="startBtn">Iniciar registro</button>
    <button id="stopBtn" disabled>Detener registro</button>
    <button id="downloadBtn" disabled>Descargar CSV</button>
  </div>
  <p>Total samples: <span id="count">0</span></p>
  <table>
    <thead>
      <tr><th>#</th><th>Millis</th><th>Fueza (g-f)</th></tr>
    </thead>
    <tbody id="tbody"></tbody>
  </table>

  <script>
    let timer = null;
    let samples = [];

    const statusEl = document.getElementById('status');
    const countEl = document.getElementById('count');
    const tbody = document.getElementById('tbody');
    const startBtn = document.getElementById('startBtn');
    const stopBtn = document.getElementById('stopBtn');
    const downloadBtn = document.getElementById('downloadBtn');

    async function fetchSample() {
      try {
        const res = await fetch('/data');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        if (typeof data.millis === 'number' && typeof data.weight_g === 'number') {
          samples.push(data);
          const idx = samples.length;
          const row = document.createElement('tr');
          const c1 = document.createElement('td');
          const c2 = document.createElement('td');
          const c3 = document.createElement('td');
          c1.textContent = idx;
          c2.textContent = data.millis;
          c3.textContent = data.weight_g.toFixed(2);
          row.appendChild(c1);
          row.appendChild(c2);
          row.appendChild(c3);
          tbody.appendChild(row);
          countEl.textContent = samples.length;
          statusEl.textContent = 'Logging';
          downloadBtn.disabled = samples.length === 0;
        }
      } catch (e) {
        statusEl.textContent = 'Error: ' + e.message;
      }
    }

    function startLogging() {
      if (timer) return;
      statusEl.textContent = 'Starting...';
      timer = setInterval(fetchSample, 200); // poll every 200 ms
      startBtn.disabled = true;
      stopBtn.disabled = false;
    }

    function stopLogging() {
      if (timer) {
        clearInterval(timer);
        timer = null;
      }
      statusEl.textContent = 'Stopped';
      startBtn.disabled = false;
      stopBtn.disabled = true;
    }

    function downloadCsv() {
      if (!samples.length) return;
      let csv = 'millis,weight_g\n';
      for (const s of samples) {
        csv += `${s.millis},${s.weight_g}\n`;
      }
      const blob = new Blob([csv], { type: 'text/csv' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'hx711_log.csv';
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);
    }

    startBtn.addEventListener('click', startLogging);
    stopBtn.addEventListener('click', stopLogging);
    downloadBtn.addEventListener('click', downloadCsv);

    // Enable download when there are samples
    const observer = new MutationObserver(() => {
      downloadBtn.disabled = samples.length === 0;
    });
    observer.observe(countEl, { childList: true });

    statusEl.textContent = 'Ready';
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  // Respond with current latest sample as JSON.
  // History is kept only in the browser, not on the ESP32.
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"millis\":%lu,\"weight_g\":%.2f}",
           lastTimestampMs, lastWeight);
  server.send(200, "application/json", buf);
}

void setup() {
  Serial.begin(115200);
  Serial.println("HX711 web logger");

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  pinMode(TARE_BUTTON_PIN, INPUT_PULLUP);

  // Initialize OLED display over I2C
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("HX711 Scale");
    display.println("Init web...");
    display.display();
  }

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale();
  scale.tare(); // Reset the scale to 0

  long zero_factor = scale.read_average();
  Serial.print("Zero factor: ");
  Serial.println(zero_factor);

  // Connect to WiFi
  Serial.println();
  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long wifiStart = millis();
  const unsigned long wifiTimeoutMs = 20000; // 20s timeout

  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < wifiTimeoutMs) {
    delay(250);
    Serial.print(".");
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("HX711 Scale");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected, IP address: ");
    Serial.println(WiFi.localIP());

    display.setCursor(0, 8);
    display.print("WiFi ");
    display.print(WiFi.localIP());
    display.setCursor(0, 20);
    display.print("Web on port 80");
    display.display();

    // Configure HTTP server
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.begin();
    Serial.println("HTTP server started");
  } else {
    Serial.println();
    Serial.println("WiFi connection failed or timed out");

    display.setCursor(0, 8);
    display.print("WiFi FAILED");
    display.display();
  }
}

void loop() {
  // Non-blocking status LED blink
  unsigned long currentMillis = millis();
  if (currentMillis - previousBlinkMillis >= blinkIntervalMs) {
    previousBlinkMillis = currentMillis;
    statusLedState = !statusLedState;
    digitalWrite(STATUS_LED_PIN, statusLedState ? HIGH : LOW);
  }

  // Non-blocking button debounce and tare on press
  bool tareReading = digitalRead(TARE_BUTTON_PIN);
  if (tareReading != tareButtonLastReading) {
    tareButtonLastChangeMs = currentMillis;
    tareButtonLastReading = tareReading;
  }

  if ((currentMillis - tareButtonLastChangeMs) > tareDebounceMs && tareReading != tareButtonStableState) {
    tareButtonStableState = tareReading;
    if (tareButtonStableState == LOW) {
      scale.tare();
      tareMessageUntilMs = currentMillis + 1000;
      Serial.println("Tare by button");
    }
  }

  scale.set_scale(calibration_factor); //Adjust to this calibration factor

  float weight = scale.get_units(5); // average of 5 readings
  lastTimestampMs = millis();
  lastWeight = weight;

  Serial.print("Reading: ");
  Serial.print(weight, 1);
  Serial.print(" g");
  Serial.print(" calibration_factor: ");
  Serial.print(calibration_factor);
  Serial.println();

  // Update OLED with current measurement and WiFi status
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (currentMillis < tareMessageUntilMs) {
    display.setTextSize(2);
    display.setCursor(16, 8);
    display.print("TARE OK");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("HX711 Scale");

    // Second line: WiFi connection status and IP
    display.setCursor(0, 8);
    if (WiFi.status() == WL_CONNECTED) {
      display.print("WiFi ");
      display.print(WiFi.localIP());
    } else {
      display.print("WiFi DISCONNECTED");
    }

    // Weight on third line, larger font
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.print(weight, 1);
    display.print(" g");
  }

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(weight, 1);
  display.print(" g");
  display.display();

  // Handle HTTP requests
  server.handleClient();

  if (Serial.available()) {
    char temp = Serial.read();
    if (temp == '+' || temp == 'a')
      calibration_factor += 10;
    else if (temp == '-' || temp == 'z')
      calibration_factor -= 10;
    else if (temp == 't' || temp == 'T'){
      scale.tare();
      tareMessageUntilMs = currentMillis + 1000;
      Serial.println("Tare by serial");
    }
  }
}