#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "RFM69Dreo.h"

// WiFi credentials
const char* ssid = "SC - Home";
const char* password = "28848882";

// MQTT Broker settings (Home Assistant)
const char* mqtt_server = "homeassistant.local";
const int mqtt_port = 1883;
const char* mqtt_user = "sam";
const char* mqtt_password = "CometComet1";

// Device details
const char* device_name = "Dreo Ceiling Fan Light";
const char* device_id = "esp_rf_dreo_001";

// MQTT Topics for Home Assistant
char mqtt_topic_command[128];
char mqtt_topic_state[128];
char mqtt_topic_availability[128];
char mqtt_topic_config[128];
char mqtt_topic_brightness_command[128];  // Add brightness topic
char mqtt_topic_brightness_state[128];    // Add brightness state topic

// Configure pins for ESP32-C6
RFM69Dreo::PinConfig rfmPins = {
    .cs = D5,
    .rst = D4,
    .dio2 = D2,
    .irq = D1
};
RFM69Dreo dreo(rfmPins);

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;

// Connection management
unsigned long lastMqttAttempt = 0;
unsigned long lastWifiCheck = 0;
const unsigned long RECONNECT_INTERVAL = 5000;
const unsigned long WIFI_CHECK_INTERVAL = 10000;

// Device state (persisted)
struct DeviceState {
  bool lightOn;
  int brightness;
};
DeviceState currentState = {false, 153};

// Command timing
unsigned long lastCommandTime = 0;
const unsigned long COMMAND_DEBOUNCE = 200;

// Constants
const int BRIGHTNESS_STEPS = 5;
const int BRIGHTNESS_LEVELS[5] = {51, 102, 153, 204, 255};

void setup() {
  Serial.begin(115200);

  // Initialize LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Generate MQTT topics
  sprintf(mqtt_topic_command, "homeassistant/light/%s/set", device_id);
  sprintf(mqtt_topic_state, "homeassistant/light/%s/state", device_id);
  sprintf(mqtt_topic_availability, "homeassistant/light/%s/availability", device_id);
  sprintf(mqtt_topic_config, "homeassistant/light/%s/config", device_id);
  sprintf(mqtt_topic_brightness_command, "homeassistant/light/%s/brightness/set", device_id);
  sprintf(mqtt_topic_brightness_state, "homeassistant/light/%s/brightness", device_id);

  // Initialize preferences for state persistence
  preferences.begin("dreo", false);
  loadDeviceState();

  // Connect to WiFi
  setup_wifi();

  // Configure MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
  client.setBufferSize(1024);
  client.setKeepAlive(60);
  
  connectMqtt();

  // Initialize the RFM69 radio
  Serial.println(F("Initializing RFM69..."));
  if (dreo.begin()) {
      Serial.println(F("RFM69HCW initialized successfully"));
  } else {
      Serial.println(F("RFM69HCW initialization failed!"));
      while(1);
  }
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqtt_callback(char* topic, byte* message, unsigned int length) {
  // Debounce rapid commands
  unsigned long now = millis();
  if (now - lastCommandTime < COMMAND_DEBOUNCE) {
    return;
  }
  lastCommandTime = now;

  String messageTemp;
  for (int i = 0; i < length; i++) {
    messageTemp += (char)message[i];
  }
  
  Serial.printf("MQTT command: %s\n", messageTemp.c_str());

  if (String(topic) == mqtt_topic_command) {
    processCommand(messageTemp);
  }
}

void processCommand(String command) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, command);
  
  bool stateChanged = false;

  if (!error) {
    // Handle JSON commands from Home Assistant
    if (doc.containsKey("state")) {
      bool newState = (doc["state"] == "ON");
      if (newState != currentState.lightOn) {
        currentState.lightOn = newState;
        sendRfCommand(RFM69Dreo::LIGHT_ON_OFF);
        stateChanged = true;
      }
    }

    if (doc.containsKey("brightness")) {
      int targetBrightness = doc["brightness"];
      if (adjustBrightness(targetBrightness)) {
        stateChanged = true;
      }
    }
  }

  if (stateChanged) {
    saveDeviceState();
    publishState();
    updateBuiltinLed();
  }
}

bool adjustBrightness(int targetBrightness) {
  if (!currentState.lightOn) return false;

  targetBrightness = constrain(targetBrightness, 51, 255);
  
  // Find closest brightness level
  int targetStep = findClosestBrightnessStep(targetBrightness);
  int currentStep = findClosestBrightnessStep(currentState.brightness);
  
  if (targetStep == currentStep) return false;

  int stepsToMove = targetStep - currentStep;
  Serial.printf("Brightness: step %d->%d (%d->%d)\n", 
                currentStep, targetStep, currentState.brightness, BRIGHTNESS_LEVELS[targetStep]);

  // Send brightness commands with proper timing
  RFM69Dreo::Command cmd = (stepsToMove > 0) ? RFM69Dreo::LIGHT_UP : RFM69Dreo::LIGHT_DOWN;
  for (int i = 0; i < abs(stepsToMove); i++) {
    sendRfCommand(cmd);
    if (i < abs(stepsToMove) - 1) delay(150);  // Delay between commands
  }

  currentState.brightness = BRIGHTNESS_LEVELS[targetStep];
  return true;
}

