#include "RFM69Dreo/RFM69Dreo.h"

// Configure pins for ESP32-C6
RFM69Dreo::PinConfig rfmPins = {
    .cs = 7,      // SPI Chip Select
    .rst = 6,     // Reset
    .dio2 = 0,    // Data
    .irq = 5      // PayloadReady/CrcOk (not used in TX mode)
};

// Create the Dreo controller instance
RFM69Dreo dreo(rfmPins);

void setup() {
    // For flashing the LED on emitting
    pinMode(LED_BUILTIN, OUTPUT);

    // Serial console: e.g. cu -s 115200 -l /dev/ttyACM0
    Serial.begin(115200);
    delay(5000);

    // Initialize the RFM69 radio
    Serial.println(F("Initializing RFM69..."));
    if (dreo.begin()) {
        Serial.println(F("RFM69HCW initialized successfully"));
    } else {
        Serial.println(F("RFM69HCW initialization failed!"));
        while(1); // Halt if initialization fails
    }
}

void loop() {
    delay(10000);
    Serial.println(F("Sending Signal"));

    // Turn on LED to indicate transmission
    digitalWrite(LED_BUILTIN, HIGH);

    // Send the LIGHT_ON_OFF command
    dreo.sendCommand(RFM69Dreo::LIGHT_ON_OFF);

    // Turn off LED after transmission
    digitalWrite(LED_BUILTIN, LOW);
}