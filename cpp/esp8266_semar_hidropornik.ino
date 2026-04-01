/*
 * ESP8266 Water Tank Monitor & Auto-Pump Controller
 * 
 * Components:
 * - ESP8266 (NodeMCU/Wemos D1 Mini)
 * - DHT22 Temperature & Humidity Sensor
 * - HC-SR04 Ultrasonic Distance Sensor
 * - 5V Relay Module (for water pump control)
 * 
 * Features:
 * - Web dashboard with real-time sensor data
 * - MQTT publishing of sensor data (topic: sensor/data)
 * - MQTT publishing of pump status (topic: pump/status)
 * - Auto pump control based on water level (ultrasonic sensor)
 * - Pump activates when distance > 5cm (low water level)
 * 
 * Required Libraries (install via Arduino Library Manager):
 * - ESP8266WiFi (built-in with ESP8266 board package)
 * - ESP8266WebServer (built-in with ESP8266 board package)
 * - DHT sensor library by Adafruit
 * - Adafruit Unified Sensor
 * - PubSubClient by Nick O'Leary (for MQTT)
 * 
 * Board Setup:
 * - Tools -> Board -> ESP8266 Boards -> NodeMCU 1.0 (or your variant)
 * - Install ESP8266 board package: http://arduino.esp8266.com/stable/package_esp8266com_index.json
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <PubSubClient.h>

// ==================== CONFIGURATION ====================
// WiFi credentials
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";

// MQTT Configuration
const char* MQTT_BROKER = "";
const int MQTT_PORT = ;
const char* MQTT_CLIENT_ID = "";
const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";

// MQTT Topics
const char* TOPIC_SENSOR_DATA = "sensor/data";
const char* TOPIC_PUMP_STATUS = "pump/status";

// Pin Configuration
// D0 = GPIO16, D1 = GPIO5, D2 = GPIO4, D3 = GPIO0, D4 = GPIO2
// D6 = GPIO12, D7 = GPIO13, D8 = GPIO15
const int DHT_PIN = D4;  
const int RELAY_PIN = D2;
const int ULTRASONIC_PIN = D6;
const int LED_PIN = D0;

// Water Level Threshold
const float WATER_LEVEL_THRESHOLD_CM = 5.0;

// Timing Configuration
const unsigned long SENSOR_READ_INTERVAL = 2000;
const unsigned long MQTT_PUBLISH_INTERVAL = 5000;
const unsigned long WIFI_TIMEOUT_MS = 15000;
const int WIFI_RETRIES = 5;

// ==================== GLOBALS ====================
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);
ESP8266WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Sensor data
float temperature = NAN;
float humidity = NAN;
float distanceCm = NAN;

// State
bool pumpState = false;
bool lastPumpState = false;
unsigned long lastSensorRead = 0;
unsigned long lastMqttPublish = 0;
int sensorFailCount = 0;
const int MAX_SENSOR_FAILS = 5;  // Turn pump off after this many consecutive failures

// ==================== HTML TEMPLATE ====================
const char HTML_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Water Tank Monitor</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
*{box-sizing:border-box}
body{font-family:'Segoe UI',Arial,sans-serif;text-align:center;margin:0;padding:20px;background:#fff;min-height:100vh}
.container{background:#fff;padding:25px;max-width:450px;margin:0 auto;border:1px solid #000}
h1{color:#000;margin:0 0 5px 0;font-size:24px}
.subtitle{color:#333;font-size:14px;margin-bottom:20px}
h2{color:#000;font-size:16px;margin:25px 0 15px 0;padding-top:15px;border-top:1px solid #000}
.sensor-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:15px 0}
.sensor-box{padding:15px 10px;background:#fff;border:1px solid #000}
.sensor-box.wide{grid-column:span 2}
.sensor-label{font-size:11px;color:#333;text-transform:uppercase;letter-spacing:1px}
.sensor-value{font-size:32px;font-weight:bold;color:#000;margin:8px 0}
.sensor-unit{font-size:14px;color:#333}
.pump-status{padding:20px;margin:15px 0;transition:all .3s}
.pump-on{background:#d4edda;border:2px solid #28a745}
.pump-off{background:#f8d7da;border:2px solid #dc3545}
.pump-label{font-size:12px;color:#333;text-transform:uppercase}
.pump-state{font-size:28px;font-weight:bold;margin:5px 0}
.pump-on .pump-state{color:#28a745}
.pump-off .pump-state{color:#dc3545}
.indicator{width:12px;height:12px;border-radius:50%;display:inline-block;margin-right:8px;animation:pulse 1.5s infinite}
.pump-on .indicator{background:#28a745}
.pump-off .indicator{background:#dc3545}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.5}}
.info{font-size:11px;color:#333;margin-top:20px;padding:10px;background:#fff;border:1px solid #000}
.water-level{height:100px;background:linear-gradient(to top,#3498db 0%,#3498db var(--level),#fff var(--level),#fff 100%);margin:10px 0;position:relative;border:1px solid #000}
.water-level::after{content:attr(data-level);position:absolute;bottom:10px;left:50%;transform:translateX(-50%);font-weight:bold;color:#000}
</style></head><body>
<div class="container">
<h1>Certainly Someone's Hydroponic</h1> 

<h2>Environment</h2>
<div class="sensor-grid">
<div class="sensor-box">
<div class="sensor-label">Temperature</div>
<div class="sensor-value" id="temp">--</div>
<div class="sensor-unit">°C</div>
</div>
<div class="sensor-box">
<div class="sensor-label">Humidity</div>
<div class="sensor-value" id="humidity">--</div>
<div class="sensor-unit">%</div>
</div>
</div>

<h2>Water Level</h2>
<div class="sensor-grid">
<div class="sensor-box wide">
<div class="sensor-label">Distance to Water Surface</div>
<div class="sensor-value" id="distance">--</div>
<div class="sensor-unit">cm</div>
</div>
</div>
<div class="water-level" id="waterLevel" style="--level:50%" data-level="--"></div>

<h2>Pump Control</h2>
<div class="pump-status pump-off" id="pumpStatus">
<div class="pump-label"><span class="indicator"></span>Water Pump</div>
<div class="pump-state" id="pumpState">...</div>
</div>

<div class="info">
Pump activates automatically when water level is low (distance > 5cm).<br>
Data published to MQTT: sensor/data & pump/status
</div>
</div>

<script>
function updateUI(d){
  if(d.temp!==null)document.getElementById('temp').textContent=d.temp.toFixed(1);
  if(d.humidity!==null)document.getElementById('humidity').textContent=d.humidity.toFixed(1);
  if(d.distance!==null){
    document.getElementById('distance').textContent=d.distance.toFixed(1);
    // Calculate water level percentage (assuming max distance is 30cm = empty, 0cm = full)
    let level=Math.max(0,Math.min(100,100-(d.distance/30)*100));
    document.getElementById('waterLevel').style.setProperty('--level',level+'%');
    document.getElementById('waterLevel').setAttribute('data-level',level.toFixed(0)+'%');
  }
  let ps=document.getElementById('pumpStatus');
  let pst=document.getElementById('pumpState');
  if(d.pump){
    ps.className='pump-status pump-on';
    pst.textContent='RUNNING';
  }else{
    ps.className='pump-status pump-off';
    pst.textContent='OFF';
  }
}
function poll(){fetch('/status').then(r=>r.json()).then(updateUI).catch(e=>console.log(e))}
setInterval(poll,2000);
poll();
</script></body></html>
)rawliteral";

// ==================== ULTRASONIC SENSOR (3-WIRE MODE) ====================
float readUltrasonicDistance() {
    // 3-wire mode: Same pin used for TRIG and ECHO
    // Connect both TRIG and ECHO pins of HC-SR04 together to ULTRASONIC_PIN
    
    // Set pin as OUTPUT for trigger pulse
    pinMode(ULTRASONIC_PIN, OUTPUT);
    digitalWrite(ULTRASONIC_PIN, LOW);
    delayMicroseconds(2);
    
    // Send 10us pulse to trigger
    digitalWrite(ULTRASONIC_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(ULTRASONIC_PIN, LOW);
    
    // Switch to INPUT to read echo
    pinMode(ULTRASONIC_PIN, INPUT);
    
    // Read echo pulse duration (timeout after 30ms = ~500cm max)
    long duration = pulseIn(ULTRASONIC_PIN, HIGH, 30000);
    
    if (duration == 0) {
        Serial.println("Ultrasonic: No echo received");
        return NAN;
    }
    
    // Calculate distance in cm
    // Speed of sound = 343 m/s = 0.0343 cm/µs
    // Distance = (duration * 0.0343) / 2
    float distance = (duration * 0.0343) / 2.0;
    
    // Sanity check (HC-SR04 range: 2cm - 400cm)
    if (distance < 2.0 || distance > 400.0) {
        return NAN;
    }
    
    return distance;
}

// ==================== DHT22 SENSOR ====================
bool readDHT() {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    if (isnan(t) || isnan(h)) {
        Serial.println("DHT22: Read error");
        return false;
    }
    
    temperature = t;
    humidity = h;
    return true;
}

// ==================== READ ALL SENSORS ====================
void readAllSensors() {
    readDHT();
    distanceCm = readUltrasonicDistance();
    
    Serial.printf("Sensors: Temp=%.1f°C, Humidity=%.1f%%, Distance=%.1fcm\n",
                  temperature, humidity, distanceCm);
}

// ==================== PUMP CONTROL ====================
void setPump(bool on) {
    pumpState = on;
    // Active LOW relay: LOW = relay ON, HIGH = relay OFF
    digitalWrite(RELAY_PIN, pumpState ? LOW : HIGH);
    // LED is also active LOW
    digitalWrite(LED_PIN, pumpState ? LOW : HIGH);
}

void updatePumpState() {
    if (isnan(distanceCm)) {
        // Safety: if sensor fails too many times in a row, turn pump off
        sensorFailCount++;
        Serial.printf("Sensor fail count: %d/%d\n", sensorFailCount, MAX_SENSOR_FAILS);
        if (sensorFailCount >= MAX_SENSOR_FAILS && pumpState) {
            Serial.println("Too many sensor failures - turning pump OFF for safety");
            setPump(false);
            publishPumpStatus();
        }
        return;
    }
    
    // Reset fail counter on successful read
    sensorFailCount = 0;
    
    // Pump ON when distance > threshold (water level is low)
    // Pump OFF when distance <= threshold (water level is adequate)
    bool newPumpState = (distanceCm > WATER_LEVEL_THRESHOLD_CM);
    
    if (newPumpState != pumpState) {
        setPump(newPumpState);
        
        Serial.printf("Pump state changed: %s (distance: %.1fcm)\n",
                      pumpState ? "ON" : "OFF", distanceCm);
        
        // Publish pump status immediately when state changes
        publishPumpStatus();
    }
}

// ==================== WIFI ====================
bool wifiConnect() {
    Serial.println("\nConnecting to WiFi...");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    for (int attempt = 1; attempt <= WIFI_RETRIES; attempt++) {
        Serial.printf("Attempt %d/%d...\n", attempt, WIFI_RETRIES);
        
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > WIFI_TIMEOUT_MS) {
                Serial.println("Timeout");
                break;
            }
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("Connected! IP: ");
            Serial.println(WiFi.localIP());
            return true;
        }
        
        // Reset and retry
        WiFi.disconnect();
        delay(1000);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    
    Serial.println("WiFi connection failed!");
    return false;
}

// ==================== MQTT ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Handle incoming MQTT messages if needed
    Serial.printf("MQTT message on topic: %s\n", topic);
}

bool mqttConnect() {
    if (mqttClient.connected()) {
        return true;
    }
    
    Serial.print("Connecting to MQTT broker...");
    
    bool connected;
    if (strlen(MQTT_USER) > 0) {
        connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
    } else {
        connected = mqttClient.connect(MQTT_CLIENT_ID);
    }
    
    if (connected) {
        Serial.println("connected!");
        return true;
    } else {
        Serial.printf("failed (rc=%d)\n", mqttClient.state());
        return false;
    }
}

void publishSensorData() {
    if (!mqttClient.connected()) {
        if (!mqttConnect()) return;
    }
    
    // Build JSON payload
    String json = "{";
    json += "\"temperature\":";
    json += isnan(temperature) ? "null" : String(temperature, 1);
    json += ",\"humidity\":";
    json += isnan(humidity) ? "null" : String(humidity, 1);
    json += ",\"distance_cm\":";
    json += isnan(distanceCm) ? "null" : String(distanceCm, 1);
    json += ",\"pump\":";
    json += pumpState ? "true" : "false";
    json += "}";
    
    if (mqttClient.publish(TOPIC_SENSOR_DATA, json.c_str())) {
        Serial.printf("MQTT published to %s: %s\n", TOPIC_SENSOR_DATA, json.c_str());
    } else {
        Serial.println("MQTT publish failed");
    }
}

void publishPumpStatus() {
    if (!mqttClient.connected()) {
        if (!mqttConnect()) return;
    }
    
    const char* status = pumpState ? "on" : "off";
    
    if (mqttClient.publish(TOPIC_PUMP_STATUS, status)) {
        Serial.printf("MQTT published to %s: %s\n", TOPIC_PUMP_STATUS, status);
    } else {
        Serial.println("MQTT pump status publish failed");
    }
}

// ==================== WEB SERVER ====================
String getJsonStatus() {
    String json = "{";
    json += "\"temp\":";
    json += isnan(temperature) ? "null" : String(temperature, 1);
    json += ",\"humidity\":";
    json += isnan(humidity) ? "null" : String(humidity, 1);
    json += ",\"distance\":";
    json += isnan(distanceCm) ? "null" : String(distanceCm, 1);
    json += ",\"pump\":";
    json += pumpState ? "true" : "false";
    json += "}";
    return json;
}

void handleRoot() {
    server.send(200, "text/html", HTML_TEMPLATE);
}

void handleStatus() {
    server.send(200, "application/json", getJsonStatus());
}

void handleNotFound() {
    server.send(404, "text/plain", "Not Found");
}

void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/status", handleStatus);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("Web server started on port 80");
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n========================================");
    Serial.println("  Water Tank Monitor - ESP8266");
    Serial.println("========================================");
    
    // Initialize pins
    pinMode(LED_PIN, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);
    // Note: ULTRASONIC_PIN mode is set dynamically in readUltrasonicDistance()
    
    // Initial states
    digitalWrite(LED_PIN, HIGH);      // LED off (active LOW)
    digitalWrite(RELAY_PIN, HIGH);    // Relay off (active LOW: HIGH = off)
    pumpState = false;
    lastPumpState = false;
    
    // Relay self-test: toggle relay so you can hear/see it click
    Serial.println("Relay test: ON (you should hear a click)...");
    digitalWrite(RELAY_PIN, LOW);     // Active LOW: relay ON
    delay(1000);
    Serial.println("Relay test: OFF (another click)...");
    digitalWrite(RELAY_PIN, HIGH);    // Active LOW: relay OFF
    delay(1000);
    Serial.println("Relay test done. If no clicks heard, check wiring.");
    
    // Initialize DHT22
    dht.begin();
    Serial.println("DHT22 initialized");
    
    // Wait for DHT22 to stabilize
    delay(2000);
    
    // Connect to WiFi
    if (!wifiConnect()) {
        Serial.println("Restarting in 10 seconds...");
        delay(10000);
        ESP.restart();
    }
    
    // Setup MQTT
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttConnect();
    
    // Setup web server
    setupWebServer();
    
    // Initial sensor read
    readAllSensors();
    
    Serial.println("\n========================================");
    Serial.printf("  Web UI: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  MQTT Broker: %s:%d\n", MQTT_BROKER, MQTT_PORT);
    Serial.printf("  Pump threshold: %.1f cm\n", WATER_LEVEL_THRESHOLD_CM);
    Serial.println("========================================\n");
}

// ==================== LOOP ====================
void loop() {
    // Handle web server
    server.handleClient();
    
    // Maintain MQTT connection
    if (!mqttClient.connected()) {
        mqttConnect();
    }
    mqttClient.loop();
    
    // Read sensors periodically
    unsigned long now = millis();
    if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
        lastSensorRead = now;
        readAllSensors();
        updatePumpState();
    }
    
    // Publish to MQTT periodically
    if (now - lastMqttPublish >= MQTT_PUBLISH_INTERVAL) {
        lastMqttPublish = now;
        publishSensorData();
        
        // Also publish pump status periodically (in addition to on-change)
        if (pumpState != lastPumpState) {
            publishPumpStatus();
            lastPumpState = pumpState;
        }
    }
    
    yield();  // Allow ESP8266 background tasks
}
