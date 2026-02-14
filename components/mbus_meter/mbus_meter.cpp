#include "mbus_meter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mbus_meter {

static const char *const TAG = "mbus_meter";

void MbusMeter::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Norwegian HAN M-Bus Meter...");
  this->uart_counter_ = 0;
  this->last_frame_time_ = 0;
}

void MbusMeter::dump_config() {
  ESP_LOGCONFIG(TAG, "Norwegian HAN M-Bus Meter:");
  ESP_LOGCONFIG(TAG, "  UART Buffer Size: %d bytes", sizeof(this->uart_buffer_));
  LOG_SENSOR("  ", "Power", this->power_sensor_);
  LOG_SENSOR("  ", "Current L1", this->current_l1_sensor_);
  LOG_SENSOR("  ", "Current L2", this->current_l2_sensor_);
  LOG_SENSOR("  ", "Current L3", this->current_l3_sensor_);
  LOG_SENSOR("  ", "Voltage L1", this->voltage_l1_sensor_);
  LOG_SENSOR("  ", "Voltage L2", this->voltage_l2_sensor_);
  LOG_SENSOR("  ", "Voltage L3", this->voltage_l3_sensor_);
  LOG_SENSOR("  ", "Energy", this->energy_sensor_);
  LOG_SENSOR("  ", "Reactive Power", this->reactive_power_sensor_);
  LOG_SENSOR("  ", "Reactive Energy", this->reactive_energy_sensor_);
  LOG_SENSOR("  ", "Reactive Export Energy", this->reactive_export_energy_sensor_);
  LOG_SENSOR("  ", "Power 2A Frame", this->power_2a_frame_sensor_);
  ESP_LOGCONFIG(TAG, "  Use 2A Frame Own Sensor: %s", this->use_2a_frame_own_sensor_ ? "YES" : "NO");
  LOG_TEXT_SENSOR("  ", "OBIS Version", this->obis_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Meter ID", this->meter_id_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Meter Type", this->meter_type_text_sensor_);
}

void MbusMeter::loop() {
  this->read_message();
}

void MbusMeter::reset_buffer() {
  this->uart_counter_ = 0;
}

bool MbusMeter::is_valid_frame_start(uint16_t position) {
  if (position + 2 >= this->uart_counter_) return false;
  return ((this->uart_buffer_[position] == 0x2A || this->uart_buffer_[position] == 0xA1) &&
          this->uart_buffer_[position + 1] == 0x08 &&
          this->uart_buffer_[position + 2] == 0x83);
}

bool MbusMeter::read_message() {
  uint32_t now = millis();

  // Frame timeout - process accumulated data if no new bytes arrive
  if (this->uart_counter_ > 0 && now - this->last_frame_time_ > FRAME_TIMEOUT_MS) {
    if (this->uart_buffer_[0] == 0xA1 && this->uart_counter_ >= 100) {
      ESP_LOGD(TAG, "A1 frame timeout - processing %d bytes", this->uart_counter_);
      this->process_current_frame();
    } else if (this->uart_buffer_[0] == 0x2A && this->uart_counter_ >= 18) {
      ESP_LOGD(TAG, "2A frame timeout - processing %d bytes", this->uart_counter_);
      this->process_current_frame();
    } else {
      ESP_LOGV(TAG, "Frame timeout: discarding %d bytes (insufficient data)", this->uart_counter_);
    }
    this->reset_buffer();
    return false;
  }

  // Read available bytes into buffer
  while (this->available() > 0 && this->uart_counter_ < sizeof(this->uart_buffer_)) {
    uint8_t byte;
    this->read_byte(&byte);
    this->last_frame_time_ = now;
    this->uart_buffer_[this->uart_counter_++] = byte;

    // Process complete frames based on type and minimum size
    if (this->uart_counter_ >= 20 && this->is_valid_frame_start(0)) {
      if (this->uart_buffer_[0] == 0xA1 && this->uart_counter_ >= 150) {
        ESP_LOGD(TAG, "Processing A1 frame of %d bytes", this->uart_counter_);
        this->process_current_frame();
        this->reset_buffer();
        return true;
      } else if (this->uart_buffer_[0] != 0xA1 && this->uart_counter_ >= 50) {
        this->process_current_frame();
        this->reset_buffer();
        return true;
      }
    }

    // Buffer overflow protection
    if (this->uart_counter_ >= sizeof(this->uart_buffer_) - 1) {
      ESP_LOGW(TAG, "Buffer overflow at %d bytes - processing and resetting", this->uart_counter_);
      this->process_current_frame();
      this->reset_buffer();
      return false;
    }
  }

  return false;
}

