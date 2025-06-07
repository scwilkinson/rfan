
#ifndef RFM69DREO_H
#define RFM69DREO_H

#include <Arduino.h>
#include <SPI.h>

class RFM69Dreo {
public:
    // Pin configuration structure
    struct PinConfig {
        uint8_t cs;
        uint8_t rst;
        uint8_t dio2;
        uint8_t irq;
    };

    // Available commands
    enum Command {
        LIGHT_ON_OFF,
        FAN_ON_OFF,
        LIGHT_DOWN,
        LIGHT_UP,
        FAN_1,
        FAN_2,
        FAN_3,
        FAN_4,
        FAN_5,
        FAN_6
    };

    // Constructor
    RFM69Dreo(const PinConfig& pins);

    // Initialize the radio
    bool begin();

    // Send a command
    void sendCommand(Command cmd);

    // Get current radio state
    bool isReady() const { return _initialized; }

private:
    PinConfig _pins;
    bool _initialized;

    // Radio configuration constants
    static const uint32_t FREQ = 433920000; // Hz
    static const uint16_t BITRATE = 3333;
    static const uint32_t FXOSC = 32000000;

    // Protocol constants
    static const char* PREAMBLE;
    static const char* SYNC_PHRASE;

    // Command data
    const char* getPayloadForCommand(Command cmd);

    // Low-level SPI functions
    uint8_t spiRead(uint8_t addr);
    uint8_t spiWrite(uint8_t addr, uint8_t value);

    // Radio control
    void setMode(uint8_t mode);
    bool waitForModeReady(uint16_t timeoutMs = 10);
    void configureRadio();

    // Protocol implementation
    void sendPayload(const char* payloadBitString);
    void transmitBitString(const char* bitString);
};

#endif