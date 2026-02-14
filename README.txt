

# List 1 - Continuous Power Monitoring Data
## Norwegian HAN Specification - Real-time Power Measurements

### Frame Structure Analysis:
```
2A:                    > Frame start marker
08:83:13:04:13:E6:40:  > M-Bus header (8 bytes, control 83, address 13:E6:40)
01:01:02:              > Data structure identifier
01:01:07:01:           > OBIS code 1.0.1.7.0.255 (Active power+ import)
[VALUE]:               > Power measurement value
[ADDITIONAL_DATA]:     > Extended data (when present)
2A:                    > Frame end marker
```

### Decoded Power Measurements:

| Sample | OBIS Code | Raw Value (Hex) | Decimal Value | Power (Watts) | Additional Data |
|--------|-----------|-----------------|---------------|---------------|-----------------|
| 1 | 1.0.1.7.0.255 | 4C | 76 | **76W** | 02:02:16:D0 |
| 2 | 1.0.1.7.0.255 | - | - | **0W** | 02:02:16:58:4F |
| 3 | 1.0.1.7.0.255 | 49 | 73 | **73W** | 02:02:16:20 |
| 4 | 1.0.1.7.0.255 | - | - | **0W** | 02:02:16:58:4F |
| 5 | 1.0.1.7.0.255 | 4F | 79 | **79W** | 02:02:16:E3 |
| 6 | 1.0.1.7.0.255 | 4F | 79 | **79W** | 02:02:16:E3 |

### Sample-by-Sample Analysis:

**Sample 1:** `4C:02:02:16:D0`
- Power: **76 watts** (0.076 kW)
- Status: Normal consumption

**Sample 2:** `02:02:16:58:4F`
- Power: **0 watts** (no primary value, only additional data)
- Possible measurement gap or standby reading

**Sample 3:** `49:02:02:16:20`
- Power: **73 watts** (0.073 kW)
- Status: Slight decrease from sample 1

**Sample 4:** `02:02:16:58:4F`
- Power: **0 watts** (repeat of sample 2 pattern)
- Consistent with measurement gap

**Sample 5:** `4F:02:02:16:E3`
- Power: **79 watts** (0.079 kW)
- Status: Slight increase

**Sample 6:** `4F:02:02:16:E3` (identical to sample 5)
- Power: **79 watts** (0.079 kW)
- Status: Stable reading

### Power Consumption Timeline:
```
Sample:  1     2     3     4     5     6
Power:   76W → 0W → 73W → 0W → 79W → 79W
Time:    ----→----→----→----→----→---->
```

### Analysis Summary:

**Power Range:** 73-79 watts (when actively consuming)
**Average Power:** ~76 watts (excluding 0W readings)
**Pattern:** Alternating between consumption and zero readings suggests either:
- Measurement timing intervals
- Device cycling on/off
- Communication protocol sampling

**Interpretation:**
This appears to be baseline household consumption - likely from:
- Standby electronics (TV, computers, chargers)
- LED lighting
- Refrigerator/freezer compressor cycling
- Router, modem, and other always-on devices

### Technical Notes:
- OBIS Code 1.0.1.7.0.255 = "Active power+ (Q1+Q4)" - Import direction
- Resolution: 1 watt per unit (Format 4.3 as per specification)
- Measurement interval: Appears to be rapid sampling (likely every few seconds)
- Zero readings may indicate measurement protocol behavior rather than actual zero consumption

Norwegian HAN Specification – OBIS Codes
OBIS List version identifier: AIDON_V0001