int findClosestBrightnessStep(int brightness) {
  int closestStep = 0;
  int minDiff = 255;
  for (int i = 0; i < BRIGHTNESS_STEPS; i++) {
    int diff = abs(BRIGHTNESS_LEVELS[i] - brightness);
    if (diff < minDiff) {
      minDiff = diff;
      closestStep = i;
    }
  }
  return closestStep;
}

void publishState() {
  StaticJsonDocument<256> doc;
  doc["state"] = currentState.lightOn ? "ON" : "OFF";
  doc["brightness"] = currentState.brightness;

  char buffer[256];
  serializeJson(doc, buffer);
  
  if (client.publish(mqtt_topic_state, buffer, true)) {
    Serial.printf("State published: %s\n", buffer);
  } else {
    Serial.println("Failed to publish state");
  }
}

void publishDiscoveryConfig() {
  StaticJsonDocument<600> doc;

  doc["name"] = device_name;
  doc["unique_id"] = device_id;
  doc["command_topic"] = mqtt_topic_command;
  doc["state_topic"] = mqtt_topic_state;
  doc["availability_topic"] = mqtt_topic_availability;
  doc["brightness"] = true;  // Enable brightness support
  doc["brightness_scale"] = 255;  // 0-255 scale
  doc["schema"] = "json";  // Use JSON schema for commands
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
  doc["optimistic"] = false;

  // Device information
  JsonObject device = doc.createNestedObject("device");
  device["identifiers"][0] = device_id;
  device["name"] = "Dreo Controller";
  device["model"] = "ESP32-C6 + RFM69";
  device["manufacturer"] = "Custom";

  char buffer[600];
  serializeJson(doc, buffer);

  Serial.println("Publishing discovery config:");
  Serial.println(buffer);

  if (client.publish(mqtt_topic_config, buffer, true)) {
    Serial.println("Discovery published!");
  } else {
    Serial.println("Discovery publish failed!");
  }
}

// State persistence functions
void loadDeviceState() {
  currentState.lightOn = preferences.getBool("lightOn", false);
  currentState.brightness = preferences.getInt("brightness", 153);
  Serial.printf("Loaded state: light=%s, brightness=%d\n", 
                currentState.lightOn ? "ON" : "OFF", currentState.brightness);
  updateBuiltinLed();
}

void saveDeviceState() {
  preferences.putBool("lightOn", currentState.lightOn);
  preferences.putInt("brightness", currentState.brightness);
}

void updateBuiltinLed() {
  digitalWrite(LED_BUILTIN, currentState.lightOn ? HIGH : LOW);
}

void sendRfCommand(RFM69Dreo::Command cmd) {
  dreo.sendCommand(cmd);
  Serial.printf("RF command sent: %d\n", cmd);
}

// Connection management
bool connectMqtt() {
  if (client.connected()) return true;
  
  unsigned long now = millis();
  if (now - lastMqttAttempt < RECONNECT_INTERVAL) {
    return false;
  }
  lastMqttAttempt = now;

  Serial.print("Connecting to MQTT...");
  
  if (client.connect(device_id, mqtt_user, mqtt_password, 
                     mqtt_topic_availability, 0, true, "offline")) {
    Serial.println(" connected");
    
    // Publish online status
    client.publish(mqtt_topic_availability, "online", true);
    
    // Setup device discovery
    publishDiscoveryConfig();
    
    // Subscribe to commands
    client.subscribe(mqtt_topic_command);
    
    // Publish current state (sync with HA after reconnect)
    publishState();
    
    return true;
  } else {
    Serial.printf(" failed, rc=%d\n", client.state());
    return false;
  }
}

bool checkWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  
  unsigned long now = millis();
  if (now - lastWifiCheck < WIFI_CHECK_INTERVAL) {
    return false;
  }
  lastWifiCheck = now;

  Serial.println("WiFi disconnected, reconnecting...");
  WiFi.reconnect();
  return false;
}

void loop() {
  // Check WiFi connection
  if (!checkWifi()) {
    delay(1000);
    return;
  }
  
  // Maintain MQTT connection
  if (!connectMqtt()) {
    delay(100);
    return;
  }
  
  // Process MQTT messages
  client.loop();
}