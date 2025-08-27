# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an Arduino project that controls a Dreo ceiling fan via RF signals using an ESP32-C6 and RFM69 radio module. The project implements an MQTT bridge for Home Assistant integration, allowing smart home control of both fan speed and light brightness.

## Hardware Architecture

- **ESP32-C6**: Main microcontroller running the MQTT client and WiFi
- **RFM69HCW**: 433MHz radio module for transmitting RF commands to the Dreo fan
- **Pin Configuration**: Defined in `rfan.ino` lines 28-34
  - CS: D5, RST: D4, DIO2: D2, IRQ: D1

## Code Structure

### Core Files
- **rfan.ino**: Main Arduino sketch with MQTT client, WiFi setup, and command handling
- **RFM69Dreo.h/cpp**: Custom library encapsulating the RF protocol and radio control
- **commands/**: Directory containing RF signal captures and protocol documentation

### Key Components

#### RFM69Dreo Library
- Implements the proprietary Dreo RF protocol (433.92MHz, 3333 bps bitrate)
- Handles 10 different commands: light on/off, brightness up/down, fan on/off, fan speeds 1-6
- Protocol uses specific preamble, sync phrase, and command payloads (see `commands.txt`)

#### MQTT Integration
- Home Assistant auto-discovery for light entity with brightness control
- JSON command schema supporting state and brightness control
- Topics follow Home Assistant convention: `homeassistant/light/{device_id}/`

#### State Management  
- Tracks light state (on/off) and brightness (0-255 scale, 5 discrete levels)
- Brightness levels: [51, 102, 153, 204, 255] corresponding to fan's 5 brightness steps

## Development Commands

This is an Arduino project - use Arduino IDE or PlatformIO for development:

- **Arduino IDE**: Open `rfan.ino` directly
- **Upload**: Use Arduino IDE's upload function or `arduino-cli compile` and `arduino-cli upload`
- **Serial Monitor**: 115200 baud for debugging output

## Protocol Details

The RF commands are stored as bit strings in `commands/commands.txt`. Each command consists of:
1. Preamble: `10001000100010001000100010001000`
2. Sync phrase: `11101110111011101000100011101110100011101000100010001000111011101000100010001000`  
3. Command payload: Varies by command (49 bits each)

The `RFM69Dreo` class abstracts this protocol - use the `Command` enum rather than raw bit strings.

## Configuration

WiFi and MQTT credentials are hardcoded in `rfan.ino` lines 7-14. For production deployment, consider using WiFiManager or similar for dynamic configuration.