void MbusMeter::process_current_frame() {
  if (this->uart_counter_ < 10) return;

  // 2A frames: short real-time power frames
  // Pattern: 2A:08:83:...:01:01:07:[POWER]:02:02:16...
  if (this->uart_buffer_[0] == 0x2A) {
    uint32_t power_value = this->search_for_real_time_power();
    if (power_value > 0) {
      ESP_LOGI(TAG, "2A frame: Power: %u W", power_value);
      if (this->use_2a_frame_own_sensor_ && this->power_2a_frame_sensor_ != nullptr) {
        this->power_2a_frame_sensor_->publish_state(power_value);
      } else if (!this->use_2a_frame_own_sensor_ && this->power_sensor_ != nullptr) {
        this->power_sensor_->publish_state(power_value);
      }
    } else {
      ESP_LOGD(TAG, "2A frame: No valid power reading found");
    }
    return;
  }

  // A1 frames: comprehensive meter data
  if (this->uart_buffer_[0] == 0xA1) {
    ESP_LOGI(TAG, "A1 frame detected, length: %d bytes", this->uart_counter_);
    this->parse_a1_frame();
    return;
  }

  // Unknown frame type - scan for HAN OBIS patterns (02:02:01)
  for (uint16_t i = 0; i + 5 < this->uart_counter_; i++) {
    if (this->uart_buffer_[i] == 0x02 &&
        this->uart_buffer_[i + 1] == 0x02 &&
        this->uart_buffer_[i + 2] == 0x01) {
      this->parse_han_obis(i);
    }
  }
}

