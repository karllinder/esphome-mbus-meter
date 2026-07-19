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
  - source: github://coldreckon/esphome-mbus-meter
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
    export_energy:
      name: "Energy Export"

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
    meter_time:
      name: "Meter Time"
```

The `meter_time` sensor holds the meter clock (date and hour, meter local standard
time) decoded from the hourly frame, and doubles as a "last hourly frame received"
indicator.

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
| 1.0.2.8.0.255 | Active energy export | Wh | `export_energy` |
| 1.0.3.8.0.255 | Reactive energy import | VArh | `reactive_energy` |
| 1.0.4.8.0.255 | Reactive energy export | VArh | `reactive_export_energy` |
| 1.1.0.2.129.255 | OBIS list version | - | `obis_version` |
| 0.0.96.1.0.255 | Meter ID | - | `meter_id` |
| 0.0.96.1.7.255 | Meter type | - | `meter_type` |
| 0.0.1.0.0.255 | Meter clock (hourly frame) | - | `meter_time` |

## Frame Types

The Norwegian HAN interface sends two types of frames:

- **2A frames** (~16-20 bytes): Real-time active power, sent every few seconds
- **A1 frames** (~150+ bytes): Comprehensive data including power, current, voltage, and energy counters, sent every ~10 seconds
- **Hourly A1 frames** (~160 bytes, AIDON "List 3"): sent once per hour on the whole
  hour — the regular A1 contents plus a clock record (0.0.1.0.0.255, a DLMS date-time
  starting `07:Ex` for the year, in meter local standard time) and the four cumulative
  energy counters (1.0.1.8.0 / 1.0.2.8.0 / 1.0.3.8.0 / 1.0.4.8.0, double-long-unsigned
  at 10 Wh/VArh resolution). Energy values arrive with bytes dropped like everything
  else; since a lossy big-endian read can only *under*estimate, the parser rejects
  counter values below the last published one, skips 1-byte reads, and requires a
  >= 3-byte read to seed the baseline after boot.

## Known Meter Quirks

- Some meters occasionally send truncated 2A frames (2 bytes instead of 4 for power values)
- Single-byte power values are the low byte of the true value with the high byte(s)
  dropped (`0x5D` = 10:5D = 4189 W). The parser recovers the high byte from a reference:
  the last published power when the total phase current is unchanged, otherwise an
  estimate from the phase currents (400/230V TN grid: P ~= 230 V * sum(I), corrected for
  reactive power). This also explains the historically observed "0x29 = ~10000 W bug"
  (0x29XX = 10496-10751 W).
- Current measurement scaling can vary between meters (0.1A vs 0.01A resolution)
- **Omitted OBIS type byte**: A1 records after the first of a group are sent "compressed"
  without the OBIS type byte — currents L2/L3 arrive as `02:01:07:10:<value>` instead of
  `02:01:33/47:07:10:<value>`, voltage L3 as `23:02:01:07:<value>`, and reactive power+
  as `02:01:07`. The parser assigns these by their fixed position in the AIDON list order.
- **Variable-length values**: leading zero bytes are stripped, so current values are 0-2
  bytes after the `0x10` long-signed tag. An *empty* value (tag only, or no payload at
  all) is sent even under load and means "no reading in this frame" — the parser keeps
  the previous state instead of publishing 0.
- **Lossy value truncation**: voltage (and sometimes power) values often arrive with
  the high byte dropped (e.g. `0x2C` instead of `09:2C`). Voltages are reconstructed by
  picking the high-byte candidate closest to the last known value for that phase (grid
  voltage moves slowly while candidates are 25.6 V apart, so this is unambiguous).
- **Reactive power**: the meter reports import (+, usually empty = 0) and export (−)
  separately; the `reactive_power` sensor publishes the net value once per A1 frame,
  with export negative.

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
