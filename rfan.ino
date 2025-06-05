#include <SPI.h>

#define RFM69HCW_G0_IRQ   5   // E - PayloadReady/CrcOk
#define RFM69HCW_CS       7   // D - SPI Chip Select
#define RFM69HCW_RST      6   // C - Reset
#define RFM69HCW_G2_DIO2  0  // A - Data
#define RFM69HCW_G1_DIO1  8  // 13 - DCLK if Gaussian FSK needed

#define MODULATION    OOK
#define FREQ          433.92
#define BITRATE       3333
#define BANDWIDTH     DCCFREQ_4 << 5 | RXBWMANT_16 << 3 | RXBWEXP_0
#define AFC_BANDWIDTH DCCFREQ_4 << 5 | RXBWMANT_16 << 3 | RXBWEXP_0
#define BIT_SYNC      0x60 // Bit Sync off, as pulses are asymmetrical

#define FXOSC 32000000.0 // RFM69HCW clock speed - do not change

#define PREAMBLE "10001000100010001000100010001000"
#define SYNC_PHRASE "11101110111011101000100011101110100011101000100010001000111011101000100010001000"

enum { FSK, OOK };

typedef struct {
  const char *commandName;
  const char *payload;
} CommandMapEntry;

const CommandMapEntry commandMap[] = {
  {"LIGHT_ON_OFF",  "1110100010001110111011101000100011101000111010001"},
  {"FAN_ON_OFF", "1000111011101110111011101000100011101000100010001"},
  {"LIGHT_DOWN", "1110100010001000111011101000100011101000100011101"},
  {"LIGHT_UP", "1110100011101000111011101000100011101000111011101"},
  {"FAN_1", "1000100010001110111011101000100010001000111010001"},
  {"FAN_2", "1000100011101000111011101000100010001000111011101"},
  {"FAN_3", "1000100011101110111011101000100010001110100010001"},
  {"FAN_4", "1000111010001000111011101000100010001110100011101"},
  {"FAN_5", "1000111010001110111011101000100010001110111010001"},
  {"FAN_6", "1000111011101000111011101000100010001110111011101"}
};

const int numCommands = sizeof(commandMap) / sizeof(commandMap[0]);

uint8_t spi_cmd(uint8_t addr, uint8_t value) {
  uint8_t status;

  digitalWrite(RFM69HCW_CS, LOW);
  SPI.transfer(addr);
  status = SPI.transfer(value);
  digitalWrite(RFM69HCW_CS, HIGH);

  return status;
}

// Set the wnr bit to write (Section 5.2.1) 
uint8_t spi_write(uint8_t addr, uint8_t value) {
  return spi_cmd(addr | 0x80, value);
}

// Pass nothing when reading
uint8_t spi_read(uint8_t addr) {
  return spi_cmd(addr, 0x00);
}

const char* getPayloadForCommand(const char* commandName) {
  for (int i = 0; i < numCommands; i++) {
    if (strcmp(commandMap[i].commandName, commandName) == 0) {
      return commandMap[i].payload;
    }
  }
  return NULL; // Return NULL if the command name was not found.
}

void sendPayload(const char *payloadBitString) {
  size_t fullLen = strlen(PREAMBLE) + strlen(SYNC_PHRASE) + strlen(payloadBitString) + 1;
  char fullBitString[fullLen];

  strcpy(fullBitString, PREAMBLE);
  strcat(fullBitString, SYNC_PHRASE);
  strcat(fullBitString, payloadBitString);

  for (int i = 0; i < 5; i++) {
    const char *ptr = fullBitString;
    while (*ptr != '\0') {
      if (*ptr == '1') {
        digitalWrite(RFM69HCW_G2_DIO2, HIGH);
      } else if (*ptr == '0') {
        digitalWrite(RFM69HCW_G2_DIO2, LOW);
      }
      delayMicroseconds(300);
      ptr++;
    }
    digitalWrite(RFM69HCW_G2_DIO2, LOW);
    delayMicroseconds(8800);
  }
}

