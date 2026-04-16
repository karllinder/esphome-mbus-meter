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

Lint (only configured on `beta` at present — see Tooling):

```bash
ruff check .                                                                     # needs pyproject.toml (beta only)
clang-format --dry-run -Werror components/mbus_meter/*.cpp components/mbus_meter/*.h  # needs .clang-format (beta only)
```

## Branches

- **`main`** — stable, default for users. Only small, low-risk changes land here.
- **`beta`** — upstream-style refactors and larger cleanups soak here before merging back to `main`. Users can opt in with `ref: beta` in `external_components`.

Target refactor PRs at `beta`. Target bug fixes / hardware-validated changes at `main`.

## Working in this repo

### PR workflow
- Branch naming: `chore/...`, `ci/...`, `refactor/...`, `docs/...`, `feature/...`, `fix/...`.
- Merge method: **merge commits** (not squash). Keep the repo history consistent: `gh pr merge <n> --merge`.
- Stacking: if PR B depends on PR A (e.g. B's lint job only passes after A's formatting change lands), branch B off A and set B's base to A. Before merging out of order, retarget manually: `gh pr edit <n> --base <target>`. After merging, delete the merged branch on remote (`git push origin --delete <name>`) and locally (`git branch -d <name>`).
- Don't force-push. Don't `--no-verify`. Don't amend merged commits.
- Commits: short imperative title, a paragraph of *why*, then mechanical what-changed bullets. Trailer: `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>`.

### Local environment caveat (Windows)
`esphome compile` on Windows with Python 3.13 + PlatformIO fails partway through with `FileNotFoundError: [WinError 2]` inside `idf_tools.py`, **even for Arduino framework** — modern Arduino-ESP32 pulls ESP-IDF in as a transitive build dependency. This is a PlatformIO / Python-3.13 subprocess bug, not an mbus_meter bug. Rely on:

1. `esphome config tests/test.<platform>-<framework>.yaml` — works everywhere.
2. CI (Linux runner, Python 3.12) for actual compile validation.

Don't sink time diagnosing the local compile failure. If you need to compile locally, use a Python 3.12 venv.

### CI
Workflow: `.github/workflows/ci.yaml`. Triggers: `push: [main, beta]`, `pull_request`, `workflow_dispatch`.

| Job | Where it runs | What it runs | Typical runtime |
|---|---|---|---|
| `lint` (beta only) | Ubuntu, Python 3.12 | `ruff check .` + `clang-format --dry-run -Werror` on `components/mbus_meter/*.cpp\|*.h` | ~30s |
| `compile` | Ubuntu, Python 3.12 | Matrix over `tests/test.*.yaml`: `esphome config` then `esphome compile` | esp8266 ~1m30s, esp32 ~3m30s |

Adding a new target: drop a new `tests/test.<platform>-<framework>.yaml` and add its filename to the `matrix.test` list in `ci.yaml`. The `lint` job lives on `beta` only until beta merges to main.

### Releases
- Tag convention: `vMAJOR.MINOR.PATCH` (e.g. `v1.1.0`).
- Target of release: `main` only — never tag `beta`.
- Use `gh release create vX.Y.Z --target main --title "..." --notes "..."`.
- A release note should enumerate: what's new, what's deferred (still on beta), how to pin (`ref: vX.Y.Z`), and any open hardware-validation issues affecting behaviour.

### Upstream references
When designing changes, model patterns on `esphome/esphome/components/dsmr/` in the upstream ESPHome repo — it's a UART-based utility-meter component with sensor + text_sensor platforms and matches our structure closely. For the test-file naming convention (`test.<platform>-<framework>.yaml`), upstream examples live at `esphome/esphome/tests/components/<name>/`.

The `.clang-format` on `beta` is copied verbatim from `https://raw.githubusercontent.com/esphome/esphome/dev/.clang-format`. Refresh from upstream if the format drifts.

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
**As of v1.1.0** the following live on `beta` only and will land on `main` at the next beta → main merge:
- `.clang-format` — copied verbatim from upstream `esphome/esphome` dev branch. CI's `lint` job runs `clang-format --dry-run -Werror` on `components/mbus_meter/*.cpp|*.h`.
- `pyproject.toml` — `[tool.ruff]` selects `E/F/I/UP`, ignores `E501`, targets `py311`. CI's `lint` job runs `ruff check .`.

If you're on `main` and these files don't exist yet, don't try to run the lint commands — they'll use defaults that don't match the project's style.

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

### Pending style cleanup (targets `beta`)
Flagged during the v1.1.0 audit but deliberately deferred — small, behaviour-neutral Python polish. Good candidates for a focused follow-up PR onto `beta`.
- `components/mbus_meter/sensor.py` — the `sensor_schema(...)` kwargs on `CONF_POWER` (and downstream keys) are under-indented (12sp instead of 16sp; siblings at 8sp instead of 12sp). Parses fine, ugly. Ruff doesn't catch nested-dict indent.
- `reactive_power` / `reactive_energy` / `reactive_export_energy` defaults in `sensor.py` use `device_class=DEVICE_CLASS_POWER` and the literal strings `"var"` / `"varh"`. Upstream `esphome.const` provides `DEVICE_CLASS_REACTIVE_POWER`, `UNIT_VOLT_AMPS_REACTIVE`, `UNIT_VOLT_AMPS_REACTIVE_HOURS`. Changing the defaults is HA-visible if a user relied on the default `device_class`; example.yaml already overrides this, so impact is small.
- `cv.GenerateID(): cv.use_id(MbusMeter)` in sensor.py / text_sensor.py could use a named `CONF_MBUS_METER_ID` custom key per the DSMR upstream pattern. Cosmetic only.
