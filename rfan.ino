#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
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

// Light state
bool ledState = false;
int currentBrightness = 153;  // Track brightness (0-255, default 60% for 5 steps)
const int BRIGHTNESS_STEPS = 5;  // Dreo light has 5 brightness levels
const int BRIGHTNESS_STEP_SIZE = 255 / BRIGHTNESS_STEPS;  // 51 per step
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

  // Connect to WiFi
  setup_wifi();

  // Configure MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
  client.setBufferSize(1024);  // Increase buffer for JSON messages
  reconnect();

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
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");

  String messageTemp;
  for (int i = 0; i < length; i++) {
    messageTemp += (char)message[i];
  }
  Serial.println(messageTemp);

  // Parse commands from Home Assistant
  if (String(topic) == mqtt_topic_command) {
    // Handle JSON commands from Home Assistant
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, messageTemp);

    if (!error) {
      // JSON command
      if (doc.containsKey("state")) {
        String state = doc["state"];
        if (state == "ON") {
          if (!ledState) {
            ledState = true;
            dreo.sendCommand(RFM69Dreo::LIGHT_ON_OFF);
            digitalWrite(LED_BUILTIN, HIGH);
          }
        } else if (state == "OFF") {
          if (ledState) {
            ledState = false;
            dreo.sendCommand(RFM69Dreo::LIGHT_ON_OFF);
            digitalWrite(LED_BUILTIN, LOW);
          }
        }
      }

      if (doc.containsKey("brightness")) {
        int targetBrightness = doc["brightness"];
        adjustBrightness(targetBrightness);
      }
    } else {
      // Simple string commands
      if (messageTemp == "ON") {
        if (!ledState) {
          ledState = true;
          dreo.sendCommand(RFM69Dreo::LIGHT_ON_OFF);
          digitalWrite(LED_BUILTIN, HIGH);
        }
      }
      else if (messageTemp == "OFF") {
        if (ledState) {
          ledState = false;
          dreo.sendCommand(RFM69Dreo::LIGHT_ON_OFF);
          digitalWrite(LED_BUILTIN, LOW);
        }
      }
      else if (messageTemp == "BRIGHTNESS_UP") {
        if (ledState) {
          dreo.sendCommand(RFM69Dreo::LIGHT_UP);
          currentBrightness = min(255, currentBrightness + BRIGHTNESS_STEP_SIZE);
        }
      }
      else if (messageTemp == "BRIGHTNESS_DOWN") {
        if (ledState) {
          dreo.sendCommand(RFM69Dreo::LIGHT_DOWN);
          currentBrightness = max(BRIGHTNESS_STEP_SIZE, currentBrightness - BRIGHTNESS_STEP_SIZE);
        }
      }
    }

    publishState();
  }
}

void adjustBrightness(int targetBrightness) {
  if (!ledState) return;

  targetBrightness = constrain(targetBrightness, 51, 255);

  // Find closest brightness level
  int targetStep = 0;
  int minDiff = 255;
  for (int i = 0; i < BRIGHTNESS_STEPS; i++) {
    int diff = abs(BRIGHTNESS_LEVELS[i] - targetBrightness);
    if (diff < minDiff) {
      minDiff = diff;
      targetStep = i;
    }
  }

  // Find current step
  int currentStep = 0;
  for (int i = 0; i < BRIGHTNESS_STEPS; i++) {
    if (abs(BRIGHTNESS_LEVELS[i] - currentBrightness) < 5) {
      currentStep = i;
      break;
    }
  }

  int stepsToMove = targetStep - currentStep;

  Serial.print("Adjusting brightness from step ");
  Serial.print(currentStep);
  Serial.print(" (");
  Serial.print(currentBrightness);
  Serial.print(") to step ");
  Serial.print(targetStep);
  Serial.print(" (");
  Serial.print(BRIGHTNESS_LEVELS[targetStep]);
  Serial.println(")");

  // Send UP or DOWN commands
  if (stepsToMove > 0) {
    for (int i = 0; i < stepsToMove; i++) {
      dreo.sendCommand(RFM69Dreo::LIGHT_UP);
      delay(150);  // Slightly longer delay for reliability
    }
  } else if (stepsToMove < 0) {
    for (int i = 0; i < abs(stepsToMove); i++) {
      dreo.sendCommand(RFM69Dreo::LIGHT_DOWN);
      delay(150);
    }
  }

  currentBrightness = BRIGHTNESS_LEVELS[targetStep];
}

void publishState() {
  // Publish state as JSON for Home Assistant
  StaticJsonDocument<256> doc;
  doc["state"] = ledState ? "ON" : "OFF";
  doc["brightness"] = currentBrightness;

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(mqtt_topic_state, buffer, true);

  Serial.print("Published state: ");
  Serial.println(buffer);
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

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    if (client.connect(device_id, mqtt_user, mqtt_password, 
                      mqtt_topic_availability, 0, true, "offline")) {
      Serial.println("connected");

      client.publish(mqtt_topic_availability, "online", true);
      publishDiscoveryConfig();
      client.subscribe(mqtt_topic_command);
      publishState();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}