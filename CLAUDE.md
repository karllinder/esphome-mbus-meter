# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Project Overview

ESPHome custom component (`mbus_meter`) for reading Norwegian HAN smart electricity meters (AIDON_V0001 and compatible) via M-Bus protocol on ESP32. Publishes sensor data to Home Assistant.

## Commands

```bash
esphome config "utility meter.yaml"     # Validate
esphome compile "utility meter.yaml"    # Compile
esphome run "utility meter.yaml"        # Compile + upload
esphome logs "utility meter.yaml"       # Live logs
```

Validate or compile the CI test configs locally:

```bash
esphome config tests/test.esp32-ard.yaml
esphome compile tests/test.esp32-ard.yaml
esphome config tests/test.esp8266-ard.yaml
esphome compile tests/test.esp8266-ard.yaml
```

Lint (matches what CI runs):

```bash
ruff check .
clang-format --dry-run -Werror components/mbus_meter/*.cpp components/mbus_meter/*.h
```

## Branches

- **`main`** — stable, default for users. Only small, low-risk changes land here.
- **`beta`** — upstream-style refactors and larger cleanups soak here before merging back to `main`. Users can opt in with `ref: beta` in `external_components`.

Target refactor PRs at `beta`. Target bug fixes / hardware-validated changes at `main`.

## Architecture

### Data Flow
```
Meter -> UART (2400 baud) -> Frame Detector -> 2A/A1 Parser -> OBIS Decoder -> Home Assistant
```

### Frame Types
- **2A frames** (~16-20 bytes): Real-time power only, sent every few seconds
- **A1 frames** (~150+ bytes): Full data (power, voltage, current, energy), sent every ~10s

### Parsing Pipeline
1. `loop()` -> `read_message()`: Accumulates UART bytes, triggers on size threshold or 2s timeout
2. `process_current_frame()`: Routes to 2A handler (`search_for_real_time_power()`) or A1 handler (`parse_a1_frame()`)
3. Frame parsers extract values and publish to ESPHome sensors

### Repository Structure
```
components/mbus_meter/
  __init__.py           # Component registration (UART device)
  sensor.py             # Numeric sensor schema + codegen
  text_sensor.py        # Text sensor schema + codegen
  mbus_meter.h          # Class declaration, sensor pointers, constants
  mbus_meter.cpp        # Frame parsing, OBIS decoding, sensor publishing
tests/
  test.esp32-ard.yaml   # Compile-test config for ESP32 Arduino
  test.esp8266-ard.yaml # Compile-test config for ESP8266 Arduino
.github/workflows/
  ci.yaml               # Compile matrix on PR + push to main/beta
example.yaml            # Full example config for users
README.md               # User docs with install instructions
LICENSE                 # MIT
```

### Tooling
- `.clang-format` — copied verbatim from upstream `esphome/esphome` dev branch. CI runs `clang-format --dry-run -Werror` on `components/mbus_meter/*.cpp|*.h`.
- `pyproject.toml` — `[tool.ruff]` selects `E/F/I/UP`, ignores `E501`, targets `py311`. CI runs `ruff check .`.

### Local-only files (gitignored)
- `utility meter.yaml` - Personal ESPHome config
- `common/` - Local ESPHome test packages
- `mbus.h` - Legacy standalone component

### OBIS Codes (Norwegian HAN / AIDON_V0001)

| OBIS Code | Measurement | Sensor Key |
|-----------|-------------|------------|
| 1.0.1.7.0.255 | Active power+ (W) | `power` |
| 1.0.2.7.0.255 | Active power- (W) | *(logged)* |
| 1.0.3.7.0.255 | Reactive power+ (VAr) | `reactive_power` |
| 1.0.4.7.0.255 | Reactive power- (VAr) | *(logged)* |
| 1.0.31/51/71.7.0.255 | Current L1/L2/L3 (A) | `current_l1/l2/l3` |
| 1.0.32/52/72.7.0.255 | Voltage L1/L2/L3 (V) | `voltage_l1/l2/l3` |
| 1.0.1.8.0.255 | Energy import (Wh) | `energy` |
| 1.0.3.8.0.255 | Reactive energy import (VArh) | `reactive_energy` |
| 1.0.4.8.0.255 | Reactive energy export (VArh) | `reactive_export_energy` |

### Critical Config
- UART: 2400 baud, `rx_buffer_size: 2048`, `baud_rate: 0` on logger
- Internal buffer: 4096 bytes, frame timeout: 2000ms
- Current: 0.1A resolution (long-signed /10), Voltage: 0.1V resolution (long-unsigned /10)
- Energy: 10 Wh resolution (double-long-unsigned * 10)

### Known Meter Quirks
- 2A frames sometimes truncate power to 1 byte instead of 2
- `0x29` single-byte = known bug for ~10000W
- Range `0x20-0x2F` single-byte = likely truncated high values (skipped)

### Open hardware-validation issues
These document behaviours that looked suspicious in review but can only be confirmed against a live meter. Don't change the code blindly.
- **#5** — `parse_current_value` reads `int16_t` then applies `fabs()`, silently dropping sign. Needs meter trace (especially under solar export) to decide whether the spec is signed or the reads are actually unsigned.
- **#6** — `CLAUDE.md` (this file) describes a `0x20-0x2F` single-byte skip that isn't implemented in current code. Either the quirks note is stale or the filter was intentionally removed. Needs trace from a meter emitting truncated 2A frames.
