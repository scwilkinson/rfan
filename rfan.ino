// ---- Libraries ----
#include <WiFi.h>
#include <AsyncTCP.h>
#include <AsyncMqttClient.h>
#include "RFM69Dreo.h"
#include <esp_wifi.h>

// ---- RFM69 Pin Configuration ----
RFM69Dreo::PinConfig rfmPins = {
  .cs   = 7,   // SPI Chip Select
  .rst  = 6,   // Reset
  .dio2 = 0,   // Data I/O
  .irq  = 5    // Interrupt Request
};
RFM69Dreo dreo(rfmPins);

// ---- WiFi Configuration ----
const char* WIFI_SSID     = "SC - Home";
const char* WIFI_PASSWORD = "28848882";

// ---- MQTT Configuration ----
const char* MQTT_SERVER   = "10.27.27.220";  // IP to avoid mDNS issues
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_USER     = "sam";
const char* MQTT_PASS     = "CometComet1";

// ---- MQTT Topics ----
const char* CMD_TOPIC           = "home/ceilingfan/light/set";
const char* STATE_TOPIC         = "home/ceilingfan/light/state";
const char* AVAILABILITY_TOPIC  = "home/ceilingfan/light/availability";
const char* DISCOVERY_TOPIC     = "homeassistant/light/ceilingfan/config";

// Home Assistant Discovery Payload
const char* DISCOVERY_PAYLOAD = R"({
  "name": "Ceiling Fan Light",
  "unique_id": "esp32_ceilingfan_light",
  "state_topic": "home/ceilingfan/light/state",
  "command_topic": "home/ceilingfan/light/set",
  "availability_topic": "home/ceilingfan/light/availability",
  "payload_available": "online",
  "payload_not_available": "offline",
  "qos": 1,
  "payload_on": "ON",
  "payload_off": "OFF",
  "retain": true
})";

// ---- Global Variables ----
AsyncMqttClient mqttClient;
bool lightOn = false; // Optimistic state

// ---- Function Prototypes ----
void initializeSerial();
void initializeLed();
void initializeRFM69();
void initializeWiFi();
void initializeMQTT();
void connectToMqtt();
void onWifiGotIP(arduino_event_id_t event, arduino_event_info_t info);
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties props, size_t len, size_t index, size_t total);
void processLightToggleCommand();

// ---- Setup Function ----
void setup() {
  initializeSerial();
  initializeLed();
  initializeRFM69();
  initializeWiFi();
  initializeMQTT();
}

// ---- Main Loop ----
void loop() {
  // AsyncMqttClient handles operations in the background.
}

// ---- Initialization Functions ----
void initializeSerial() {
  Serial.begin(115200);
  Serial.println("\nBooting Ceiling Fan Light Controller...");
}

void initializeLed() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void initializeRFM69() {
  Serial.println("Initializing RFM69...");
  if (!dreo.begin()) {
    Serial.println("RFM69 init failed! Halting.");
    while (true) { // Blink LED for critical failure
      digitalWrite(LED_BUILTIN, HIGH); delay(100);
      digitalWrite(LED_BUILTIN, LOW);  delay(100);
    }
  }
  Serial.println("RFM69 ready.");
}

void initializeWiFi() {
  Serial.printf("Connecting to WiFi: %s...\n", WIFI_SSID);
  WiFi.onEvent(onWifiGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  esp_wifi_set_ps(WIFI_PS_NONE); // Disable WiFi power-saving for MQTT stability
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void initializeMQTT() {
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASS);
  mqttClient.setWill(AVAILABILITY_TOPIC, 1, true, "offline"); // LWT
  mqttClient.setKeepAlive(60);
}

// ---- WiFi Event Callback ----
void onWifiGotIP(arduino_event_id_t event, arduino_event_info_t info) {
  Serial.printf("WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.println("Connecting to MQTT...");
  connectToMqtt();
}

// ---- MQTT Connection Logic ----
void connectToMqtt() {
  if (WiFi.isConnected()) {
    mqttClient.connect();
  }
}

// ---- MQTT Event Callbacks ----
void onMqttConnect(bool sessionPresent) {
  Serial.println("▶ MQTT connected.");

  mqttClient.publish(AVAILABILITY_TOPIC, 1, true, "online");
  Serial.printf("Published availability to: %s\n", AVAILABILITY_TOPIC);

  mqttClient.publish(DISCOVERY_TOPIC, 1, true, DISCOVERY_PAYLOAD);
  Serial.printf("Published HA discovery to: %s\n", DISCOVERY_TOPIC);

  mqttClient.subscribe(CMD_TOPIC, 1);
  Serial.printf("Subscribed to command topic: %s\n", CMD_TOPIC);

  const char* currentLightState = lightOn ? "ON" : "OFF";
  mqttClient.publish(STATE_TOPIC, 1, true, currentLightState);
  Serial.printf("Published initial state '%s' to: %s\n", currentLightState, STATE_TOPIC);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.print("⚠ MQTT disconnected. Reason ID: ");
  Serial.println((int8_t)reason); // Log reason code for debugging
  Serial.println("Retrying MQTT connection in 5s...");
  delay(5000);
  if (WiFi.isConnected()) {
    connectToMqtt();
  } else {
    Serial.println("WiFi not connected. MQTT retry deferred.");
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties props,
                   size_t len, size_t index, size_t total) {
  String msg;
  msg.reserve(len + 1);
  for (size_t i = 0; i < len; i++) {
    msg += (char)payload[i];
  }
  msg.trim();

  Serial.printf("📥 MQTT Rx: %s → %s\n", topic, msg.c_str());

  if (String(topic).equals(CMD_TOPIC)) {
    if (msg.equalsIgnoreCase("ON") ||
        msg.equalsIgnoreCase("OFF") ||
        msg.equalsIgnoreCase("TOGGLE")) {
      processLightToggleCommand();
    } else {
      Serial.printf("Unknown command '%s' on topic %s.\n", msg.c_str(), topic);
    }
  }
}

// ---- Command Processing Function ----
void processLightToggleCommand() {
  Serial.println("Processing light toggle...");

  dreo.sendCommand(RFM69Dreo::LIGHT_ON_OFF); // Assumes this is a toggle command
  Serial.println("RF toggle command sent.");

  digitalWrite(LED_BUILTIN, HIGH); delay(100);
  digitalWrite(LED_BUILTIN, LOW);

  lightOn = !lightOn;
  const char* newState = lightOn ? "ON" : "OFF";

  mqttClient.publish(STATE_TOPIC, 1, true, newState);
  Serial.printf("📤 Published new state '%s' to: %s\n", newState, STATE_TOPIC);
}
