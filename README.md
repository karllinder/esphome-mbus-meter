# ESPHome M-Bus Meter Component

A custom [ESPHome](https://esphome.io/) component for reading Norwegian HAN (Home Area Network) smart electricity meters via M-Bus protocol on ESP32.

Supports **AIDON_V0001** and compatible meters used by Norwegian electricity grid companies.

## Features

- Real-time power consumption (2A frames, updated every few seconds)
- 3-phase voltage and current measurements (A1 frames, every ~10 seconds)
- Active and reactive energy counters (cumulative import/export)
- Meter identification (meter ID, type, OBIS version)
- Seamless integration with Home Assistant via ESPHome

## Hardware Requirements

- ESP32 development board
- HAN port interface circuit (level shifter for M-Bus to UART)
- RJ45 cable to connect to the meter's HAN port

## Installation

Add this repository as an external component in your ESPHome configuration:

```yaml
external_components:
  - source: github://karllinder/esphome-mbus-meter
    components: [ mbus_meter ]
```

## Configuration

### Minimal example

```yaml
uart:
  id: uart_bus
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 2400
  rx_buffer_size: 2048

mbus_meter:
  id: mbus_reader
  uart_id: uart_bus

sensor:
  - platform: mbus_meter
    id: mbus_reader

    power:
      name: "Power Consumption"

    current_l1:
      name: "Current L1"
    current_l2:
      name: "Current L2"
    current_l3:
      name: "Current L3"

    voltage_l1:
      name: "Voltage L1"
    voltage_l2:
      name: "Voltage L2"
    voltage_l3:
      name: "Voltage L3"

    energy:
      name: "Energy Import"

    reactive_power:
      name: "Reactive Power"
    reactive_energy:
      name: "Reactive Energy Import"
    reactive_export_energy:
      name: "Reactive Energy Export"
```

### Text sensors (meter identification)

```yaml
text_sensor:
  - platform: mbus_meter
    id: mbus_reader

    obis_version:
      name: "OBIS Version"
    meter_id:
      name: "Meter ID"
    meter_type:
      name: "Meter Type"
```

### Separate 2A frame power sensor

The meter sends two types of frames: fast 2A frames (power only) and slower A1 frames (all data). By default, both update the same power sensor. To track them separately:

```yaml
sensor:
  - platform: mbus_meter
    id: mbus_reader

    2a_frame_own_sensor: true

    power:
      name: "Power (A1 frame)"

    power_2a_frame:
      name: "Power (2A frame)"
```

See [example.yaml](example.yaml) for a full configuration example.

## Supported OBIS Codes

| OBIS Code | Measurement | Unit | Sensor Key |
|-----------|-------------|------|------------|
| 1.0.1.7.0.255 | Active power+ (import) | W | `power` |
| 1.0.2.7.0.255 | Active power- (export) | W | *(logged only)* |
| 1.0.3.7.0.255 | Reactive power+ (import) | VAr | `reactive_power` |
| 1.0.4.7.0.255 | Reactive power- (export) | VAr | *(logged only)* |
| 1.0.31.7.0.255 | Current L1 | A | `current_l1` |
| 1.0.51.7.0.255 | Current L2 | A | `current_l2` |
| 1.0.71.7.0.255 | Current L3 | A | `current_l3` |
| 1.0.32.7.0.255 | Voltage L1 | V | `voltage_l1` |
| 1.0.52.7.0.255 | Voltage L2 | V | `voltage_l2` |
| 1.0.72.7.0.255 | Voltage L3 | V | `voltage_l3` |
| 1.0.1.8.0.255 | Active energy import | Wh | `energy` |
| 1.0.2.8.0.255 | Active energy export | Wh | *(logged only)* |
| 1.0.3.8.0.255 | Reactive energy import | VArh | `reactive_energy` |
| 1.0.4.8.0.255 | Reactive energy export | VArh | `reactive_export_energy` |
| 1.1.0.2.129.255 | OBIS list version | - | `obis_version` |
| 0.0.96.1.0.255 | Meter ID | - | `meter_id` |
| 0.0.96.1.7.255 | Meter type | - | `meter_type` |

## Frame Types

The Norwegian HAN interface sends two types of frames:

- **2A frames** (~16-20 bytes): Real-time active power, sent every few seconds
- **A1 frames** (~150+ bytes): Comprehensive data including power, current, voltage, and energy counters, sent every ~10 seconds

## Known Meter Quirks

- Some meters occasionally send truncated 2A frames (2 bytes instead of 4 for power values)
- Value `0x29` in single-byte power mode is a known meter bug representing ~10000W
- Current measurement scaling can vary between meters (0.1A vs 0.01A resolution)

## Tested Meters

- Aidon 6525 (AIDON_V0001)

If you have tested this component with a different meter, please open an issue to let us know.

## Troubleshooting

1. **No data**: Verify UART wiring (RX/TX pins) and baud rate (must be 2400)
2. **Incomplete frames**: Ensure `rx_buffer_size` is at least 2048
3. **Enable debug logging**: Set `log_level: VERY_VERBOSE` to see raw frame data
4. **Logger baud_rate**: Must be `0` if UART pins are used for meter communication

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) for details.