void MbusMeter::parse_han_obis(uint16_t position) {
  if (position + 10 >= this->uart_counter_) return;
  if (position + 4 >= this->uart_counter_) return;

  // Pattern: 02:02:01:[OBIS_TYPE]:[LENGTH]:[DATA...]
  uint8_t obis_type = this->uart_buffer_[position + 3];
  uint8_t data_length = this->uart_buffer_[position + 4];

  switch (obis_type) {
    case 0x01:
      // OBIS List version identifier (1.1.0.2.129.255) - visible-string
      if (data_length == 0x02 && position + 6 < this->uart_counter_) {
        uint16_t text_start = position + 5;
        if (this->uart_buffer_[text_start] == 0x0B) text_start++;  // Skip length prefix
        this->parse_text_value(text_start, this->obis_version_text_sensor_);
      }
      break;

    case 0x07:
      // Active power+ (1.0.1.7.0.255) - double-long-unsigned
      if (data_length == 0x04 && position + 9 < this->uart_counter_) {
        uint32_t power = this->extract_obis_value(position + 5, 4);
        ESP_LOGI(TAG, "Active power+ (1.0.1.7.0.255): %u W", power);
        if (!this->use_2a_frame_own_sensor_ && this->power_sensor_ != nullptr) {
          this->power_sensor_->publish_state(power);
        }
      }
      break;

    case 0x10:
      // Meter ID (0.0.96.1.0.255) - visible-string, 16 digits
      {
        uint8_t safe_length = (data_length > 20) ? 20 : data_length;
        if (position + 5 + safe_length < this->uart_counter_) {
          this->parse_text_value(position + 5, this->meter_id_text_sensor_);
        }
      }
      break;

    case 0x1F:  // Current L1 (1.0.31.7.0.255)
      if (data_length >= 0x02 && position + 7 < this->uart_counter_)
        this->parse_current_value(position + 5, 1);
      break;
    case 0x33:  // Current L2 (1.0.51.7.0.255)
      if (data_length >= 0x02 && position + 7 < this->uart_counter_)
        this->parse_current_value(position + 5, 2);
      break;
    case 0x47:  // Current L3 (1.0.71.7.0.255)
      if (data_length >= 0x02 && position + 7 < this->uart_counter_)
        this->parse_current_value(position + 5, 3);
      break;

    case 0x20:  // Voltage L1 (1.0.32.7.0.255)
      if (data_length >= 0x02 && position + 7 < this->uart_counter_)
        this->parse_voltage_value(position + 5, 1);
      break;
    case 0x34:  // Voltage L2 (1.0.52.7.0.255)
      if (data_length >= 0x02 && position + 7 < this->uart_counter_)
        this->parse_voltage_value(position + 5, 2);
      break;
    case 0x48:  // Voltage L3 (1.0.72.7.0.255)
      if (data_length >= 0x02 && position + 7 < this->uart_counter_)
        this->parse_voltage_value(position + 5, 3);
      break;

    case 0x08:
      // Active energy import (1.0.1.8.0.255) - double-long-unsigned
      if (data_length == 0x04 && position + 9 < this->uart_counter_)
        this->parse_energy_value(position + 5);
      break;

    case 0x02: {
      // Active power- export (1.0.2.7.0.255)
      if (data_length == 0x04 && position + 9 < this->uart_counter_) {
        uint32_t export_power = this->extract_obis_value(position + 5, 4);
        ESP_LOGI(TAG, "Active power- export (1.0.2.7.0.255): %u W", export_power);
      }
      break;
    }

    case 0x03: {
      // Reactive power+ import (1.0.3.7.0.255)
      if (data_length == 0x04 && position + 9 < this->uart_counter_) {
        uint32_t reactive_power = this->extract_obis_value(position + 5, 4);
        ESP_LOGI(TAG, "Reactive power+ import (1.0.3.7.0.255): %u VAr", reactive_power);
        if (this->reactive_power_sensor_ != nullptr)
          this->reactive_power_sensor_->publish_state(reactive_power);
      }
      break;
    }

    case 0x04: {
      // Reactive power- export (1.0.4.7.0.255)
      if (data_length == 0x04 && position + 9 < this->uart_counter_) {
        uint32_t reactive_export = this->extract_obis_value(position + 5, 4);
        ESP_LOGI(TAG, "Reactive power- export (1.0.4.7.0.255): %u VAr", reactive_export);
      }
      break;
    }

    default:
      break;
  }
}

void MbusMeter::parse_current_value(uint16_t position, uint8_t phase) {
  if (position + 1 >= this->uart_counter_) return;

  // Norwegian HAN spec: long-signed, 0.1A resolution, format 3.1 (xxx.x A)
  int16_t raw_current = (this->uart_buffer_[position] << 8) | this->uart_buffer_[position + 1];
  float current_a = fabs(raw_current / 10.0f);

  const char *obis_codes[] = {"1.0.31.7.0.255", "1.0.51.7.0.255", "1.0.71.7.0.255"};
  ESP_LOGI(TAG, "Current L%d (%s): %.1f A (raw: %d)", phase, obis_codes[phase - 1], current_a, raw_current);

  sensor::Sensor *sensors[] = {this->current_l1_sensor_, this->current_l2_sensor_, this->current_l3_sensor_};
  if (phase >= 1 && phase <= 3 && sensors[phase - 1] != nullptr) {
    sensors[phase - 1]->publish_state(current_a);
  }
}

void MbusMeter::parse_voltage_value(uint16_t position, uint8_t phase) {
  if (position + 1 >= this->uart_counter_) return;

  // Norwegian HAN spec: long-unsigned, 0.1V resolution, format 3.1 (xxx.x V)
  uint16_t raw_voltage = (this->uart_buffer_[position] << 8) | this->uart_buffer_[position + 1];
  float voltage_v = raw_voltage / 10.0f;

  if (voltage_v < 100.0f || voltage_v > 300.0f) {
    ESP_LOGW(TAG, "Voltage L%d out of range: %.1f V (raw: %u)", phase, voltage_v, raw_voltage);
    return;
  }

  const char *obis_codes[] = {"1.0.32.7.0.255", "1.0.52.7.0.255", "1.0.72.7.0.255"};
  ESP_LOGI(TAG, "Voltage L%d (%s): %.1f V", phase, obis_codes[phase - 1], voltage_v);

  sensor::Sensor *sensors[] = {this->voltage_l1_sensor_, this->voltage_l2_sensor_, this->voltage_l3_sensor_};
  if (phase >= 1 && phase <= 3 && sensors[phase - 1] != nullptr) {
    sensors[phase - 1]->publish_state(voltage_v);
  }
}