List No. | OBIS Code        | Object Name                                          | Unit        | Data Type            | Detailed Description
---------|------------------|------------------------------------------------------|-------------|----------------------|-----------------------------------------------------
1        | 1.0.1.7.0.255    | Active power+ (Q1+Q4)                               | kW          | double-long-unsigned | Active power in import direction, with resolution of W, Format 4.3 (xxxx,xxx kW)
2        | 1.1.0.2.129.255  | OBIS List version identifier                        | -           | visible-string       | Version number of this OBIS list to track the changes
3        | 0.0.96.1.0.255   | Meter ID (GIAI GS1 16 digit)                        | -           | visible-string       | Serial number of the meter point:16 digits 9999999999999999
4        | 0.0.96.1.7.255   | Meter type                                           | -           | visible-string       | Type number of the meter: "6515, 6525, 6534, 6540, 6550"
5        | 1.0.1.7.0.255    | Active power+ (Q1+Q4)                               | kW          | double-long-unsigned | Active power in import direction, resolution of W, Format 4.3 (xxxx,xxx kW)
6        | 1.0.2.7.0.255    | Active power - (Q2+Q3)                              | kW          | double-long-unsigned | Active power in export direction, resolution of W, Format 4.3 (xxxx,xxx kW)
7        | 1.0.3.7.0.255    | Reactive power + (Q1+Q2)                            | kVAr        | double-long-unsigned | Reactive power in import direction, resolution of VAr, Format 4.3 (xxxx,xxx kVAr)
8        | 1.0.4.7.0.255    | Reactive power - (Q3+Q4)                            | kVAr        | double-long-unsigned | Reactive power in export direction, resolution of VAr, Format 4.3 (xxxx,xxx kVAr)
9        | 1.0.31.7.0.255   | IL1 Current phase L1                                | A           | long-signed          | 0.5 second RMS current L1, resolution of 0.1 A, format 3.1 (xxx.x A) (3P3W current between L1 and L2 and part from current between L1 and L3)
10       | 1.0.51.7.0.255   | IL2 Current phase L2                                | A           | long-signed          | 0.5 second RMS current L2, resolution of 0.1 A, format 3.1 (xxx.x A) (3P3W N/A)
11       | 1.0.71.7.0.255   | IL3 Current phase L3                                | A           | long-signed          | 0.5 second RMS current L3, resolution of 0.1 A, format 3.1 (xxx.x A) (3P3W current between L2 and L3 and part from current between L1 and L3)
12       | 1.0.32.7.0.255   | UL1 Phase voltage 4W meter, Line voltage 3W meter  | V           | long-unsigned        | 0.5 second RMS voltage L1, resolution of 0.1 V, format 3.1 (xxx.x V) (3P3W line voltage L1-L2)
13       | 1.0.52.7.0.255   | UL2 Phase voltage 4W meter, Line voltage 3W meter  | V           | long-unsigned        | 0.5 second RMS voltage L2, resolution of 0.1 V, format 3.1 (xxx.x V) (3P3W line voltage L1-L3)
14       | 1.0.72.7.0.255   | UL3 Phase voltage 4W meter, Line voltage 3W meter  | V           | long-unsigned        | 0.5 second RMS voltage L3, resolution of 0.1 V, format 3.1 (xxx.x V) (3P3W line voltage L2-L3)
15       | 0.0.1.0.0.255    | Clock and date in meter                             | -           | octet-string         | Local date and time of Norway
16       | 1.0.1.8.0.255    | Cumulative hourly active import energy (A+) (Q1+Q4)| kWh/Wh*     | double-long-unsigned | Active energy import, resolution of 10 Wh, format 7.2 (xxxxxxx.xx kWh) / Active energy import, resolution of 0.01 Wh, format 7.2 (xxxxxxx.xx Wh) *
17       | 1.0.2.8.0.255    | Cumulative hourly active export energy (A-) (Q2+Q3)| kWh/Wh*     | double-long-unsigned | Active energy export, resolution of 10 Wh, format 7.2 (xxxxxxx.xx kWh) / Active energy export, resolution of 0.01 Wh, format 7.2 (xxxxxxx.xx Wh) *
18       | 1.0.3.8.0.255    | Cumulative hourly reactive import energy (R+) (Q1+Q2) | kVArh/VArh* | double-long-unsigned | Reactive energy import, resolution of 10 VArh, format 7.2 (xxxxxxx.xx kVArh) / Reactive energy import, resolution of 0.01 VArh, format 7.2 (xxxxxxx.xx VArh) *
19       | 1.0.4.8.0.255    | Cumulative hourly reactive export energy (R-) (Q3+Q4) | kVArh/VArh* | double-long-unsigned | Reactive energy export, resolution of 10 VArh, format 7.2 (xxxxxxx.xx kVArh) / Reactive energy export, resolution of 0.01 VArh, format 7.2 (xxxxxxx.xx VArh) *

Notes:
- This table represents the OBIS (Object Identification System) codes for Norwegian HAN (Home Area Network) specification
- OBIS codes follow the format: A.B.C.D.E.F where each position has specific meaning
- Data types include: double-long-unsigned, visible-string, long-signed, long-unsigned, and octet-string
- Energy values may be in kWh or Wh depending on meter configuration (marked with *)
- Format notation: x.y means x total digits with y decimal places (e.g., 4.3 = xxxx.xxx)
- 3P3W refers to 3-phase 3-wire configuration
- RMS = Root Mean Square
- Q1, Q2, Q3, Q4 refer to quadrants in the power measurement system