void sendCommand(const char* commandName) {
  const char* payload = getPayloadForCommand(commandName);
  if (payload != NULL) {
    sendPayload(payload);  // Assuming you build the full bit string with preamble/sync.
  } else {
    Serial.println(F("Unknown Command"));
  }
}

void rfminit() {
  // Read the version register, if it's not 0x24, something is wrong
  if(spi_read(0x10) == 0x24)
    Serial.println(F("RFM69HCW SPI Working"));
  else
    Serial.println(F("RFM69HCW SPI Failed"));

  // RegOpMode: Standby Mode & wait
  spi_write(0x01, 0x04); 
  while(!(spi_read(0x27)) & 0x80);

  // RegDataModul: Continuous w/ bit-sync and no other shaping 
  uint8_t modulation = MODULATION;
  spi_write(0x02, BIT_SYNC | modulation << 3); 

  // RegFrf*: Frequency across 3 bytes
  uint32_t freqrf = (FREQ * 1000000) / (FXOSC / 524288);
  spi_write(0x07, (freqrf >> 16) & 0xFF);
  spi_write(0x08, (freqrf >> 8) & 0xFF);
  spi_write(0x09, freqrf & 0xFF);

  // RegBitrate*: Bitrate needed for Bit Synchronizer
  uint16_t bitrate = FXOSC / BITRATE;
  spi_write(0x03, (bitrate >> 8) & 0xFF);
  spi_write(0x04, bitrate & 0xFF);

  // ClkOut set to Off to save some power
  spi_write(0x26, 0x07);

  /*
   * TX- Settings
   */ 
  // High Power toggles
  // spi_write(0x5A, 0x5D); // Section 3.3.7 PA1 on for +20dBm
  // spi_write(0x5C, 0x7C); // Section 3.3.7 PA2 on for +20dBm

  spi_write(0x5A, 0x5D); // PA1 on
  spi_write(0x5C, 0x70); // PA2 off

  // Power Settings for Tx
  // spi_write(0x13, 0x0F); // Default = 0x1A. Disable OCP = 0x0F
  // spi_write(0x11, 0x7F); // Default = 0x9F. 23dBm = 0x1F, PA1+2 on = 0x60

  spi_write(0x13, 0x1A); // Keep OCP enabled (default)
  spi_write(0x11, 0x60); // PA1 only, minimum power (~8dBm)

  // Sleep
  spi_write(0x01, 0x00);
  while(!(spi_read(0x27)) & 0x80);
}

void setup() {
  // Setup pins other than SPI, VDD and ground
  pinMode(RFM69HCW_CS, OUTPUT);
  pinMode(RFM69HCW_RST, OUTPUT);
  pinMode(RFM69HCW_G2_DIO2, OUTPUT);

  // For flashing the LED on emitting
  pinMode(LED_BUILTIN, OUTPUT);

  // Serial console: e.g. cu -s 115200 -l /dev/ttyACM0
  Serial.begin(115200);
  delay(5000);

  // Section 5.2.1 - SPI Interface
  digitalWrite(RFM69HCW_CS, HIGH);
  SPI.begin();
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  Serial.println(F("SPI connection open"));

  // Section 7.2.2 - Manual Reset
  digitalWrite(RFM69HCW_RST, HIGH);
  delayMicroseconds(100);
  digitalWrite(RFM69HCW_RST, LOW);
  // Spec says 5ms, but takes longer
  delay(6);

  // Configure the receiver
  rfminit();
}

void loop() {
  delay(10000); 
  Serial.println(F("Sending Signal"));

  digitalWrite(LED_BUILTIN, HIGH);

  spi_write(0x01, 0x0C);
  while(!(spi_read(0x27)) & 0x80);

  sendCommand("LIGHT_ON_OFF");

  // Sleep
  spi_write(0x01, 0x00);
  while(!(spi_read(0x27)) & 0x80);

  digitalWrite(LED_BUILTIN, LOW);
}