void MbusMeter::parse_energy_value(uint16_t position) {
  if (position + 3 >= this->uart_counter_) return;

  // Norwegian HAN spec: double-long-unsigned, resolution 10 Wh, format 7.2
  uint32_t energy_raw = this->extract_obis_value(position, 4);
  uint32_t energy_wh = energy_raw * 10;

  ESP_LOGI(TAG, "Active import energy (1.0.1.8.0.255): %u Wh (raw: %u)", energy_wh, energy_raw);

  if (this->energy_sensor_ != nullptr) {
    this->energy_sensor_->publish_state(energy_wh);
  }
}

void MbusMeter::parse_text_value(uint16_t position, text_sensor::TextSensor *sensor) {
  if (sensor == nullptr || position >= this->uart_counter_) return;

  std::string text_value;
  for (uint16_t i = 0; i < 20 && (position + i) < this->uart_counter_; i++) {
    uint8_t byte = this->uart_buffer_[position + i];
    if (byte >= 32 && byte <= 126) {
      text_value += (char) byte;
    } else if (byte == 0x00 || byte < 32) {
      break;
    }
  }

  if (!text_value.empty()) {
    ESP_LOGI(TAG, "Text value: '%s'", text_value.c_str());
    sensor->publish_state(text_value);
  }
}

uint32_t MbusMeter::extract_obis_value(uint16_t position, uint8_t length) {
  if (position + length > this->uart_counter_) {
    ESP_LOGW(TAG, "Not enough data at pos %d, need %d bytes", position, length);
    return 0;
  }
  uint32_t value = 0;
  for (uint8_t i = 0; i < length && i < 4; i++) {
    value = (value << 8) | this->uart_buffer_[position + i];
  }
  return value;
}

uint32_t MbusMeter::search_for_real_time_power() {
  // Search for pattern: 01:01:07:[POWER_BYTES]:02:02:16
  // Handles both two-byte and single-byte power values

  for (uint16_t i = 0; i + 6 < this->uart_counter_; i++) {
    if (this->uart_buffer_[i] != 0x01 ||
        this->uart_buffer_[i + 1] != 0x01 ||
        this->uart_buffer_[i + 2] != 0x07) continue;

    // Two-byte power: 01:01:07:XX:YY:02:02:16
    if (i + 7 < this->uart_counter_ &&
        this->uart_buffer_[i + 5] == 0x02 &&
        this->uart_buffer_[i + 6] == 0x02 &&
        this->uart_buffer_[i + 7] == 0x16) {
      uint32_t power = (this->uart_buffer_[i + 3] << 8) | this->uart_buffer_[i + 4];
      ESP_LOGD(TAG, "2A power (two-byte): %u W [%02X:%02X]", power,
               this->uart_buffer_[i + 3], this->uart_buffer_[i + 4]);
      return power;
    }

    // Single-byte power: 01:01:07:XX:02:02:16
    if (i + 6 < this->uart_counter_ &&
        this->uart_buffer_[i + 4] == 0x02 &&
        this->uart_buffer_[i + 5] == 0x02 &&
        this->uart_buffer_[i + 6] == 0x16) {
      uint32_t power = this->uart_buffer_[i + 3];
      ESP_LOGD(TAG, "2A power (single-byte): %u W [%02X]", power, this->uart_buffer_[i + 3]);
      return power;
    }
  }

  return 0;
}

