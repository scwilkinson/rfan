#include "RFM69Dreo.h"

// Protocol constants
const char* RFM69Dreo::PREAMBLE = "10001000100010001000100010001000";
const char* RFM69Dreo::SYNC_PHRASE = "11101110111011101000100011101110100011101000100010001000111011101000100010001000";

// Command payloads
const char* RFM69Dreo::getPayloadForCommand(Command cmd) {
    switch(cmd) {
        case LIGHT_ON_OFF: return "1110100010001110111011101000100011101000111010001";
        case FAN_ON_OFF:   return "1000111011101110111011101000100011101000100010001";
        case LIGHT_DOWN:   return "1110100010001000111011101000100011101000100011101";
        case LIGHT_UP:     return "1110100011101000111011101000100011101000111011101";
        case FAN_1:        return "1000100010001110111011101000100010001000111010001";
        case FAN_2:        return "1000100011101000111011101000100010001000111011101";
        case FAN_3:        return "1000100011101110111011101000100010001110100010001";
        case FAN_4:        return "1000111010001000111011101000100010001110100011101";
        case FAN_5:        return "1000111010001110111011101000100010001110111010001";
        case FAN_6:        return "1000111011101000111011101000100010001110111011101";
        default:           return nullptr;
    }
}

RFM69Dreo::RFM69Dreo(const PinConfig& pins) : _pins(pins), _initialized(false) {
    pinMode(_pins.cs, OUTPUT);
    pinMode(_pins.rst, OUTPUT);
    pinMode(_pins.dio2, OUTPUT);
    digitalWrite(_pins.cs, HIGH);
}

bool RFM69Dreo::begin() {
    // Initialize SPI
    SPI.begin();
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

    // Reset the module
    digitalWrite(_pins.rst, HIGH);
    delayMicroseconds(100);
    digitalWrite(_pins.rst, LOW);
    delay(6);

    // Check if module responds
    if (spiRead(0x10) != 0x24) {
        return false;
    }

    // Configure the radio
    configureRadio();

    _initialized = true;
    return true;
}

void RFM69Dreo::sendCommand(Command cmd) {
    if (!_initialized) return;

    const char* payload = getPayloadForCommand(cmd);
    if (payload == nullptr) return;

    setMode(0x0C);
    if (!waitForModeReady()) {
        Serial.println("RFM69 not responding!");
        return;
    }

    sendPayload(payload);

    setMode(0x00);
    waitForModeReady();  // Less critical if this fails
}

void RFM69Dreo::sendPayload(const char* payloadBitString) {
    // Build complete transmission string
    size_t fullLen = strlen(PREAMBLE) + strlen(SYNC_PHRASE) + strlen(payloadBitString) + 1;
    char fullBitString[fullLen];

    strcpy(fullBitString, PREAMBLE);
    strcat(fullBitString, SYNC_PHRASE);
    strcat(fullBitString, payloadBitString);

    // Transmit 5 times with inter-packet delay
    for (int i = 0; i < 5; i++) {
        transmitBitString(fullBitString);
        digitalWrite(_pins.dio2, LOW);
        delayMicroseconds(8800);
    }
}

void RFM69Dreo::transmitBitString(const char* bitString) {
    while (*bitString) {
        digitalWrite(_pins.dio2, (*bitString == '1') ? HIGH : LOW);
        delayMicroseconds(300);
        bitString++;
    }
}

void RFM69Dreo::configureRadio() {
    // Set to standby mode
    setMode(0x04);
    waitForModeReady();

    // Configure OOK modulation
    spiWrite(0x02, 0x60 | (1 << 3)); // Continuous mode, OOK, no shaping

    // Set frequency
    uint32_t freqrf = (FREQ * 524288ULL) / FXOSC;
    spiWrite(0x07, (freqrf >> 16) & 0xFF);
    spiWrite(0x08, (freqrf >> 8) & 0xFF);
    spiWrite(0x09, freqrf & 0xFF);

    // Set bitrate
    uint16_t bitrate = FXOSC / BITRATE;
    spiWrite(0x03, (bitrate >> 8) & 0xFF);
    spiWrite(0x04, bitrate & 0xFF);

    // Power configuration
    spiWrite(0x5A, 0x5D); // PA1 on
    spiWrite(0x5C, 0x70); // PA2 off
    spiWrite(0x13, 0x1A); // OCP enabled
    spiWrite(0x11, 0x60); // PA1 only, ~8dBm

    // Disable ClkOut
    spiWrite(0x26, 0x07);

    // Return to sleep mode
    setMode(0x00);
    waitForModeReady();
}

uint8_t RFM69Dreo::spiRead(uint8_t addr) {
    uint8_t result;

    digitalWrite(_pins.cs, LOW);
    SPI.transfer(addr & 0x7F); // Ensure read bit (MSB = 0)
    result = SPI.transfer(0x00);
    digitalWrite(_pins.cs, HIGH);

    return result;
}

uint8_t RFM69Dreo::spiWrite(uint8_t addr, uint8_t value) {
    uint8_t result;

    digitalWrite(_pins.cs, LOW);
    SPI.transfer(addr | 0x80); // Set write bit (MSB = 1)
    result = SPI.transfer(value);
    digitalWrite(_pins.cs, HIGH);

    return result;
}

void RFM69Dreo::setMode(uint8_t mode) {
    spiWrite(0x01, mode);
}

bool RFM69Dreo::waitForModeReady(uint16_t timeoutMs) {
    uint32_t start = millis();

    while (!(spiRead(0x27) & 0x80)) {
        if (millis() - start > timeoutMs) {
            _initialized = false;  // Mark as failed
            return false;
        }
    }
    return true;
}
