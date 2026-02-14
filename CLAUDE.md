# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESPHome-based smart utility meter reader for Norwegian HAN (Home Area Network) electricity meters. It implements a custom ESPHome component (`mbus_meter`) that communicates via M-Bus (Meter-Bus) protocol on ESP32, parses HDLC frames to extract OBIS-coded values, and publishes them as Home Assistant sensors. The component supports AIDON_V0001 and compatible Norwegian smart meters.

## Essential Commands

### Build and Deploy
```bash
# Validate configuration
esphome config "utility meter.yaml"

# Compile firmware
esphome compile "utility meter.yaml"

# Upload to ESP32 device
esphome upload "utility meter.yaml"

# View live logs from device
esphome logs "utility meter.yaml"

# Clean build files
esphome clean "utility meter.yaml"
```

### Development Workflow
```bash
# Full cycle: validate, compile, and upload
esphome run "utility meter.yaml"

# Compile and upload via OTA
esphome run "utility meter.yaml" --device OTA_IP_ADDRESS

# Generate C++ code without compiling (for debugging)
esphome compile "utility meter.yaml" --only-generate
```

## Architecture

### Component Structure
The project implements a custom ESPHome component (`mbus_meter`) that:
1. Extends `Component` and `uart::UARTDevice` for UART communication
2. Reads HDLC frames with timeout-based framing (no HDLC 0x7E delimiters used)
3. Supports two frame types: 2A (real-time power) and A1 (comprehensive data)
4. Extracts OBIS-coded values from the data stream
5. Publishes numeric sensor data and text sensor data to Home Assistant

### Data Flow
```
Electricity Meter -> UART (2400 baud) -> Frame Detector -> 2A/A1 Parser -> OBIS Decoder -> Sensor Publishing -> Home Assistant
```

### Frame Types
- **2A frames**: Short frames (~16-20 bytes) containing real-time active power data, sent frequently
- **A1 frames**: Long frames (~150+ bytes) containing comprehensive meter data (power, current, voltage, energy counters), sent every ~10 seconds

### Repository Structure (ESPHome external component format)
- `components/mbus_meter/mbus_meter.h` - Component header with sensor declarations and method prototypes
- `components/mbus_meter/mbus_meter.cpp` - Component implementation with frame parsing and OBIS decoding
- `components/mbus_meter/__init__.py` - ESPHome component registration (platform + UART device)
- `components/mbus_meter/sensor.py` - Sensor platform schema and code generation
- `components/mbus_meter/text_sensor.py` - Text sensor platform schema and code generation
- `example.yaml` - Full example configuration for users
- `README.md` - User-facing documentation with installation and usage instructions
- `LICENSE` - MIT license

### Local-only files (not in repo)
- `utility meter.yaml` - Personal ESPHome config with network/device settings
- `common/` - Shared ESPHome test packages (WiFi, device base, time, etc.)
- `mbus.h` - Legacy standalone component (superseded by mbus_meter)
- `crc_validator.py` / `validate_crc16.py` - CRC-16 validation tools for debugging

### OBIS Code Mapping (Norwegian HAN / AIDON_V0001)
The component recognizes these OBIS codes:

**Power measurements:**
- 1.0.1.7.0.255 - Active power+ import (W)
- 1.0.2.7.0.255 - Active power- export (W)
- 1.0.3.7.0.255 - Reactive power+ import (VAr)
- 1.0.4.7.0.255 - Reactive power- export (VAr)

**Current measurements (3-phase, 0.1A resolution):**
- 1.0.31.7.0.255 - Current L1 (A)
- 1.0.51.7.0.255 - Current L2 (A)
- 1.0.71.7.0.255 - Current L3 (A)

**Voltage measurements (3-phase, 0.1V resolution):**
- 1.0.32.7.0.255 - Voltage L1 (V)
- 1.0.52.7.0.255 - Voltage L2 (V)
- 1.0.72.7.0.255 - Voltage L3 (V)

**Energy counters (cumulative):**
- 1.0.1.8.0.255 - Active energy import (Wh)
- 1.0.2.8.0.255 - Active energy export (Wh)
- 1.0.3.8.0.255 - Reactive energy import (VArh)
- 1.0.4.8.0.255 - Reactive energy export (VArh)

**Text identifiers:**
- 1.1.0.2.129.255 - OBIS list version (e.g., "AIDON_V0001")
- 0.0.96.1.0.255 - Meter ID (16-digit GIAI GS1)
- 0.0.96.1.7.255 - Meter type (e.g., "6515", "6525", "6534", "6540", "6550")

### Critical Configuration
- The UART RX buffer should be at least 2048 bytes (configured in `utility meter.yaml`) to handle complete meter frames
- The component's internal buffer is 4096 bytes to safely accumulate data
- Baud rate is 2400 for Norwegian HAN standard
- Frame timeout is 2000ms - if no new bytes arrive within this window, the buffer is processed
- Logger baud_rate must be set to 0 (disabled on UART) since the UART is used for meter communication

### Known Meter Quirks
- 2A frames sometimes send truncated power values (2 bytes instead of 4)
- Value 0x29 in single-byte power is a known bug representing 10000W
- Values in range 0x20-0x2F in single-byte mode may be truncated high power values
- Current scaling varies between meters (/10 vs /100 resolution)