void MbusMeter::parse_a1_frame() {
  // A1 frame structure:
  // Header: A1:[...]:02:02:01:01:02:0B:[version]:02:02:01:10:[meter_id]:02:02:01:07:...
  // OBIS entries separated by 02:02:16
  // Standard entry:  02:01:[TYPE]:07:[VALUE_BYTES]
  // Energy entry:    02:01:[TYPE]:08:[VALUE_BYTES]

  // Extract text sensors from header: 02:02:01:[TYPE]:[DATA...]
  for (uint16_t i = 0; i + 4 < this->uart_counter_ && i < 40; i++) {
    if (this->uart_buffer_[i] != 0x02 || this->uart_buffer_[i + 1] != 0x02 || this->uart_buffer_[i + 2] != 0x01)
      continue;
    uint8_t type = this->uart_buffer_[i + 3];
    if (type == 0x01 && i + 6 < this->uart_counter_) {
      // OBIS version (1.1.0.2.129.255): skip non-printable prefix bytes (02:0B)
      uint16_t text_pos = i + 4;
      while (text_pos < this->uart_counter_ && text_pos < i + 8 &&
             (this->uart_buffer_[text_pos] < 0x20 || this->uart_buffer_[text_pos] > 0x7E)) {
        text_pos++;
      }
      this->parse_text_value(text_pos, this->obis_version_text_sensor_);
    } else if (type == 0x07 && i + 5 < this->uart_counter_) {
      // Meter type (0.0.96.1.7.255): skip non-printable prefix bytes
      uint16_t text_pos = i + 4;
      while (text_pos < this->uart_counter_ && text_pos < i + 8 &&
             (this->uart_buffer_[text_pos] < 0x20 || this->uart_buffer_[text_pos] > 0x7E)) {
        text_pos++;
      }
      this->parse_text_value(text_pos, this->meter_type_text_sensor_);
    } else if (type == 0x10 && i + 5 < this->uart_counter_) {
      // Meter ID (0.0.96.1.0.255)
      this->parse_text_value(i + 4, this->meter_id_text_sensor_);
    }
  }

  // Verbose hex dump for debugging
  ESP_LOGV(TAG, "A1 frame hex dump (%d bytes):", this->uart_counter_);
  for (uint16_t i = 0; i < this->uart_counter_ && i < 300; i += 16) {
    std::string line;
    for (uint16_t j = i; j < i + 16 && j < this->uart_counter_; j++) {
      char hex[4];
      sprintf(hex, "%02X:", this->uart_buffer_[j]);
      line += hex;
    }
    ESP_LOGV(TAG, "  %04X: %s", i, line.c_str());
  }

  // Search for energy counter patterns: 02:01:XX:08:...
  bool found_reactive_import = false;
  for (uint16_t i = 15; i + 4 < this->uart_counter_; i++) {
    if (this->uart_buffer_[i] == 0x02 &&
        this->uart_buffer_[i + 1] == 0x01 &&
        this->uart_buffer_[i + 3] == 0x08) {

      uint8_t energy_type = this->uart_buffer_[i + 2];

      uint16_t value_start = i + 4;
      uint16_t value_end = this->find_next_separator(value_start);
      uint16_t value_length = value_end - value_start;

      // Value length 0 means the energy counter is 0
      uint32_t energy_raw = 0;
      if (value_length >= 4) {
        energy_raw = this->extract_obis_value(value_start, 4);
      } else if (value_length >= 2) {
        energy_raw = this->extract_obis_value(value_start, 2);
      } else if (value_length >= 1) {
        energy_raw = this->uart_buffer_[value_start];
      }

      // Resolution: 10 Wh/VArh per the HAN spec
      uint32_t energy_scaled = energy_raw * 10;

      switch (energy_type) {
        case 0x01:
          ESP_LOGI(TAG, "A1: Active energy import (1.0.1.8.0.255): %u Wh [raw: %u]", energy_scaled, energy_raw);
          if (this->energy_sensor_ != nullptr) this->energy_sensor_->publish_state(energy_scaled);
          break;
        case 0x02:
          ESP_LOGI(TAG, "A1: Active energy export (1.0.2.8.0.255): %u Wh [raw: %u]", energy_scaled, energy_raw);
          break;
        case 0x03:
          ESP_LOGI(TAG, "A1: Reactive energy import (1.0.3.8.0.255): %u VArh [raw: %u]", energy_scaled, energy_raw);
          if (this->reactive_energy_sensor_ != nullptr) this->reactive_energy_sensor_->publish_state(energy_scaled);
          found_reactive_import = true;
          break;
        case 0x04:
          ESP_LOGI(TAG, "A1: Reactive energy export (1.0.4.8.0.255): %u VArh [raw: %u]", energy_scaled, energy_raw);
          if (this->reactive_export_energy_sensor_ != nullptr) this->reactive_export_energy_sensor_->publish_state(energy_scaled);
          break;
        default:
          ESP_LOGD(TAG, "A1: Unknown energy type 0x%02X: %u [raw: %u]", energy_type, energy_scaled, energy_raw);
          break;
      }

      i += 4;
    }
  }

  // Second pass: compact energy pattern 02:01:08:VALUE (OBIS type byte omitted)
  // Some meters omit the type byte for reactive energy import
  if (!found_reactive_import) {
    for (uint16_t i = 15; i + 3 < this->uart_counter_; i++) {
      if (this->uart_buffer_[i] == 0x02 &&
          this->uart_buffer_[i + 1] == 0x01 &&
          this->uart_buffer_[i + 2] == 0x08) {
        uint16_t value_start = i + 3;
        uint16_t value_end = this->find_next_separator(value_start);
        uint16_t value_length = value_end - value_start;

        if (value_length > 0 && value_length <= 4) {
          uint32_t energy_raw = 0;
          if (value_length >= 4) {
            energy_raw = this->extract_obis_value(value_start, 4);
          } else if (value_length >= 2) {
            energy_raw = this->extract_obis_value(value_start, 2);
          } else {
            energy_raw = this->uart_buffer_[value_start];
          }
          uint32_t energy_scaled = energy_raw * 10;

          ESP_LOGI(TAG, "A1: Reactive energy import (1.0.3.8.0.255): %u VArh [raw: %u, compact]",
                   energy_scaled, energy_raw);
          if (this->reactive_energy_sensor_ != nullptr) {
            this->reactive_energy_sensor_->publish_state(energy_scaled);
          }
          break;
        }
      }
    }
  }

  // Search for standard OBIS patterns: 02:01:XX:07:...
  for (uint16_t i = 15; i + 3 < this->uart_counter_; i++) {
    // Standard pattern
    if (this->uart_buffer_[i] == 0x02 &&
        this->uart_buffer_[i + 1] == 0x01 &&
        this->uart_buffer_[i + 3] == 0x07) {

      uint8_t obis_type = this->uart_buffer_[i + 2];
      uint16_t data_start = i + 4;
      uint16_t data_end = this->find_next_separator(data_start);

      if (data_end > data_start && data_end - data_start <= 8) {
        this->parse_a1_obis_value(obis_type, data_start, data_end);
      }

      if (data_end > i) i = data_end + 2;
      continue;
    }

    // Voltage alternate pattern: 23:02:01:XX:07:...
    if (this->uart_buffer_[i] == 0x23 &&
        i + 4 < this->uart_counter_ &&
        this->uart_buffer_[i + 1] == 0x02 &&
        this->uart_buffer_[i + 2] == 0x01 &&
        this->uart_buffer_[i + 4] == 0x07) {

      uint8_t obis_type = this->uart_buffer_[i + 3];
      uint16_t data_start = i + 5;
      uint16_t data_end = this->find_next_separator(data_start);

      if (data_end > data_start) {
        this->parse_a1_obis_value(obis_type, data_start, data_end);
      }

      if (data_end > i) i = data_end + 2;
    }
  }
}

uint16_t MbusMeter::find_next_separator(uint16_t start_pos) {
  for (uint16_t i = start_pos; i + 2 < this->uart_counter_; i++) {
    // Standard separator: 02:02:16
    if (this->uart_buffer_[i] == 0x02 &&
        this->uart_buffer_[i + 1] == 0x02 &&
        this->uart_buffer_[i + 2] == 0x16) {
      return i;
    }
    // Energy section separator: 02:02:01:16
    if (i + 3 < this->uart_counter_ &&
        this->uart_buffer_[i] == 0x02 &&
        this->uart_buffer_[i + 1] == 0x02 &&
        this->uart_buffer_[i + 2] == 0x01 &&
        this->uart_buffer_[i + 3] == 0x16) {
      return i;
    }
  }
  return this->uart_counter_;
}

void MbusMeter::parse_a1_obis_value(uint8_t obis_type, uint16_t data_start, uint16_t data_end) {
  if (data_end <= data_start) return;
  uint16_t data_length = data_end - data_start;

  switch (obis_type) {
    case 0x01:  // Active power+ (1.0.1.7.0.255)
      if (data_length >= 2) {
        uint32_t power = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        ESP_LOGI(TAG, "A1: Active power+ (1.0.1.7.0.255): %u W", power);
        if (this->power_sensor_ != nullptr) this->power_sensor_->publish_state(power);
      }
      break;

    case 0x02:  // Active power- (1.0.2.7.0.255)
      if (data_length >= 2) {
        uint32_t export_power = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        ESP_LOGI(TAG, "A1: Active power- (1.0.2.7.0.255): %u W", export_power);
      }
      break;

    case 0x03:  // Reactive power+ (1.0.3.7.0.255)
      if (data_length >= 2) {
        uint32_t rp = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        ESP_LOGI(TAG, "A1: Reactive power+ (1.0.3.7.0.255): %u VAr", rp);
        if (this->reactive_power_sensor_ != nullptr) this->reactive_power_sensor_->publish_state(rp);
      }
      break;

    case 0x04:  // Reactive power- (1.0.4.7.0.255)
      if (data_length >= 2) {
        uint32_t rp_export = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        ESP_LOGI(TAG, "A1: Reactive power- (1.0.4.7.0.255): %u VAr", rp_export);
      }
      break;

    case 0x1F:  // Current L1 (1.0.31.7.0.255)
    case 0x33:  // Current L2 (1.0.51.7.0.255)
    case 0x47:  // Current L3 (1.0.71.7.0.255)
      if (data_length >= 2) {
        // Pattern: [data_type_byte]:[value_byte], value is 0.1A resolution
        uint8_t current_raw = this->uart_buffer_[data_start + 1];
        float current_a = current_raw / 10.0f;
        uint8_t phase = (obis_type == 0x1F) ? 1 : (obis_type == 0x33) ? 2 : 3;

        const char *obis_codes[] = {"1.0.31.7.0.255", "1.0.51.7.0.255", "1.0.71.7.0.255"};
        ESP_LOGI(TAG, "A1: Current L%d (%s): %.1f A", phase, obis_codes[phase - 1], current_a);

        sensor::Sensor *sensors[] = {this->current_l1_sensor_, this->current_l2_sensor_, this->current_l3_sensor_};
        if (sensors[phase - 1] != nullptr) sensors[phase - 1]->publish_state(current_a);
      }
      break;

    case 0x20:  // Voltage L1 (1.0.32.7.0.255)
    case 0x34:  // Voltage L2 (1.0.52.7.0.255)
    case 0x48:  // Voltage L3 (1.0.72.7.0.255)
      if (data_length >= 2) {
        uint16_t voltage_raw = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        float voltage_v = voltage_raw / 10.0f;
        uint8_t phase = (obis_type == 0x20) ? 1 : (obis_type == 0x34) ? 2 : 3;

        if (voltage_v < 100.0f || voltage_v > 300.0f) {
          ESP_LOGW(TAG, "A1: Voltage L%d out of range: %.1f V", phase, voltage_v);
          break;
        }

        const char *obis_codes[] = {"1.0.32.7.0.255", "1.0.52.7.0.255", "1.0.72.7.0.255"};
        ESP_LOGI(TAG, "A1: Voltage L%d (%s): %.1f V", phase, obis_codes[phase - 1], voltage_v);

        sensor::Sensor *sensors[] = {this->voltage_l1_sensor_, this->voltage_l2_sensor_, this->voltage_l3_sensor_};
        if (sensors[phase - 1] != nullptr) sensors[phase - 1]->publish_state(voltage_v);
      }
      break;

    default:
      ESP_LOGD(TAG, "A1: Unknown OBIS type 0x%02X (%d bytes)", obis_type, data_length);
      break;
  }
}

}  // namespace mbus_meter
}  // namespace esphome
