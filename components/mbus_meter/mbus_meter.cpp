#include "mbus_meter.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cmath>

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
  ESP_LOGCONFIG(TAG, "  UART Buffer Size: %u bytes", (unsigned) sizeof(this->uart_buffer_));
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

void MbusMeter::loop() { this->read_message(); }

void MbusMeter::reset_buffer() { this->uart_counter_ = 0; }

bool MbusMeter::is_valid_frame_start(uint16_t position) {
  if (position + 2 >= this->uart_counter_)
    return false;
  return ((this->uart_buffer_[position] == 0x2A || this->uart_buffer_[position] == 0xA1) &&
          this->uart_buffer_[position + 1] == 0x08 && this->uart_buffer_[position + 2] == 0x83);
}

bool MbusMeter::read_message() {
  uint32_t now = millis();

  // Frame timeout - process accumulated data if no new bytes arrive
  if (this->uart_counter_ > 0 && now - this->last_frame_time_ > FRAME_TIMEOUT_MS) {
    if (this->uart_buffer_[0] == 0xA1 && this->uart_counter_ >= A1_FRAME_MIN_BYTES_TIMEOUT) {
      ESP_LOGD(TAG, "A1 frame timeout - processing %u bytes", this->uart_counter_);
      this->process_current_frame();
    } else if (this->uart_buffer_[0] == 0x2A && this->uart_counter_ >= SHORT_FRAME_MIN_BYTES_TIMEOUT) {
      ESP_LOGD(TAG, "2A frame timeout - processing %u bytes", this->uart_counter_);
      this->process_current_frame();
    } else {
      ESP_LOGV(TAG, "Frame timeout: discarding %u bytes (insufficient data)", this->uart_counter_);
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
    if (this->uart_counter_ >= FRAME_START_MIN_BYTES && this->is_valid_frame_start(0)) {
      if (this->uart_buffer_[0] == 0xA1 && this->uart_counter_ >= A1_FRAME_MIN_BYTES_IMMEDIATE) {
        ESP_LOGD(TAG, "Processing A1 frame of %u bytes", this->uart_counter_);
        this->process_current_frame();
        this->reset_buffer();
        return true;
      } else if (this->uart_buffer_[0] != 0xA1 && this->uart_counter_ >= SHORT_FRAME_MIN_BYTES_IMMEDIATE) {
        this->process_current_frame();
        this->reset_buffer();
        return true;
      }
    }

    // Buffer overflow protection
    if (this->uart_counter_ >= sizeof(this->uart_buffer_) - 1) {
      ESP_LOGW(TAG, "Buffer overflow at %u bytes - processing and resetting", this->uart_counter_);
      this->process_current_frame();
      this->reset_buffer();
      return false;
    }
  }

  return false;
}

void MbusMeter::process_current_frame() {
  if (this->uart_counter_ < 10)
    return;

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
    ESP_LOGI(TAG, "A1 frame detected, length: %u bytes", this->uart_counter_);
    this->parse_a1_frame();
    return;
  }

  // Unknown frame type - scan for HAN OBIS patterns (02:02:01)
  for (uint16_t i = 0; i + 5 < this->uart_counter_; i++) {
    if (this->uart_buffer_[i] == 0x02 && this->uart_buffer_[i + 1] == 0x02 && this->uart_buffer_[i + 2] == 0x01) {
      this->parse_han_obis(i);
    }
  }
}

void MbusMeter::parse_han_obis(uint16_t position) {
  if (position + 10 >= this->uart_counter_)
    return;

  // Pattern: 02:02:01:[OBIS_TYPE]:[LENGTH]:[DATA...]
  uint8_t obis_type = this->uart_buffer_[position + 3];
  uint8_t data_length = this->uart_buffer_[position + 4];

  switch (obis_type) {
    case 0x01:
      // OBIS List version identifier (1.1.0.2.129.255) - visible-string
      if (data_length == 0x02 && position + 6 < this->uart_counter_) {
        uint16_t text_start = position + 5;
        if (this->uart_buffer_[text_start] == 0x0B)
          text_start++;  // Skip length prefix
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
  if (position + 1 >= this->uart_counter_)
    return;

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
  if (position + 1 >= this->uart_counter_)
    return;

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
  if (position + 3 >= this->uart_counter_)
    return;

  // Norwegian HAN spec: double-long-unsigned, resolution 10 Wh, format 7.2
  uint32_t energy_raw = this->extract_obis_value(position, 4);
  uint32_t energy_wh = energy_raw * 10;

  ESP_LOGI(TAG, "Active import energy (1.0.1.8.0.255): %u Wh (raw: %u)", energy_wh, energy_raw);

  if (this->energy_sensor_ != nullptr) {
    this->energy_sensor_->publish_state(energy_wh);
  }
}

void MbusMeter::parse_text_value(uint16_t position, text_sensor::TextSensor *sensor) {
  if (sensor == nullptr || position >= this->uart_counter_)
    return;

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

uint32_t MbusMeter::reconstruct_power(uint8_t low_byte) {
  // A single-byte power value is the low byte of the true value; the meter
  // drops the high byte(s) (e.g. 0x5D for 10:5D = 4189 W). Candidates are
  // low_byte + k*256; recover k from a reference power:
  // - If the total current is unchanged since the last published power, the
  //   load level is unchanged - use the last published power (exact tracking).
  // - Otherwise estimate from the phase currents (400/230V TN grid:
  //   S ~= 230 V * (IL1+IL2+IL3)), corrected for the known reactive power.
  //   Worst-case error is one k step (256 W) for loads with poor power factor.
  float sum_i = this->last_current_a_[0] + this->last_current_a_[1] + this->last_current_a_[2];
  float ref;
  if (this->last_power_w_ > 0 && fabsf(sum_i - this->last_power_sum_i_) < 1.0f) {
    ref = (float) this->last_power_w_;
  } else {
    float s_est = 230.0f * sum_i;
    float q = fabsf(this->frame_reactive_net_);
    float p_sq = s_est * s_est - q * q;
    ref = (p_sq > 0.0f) ? sqrtf(p_sq) : 0.0f;
  }
  int32_t k = (int32_t) ((ref - (float) low_byte) / 256.0f + 0.5f);
  if (k < 0)
    k = 0;
  uint32_t best = low_byte;
  int32_t best_diff = 0x7FFFFFFF;
  for (int32_t kk = k - 1; kk <= k + 1; kk++) {
    if (kk < 0)
      continue;
    uint32_t cand = (uint32_t) kk * 256 + low_byte;
    int32_t diff = (int32_t) cand - (int32_t) ref;
    if (diff < 0)
      diff = -diff;
    if (diff < best_diff) {
      best_diff = diff;
      best = cand;
    }
  }
  this->last_power_w_ = best;
  this->last_power_sum_i_ = sum_i;
  return best;
}

uint32_t MbusMeter::search_for_real_time_power() {
  // Search for pattern: 01:01:07:[POWER_BYTES]:02:02:16
  // Handles both two-byte and single-byte power values

  for (uint16_t i = 0; i + 6 < this->uart_counter_; i++) {
    if (this->uart_buffer_[i] != 0x01 || this->uart_buffer_[i + 1] != 0x01 || this->uart_buffer_[i + 2] != 0x07)
      continue;

    // Two-byte power: 01:01:07:XX:YY:02:02:16
    if (i + 7 < this->uart_counter_ && this->uart_buffer_[i + 5] == 0x02 && this->uart_buffer_[i + 6] == 0x02 &&
        this->uart_buffer_[i + 7] == 0x16) {
      uint32_t power = (this->uart_buffer_[i + 3] << 8) | this->uart_buffer_[i + 4];
      ESP_LOGD(TAG, "2A power (two-byte): %u W [%02X:%02X]", power, this->uart_buffer_[i + 3],
               this->uart_buffer_[i + 4]);
      this->last_power_w_ = power;
      this->last_power_sum_i_ = this->last_current_a_[0] + this->last_current_a_[1] + this->last_current_a_[2];
      return power;
    }

    // Single-byte power: 01:01:07:XX:02:02:16
    // The low byte of the true value with the high byte dropped by the meter
    // (e.g. the classic 0x29 = ~10500 W); reconstruct via the phase currents
    if (i + 6 < this->uart_counter_ && this->uart_buffer_[i + 4] == 0x02 && this->uart_buffer_[i + 5] == 0x02 &&
        this->uart_buffer_[i + 6] == 0x16) {
      uint8_t b = this->uart_buffer_[i + 3];
      uint32_t power = this->reconstruct_power(b);
      ESP_LOGD(TAG, "2A power (single-byte): %u W [reconstructed from 0x%02X]", power, b);
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
  ESP_LOGV(TAG, "A1 frame hex dump (%u bytes):", this->uart_counter_);
  for (uint16_t i = 0; i < this->uart_counter_ && i < 300; i += 16) {
    uint16_t chunk_len = (this->uart_counter_ - i < 16) ? this->uart_counter_ - i : 16;
    ESP_LOGV(TAG, "  %04X: %s", i, format_hex_pretty(&this->uart_buffer_[i], chunk_len).c_str());
  }

  // Search for energy counter patterns: 02:01:XX:08:...
  bool found_reactive_import = false;
  for (uint16_t i = 15; i + 4 < this->uart_counter_; i++) {
    if (this->uart_buffer_[i] == 0x02 && this->uart_buffer_[i + 1] == 0x01 && this->uart_buffer_[i + 3] == 0x08) {
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
          if (this->energy_sensor_ != nullptr)
            this->energy_sensor_->publish_state(energy_scaled);
          break;
        case 0x02:
          ESP_LOGI(TAG, "A1: Active energy export (1.0.2.8.0.255): %u Wh [raw: %u]", energy_scaled, energy_raw);
          break;
        case 0x03:
          ESP_LOGI(TAG, "A1: Reactive energy import (1.0.3.8.0.255): %u VArh [raw: %u]", energy_scaled, energy_raw);
          if (this->reactive_energy_sensor_ != nullptr)
            this->reactive_energy_sensor_->publish_state(energy_scaled);
          found_reactive_import = true;
          break;
        case 0x04:
          ESP_LOGI(TAG, "A1: Reactive energy export (1.0.4.8.0.255): %u VArh [raw: %u]", energy_scaled, energy_raw);
          if (this->reactive_export_energy_sensor_ != nullptr)
            this->reactive_export_energy_sensor_->publish_state(energy_scaled);
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
      if (this->uart_buffer_[i] == 0x02 && this->uart_buffer_[i + 1] == 0x01 && this->uart_buffer_[i + 2] == 0x08) {
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

          ESP_LOGI(TAG, "A1: Reactive energy import (1.0.3.8.0.255): %u VArh [raw: %u, compact]", energy_scaled,
                   energy_raw);
          if (this->reactive_energy_sensor_ != nullptr) {
            this->reactive_energy_sensor_->publish_state(energy_scaled);
          }
          break;
        }
      }
    }
  }

  // Search for standard OBIS patterns: 02:01:XX:07:...
  // The meter also sends "compressed" records with the OBIS type byte omitted
  // (02:01:07:<payload> instead of 02:01:XX:07:<payload>). Which OBIS such a
  // record belongs to follows from the fixed AIDON list order: currents carry
  // a 0x10 long-signed tag and follow IL1 (so L2, then L3); an empty payload
  // before the currents is Reactive power+ with value 0.
  uint8_t next_compressed_phase = 2;
  bool seen_current = false;
  this->frame_reactive_net_ = 0.0f;
  this->frame_reactive_seen_ = false;
  this->frame_power_pending_ = false;
  for (uint16_t i = 15; i + 3 < this->uart_counter_; i++) {
    // Standard pattern
    if (this->uart_buffer_[i] == 0x02 && this->uart_buffer_[i + 1] == 0x01 && this->uart_buffer_[i + 3] == 0x07) {
      uint8_t obis_type = this->uart_buffer_[i + 2];
      uint16_t data_start = i + 4;
      uint16_t data_end = this->find_next_separator(data_start);

      if (obis_type == 0x1F || obis_type == 0x33 || obis_type == 0x47) {
        seen_current = true;
        next_compressed_phase = (obis_type == 0x1F) ? 2 : (obis_type == 0x33) ? 3 : 4;
      }

      if (data_end > data_start && data_end - data_start <= 8) {
        this->parse_a1_obis_value(obis_type, data_start, data_end);
      }

      if (data_end > i)
        i = data_end + 2;
      continue;
    }

    // Compressed current record: 02:01:07:10:<value> (type byte omitted)
    if (this->uart_buffer_[i] == 0x02 && this->uart_buffer_[i + 1] == 0x01 && this->uart_buffer_[i + 2] == 0x07 &&
        this->uart_buffer_[i + 3] == 0x10) {
      uint16_t data_start = i + 3;  // keep the 0x10 tag, same layout as explicit current records
      uint16_t data_end = this->find_next_separator(data_start);

      if (data_end > data_start && data_end - data_start <= 8 && next_compressed_phase <= 3) {
        uint8_t obis_type = (next_compressed_phase == 2) ? 0x33 : 0x47;
        ESP_LOGD(TAG, "A1: Compressed current record at pos %u -> L%u", i, next_compressed_phase);
        this->parse_a1_obis_value(obis_type, data_start, data_end);
        next_compressed_phase++;
        seen_current = true;
      }

      if (data_end > i)
        i = data_end + 2;
      continue;
    }

    // Compressed empty record before the currents: Reactive power+ with no value (= 0)
    if (!seen_current && this->uart_buffer_[i] == 0x02 && this->uart_buffer_[i + 1] == 0x01 &&
        this->uart_buffer_[i + 2] == 0x07 && i + 5 < this->uart_counter_ && this->uart_buffer_[i + 3] == 0x02 &&
        this->uart_buffer_[i + 4] == 0x02 && this->uart_buffer_[i + 5] == 0x16) {
      ESP_LOGD(TAG, "A1: Reactive power+ (1.0.3.7.0.255): 0 VAr [empty compressed record]");
      this->frame_reactive_seen_ = true;
      i += 5;
      continue;
    }

    // Voltage alternate pattern: 23:02:01:XX:07:...
    if (this->uart_buffer_[i] == 0x23 && i + 4 < this->uart_counter_ && this->uart_buffer_[i + 1] == 0x02 &&
        this->uart_buffer_[i + 2] == 0x01 && this->uart_buffer_[i + 4] == 0x07) {
      uint8_t obis_type = this->uart_buffer_[i + 3];
      uint16_t data_start = i + 5;
      uint16_t data_end = this->find_next_separator(data_start);

      if (data_end > data_start) {
        this->parse_a1_obis_value(obis_type, data_start, data_end);
      }

      if (data_end > i)
        i = data_end + 2;
      continue;
    }

    // Compressed voltage record: 23:02:01:07:<payload> (type byte omitted -> Voltage L3)
    if (this->uart_buffer_[i] == 0x23 && this->uart_buffer_[i + 1] == 0x02 && this->uart_buffer_[i + 2] == 0x01 &&
        this->uart_buffer_[i + 3] == 0x07) {
      uint16_t data_start = i + 4;
      uint16_t data_end = this->find_next_separator(data_start);

      if (data_end > data_start && data_end - data_start <= 8) {
        ESP_LOGD(TAG, "A1: Compressed voltage record at pos %u -> L3", i);
        this->parse_a1_obis_value(0x48, data_start, data_end);
      }

      if (data_end > i)
        i = data_end + 2;
    }
  }

  // Net reactive power (import positive, export negative), published once per
  // frame so import/export records don't overwrite each other
  if (this->frame_reactive_seen_ && this->reactive_power_sensor_ != nullptr) {
    ESP_LOGI(TAG, "A1: Net reactive power: %.0f var", this->frame_reactive_net_);
    this->reactive_power_sensor_->publish_state(this->frame_reactive_net_);
  }

  // Truncated power value: resolve now that this frame's currents are known
  if (this->frame_power_pending_) {
    uint32_t power = this->reconstruct_power(this->frame_power_byte_);
    ESP_LOGI(TAG, "A1: Active power+ (1.0.1.7.0.255): %u W [reconstructed from 0x%02X]", power,
             this->frame_power_byte_);
    if (this->power_sensor_ != nullptr)
      this->power_sensor_->publish_state(power);
    this->frame_power_pending_ = false;
  }
}

uint16_t MbusMeter::find_next_separator(uint16_t start_pos) {
  for (uint16_t i = start_pos; i + 2 < this->uart_counter_; i++) {
    // Standard separator: 02:02:16
    if (this->uart_buffer_[i] == 0x02 && this->uart_buffer_[i + 1] == 0x02 && this->uart_buffer_[i + 2] == 0x16) {
      return i;
    }
    // Energy section separator: 02:02:01:16
    if (i + 3 < this->uart_counter_ && this->uart_buffer_[i] == 0x02 && this->uart_buffer_[i + 1] == 0x02 &&
        this->uart_buffer_[i + 2] == 0x01 && this->uart_buffer_[i + 3] == 0x16) {
      return i;
    }
  }
  return this->uart_counter_;
}

void MbusMeter::parse_a1_obis_value(uint8_t obis_type, uint16_t data_start, uint16_t data_end) {
  if (data_end <= data_start)
    return;
  uint16_t data_length = data_end - data_start;

  switch (obis_type) {
    case 0x01:  // Active power+ (1.0.1.7.0.255)
      if (data_length >= 2) {
        uint32_t power = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        ESP_LOGI(TAG, "A1: Active power+ (1.0.1.7.0.255): %u W", power);
        this->last_power_w_ = power;
        this->last_power_sum_i_ = this->last_current_a_[0] + this->last_current_a_[1] + this->last_current_a_[2];
        this->frame_power_pending_ = false;  // full value beats a truncated one from the same frame
        if (this->power_sensor_ != nullptr)
          this->power_sensor_->publish_state(power);
      } else if (data_length == 1) {
        // Truncated single byte (high byte dropped by the meter); resolve at
        // the end of the frame once this frame's currents are parsed
        this->frame_power_byte_ = this->uart_buffer_[data_start];
        this->frame_power_pending_ = true;
      }
      break;

    case 0x02:  // Active power- (1.0.2.7.0.255)
      if (data_length >= 2) {
        uint32_t export_power = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        ESP_LOGI(TAG, "A1: Active power- (1.0.2.7.0.255): %u W", export_power);
      }
      break;

    case 0x03: {  // Reactive power+ (1.0.3.7.0.255)
      // Leading zero bytes are stripped, so 1-byte values occur (e.g. 0xBF = 191 VAr)
      uint8_t len = (data_length > 4) ? 4 : (uint8_t) data_length;
      uint32_t rp = this->extract_obis_value(data_start, len);
      ESP_LOGI(TAG, "A1: Reactive power+ (1.0.3.7.0.255): %u VAr [%u byte(s)]", rp, len);
      this->frame_reactive_net_ += (float) rp;
      this->frame_reactive_seen_ = true;
      break;
    }

    case 0x04: {  // Reactive power- (1.0.4.7.0.255)
      uint8_t len = (data_length > 4) ? 4 : (uint8_t) data_length;
      uint32_t rp_export = this->extract_obis_value(data_start, len);
      float sum_i = this->last_current_a_[0] + this->last_current_a_[1] + this->last_current_a_[2];
      if (len >= 2) {
        this->last_reactive_export_ = rp_export;
        this->last_reactive_sum_i_ = sum_i;
      } else if (rp_export == (this->last_reactive_export_ >> 8) &&
                 fabsf(sum_i - this->last_reactive_sum_i_) < 1.0f) {
        // Truncated repeat: the byte is the high byte of the last full value
        // and the load is unchanged - keep the last full value
        ESP_LOGD(TAG, "A1: Reactive power- truncated 0x%02X, keeping last full value %u VAr",
                 (unsigned) rp_export, this->last_reactive_export_);
        rp_export = this->last_reactive_export_;
      }
      ESP_LOGI(TAG, "A1: Reactive power- (1.0.4.7.0.255): %u VAr [%u byte(s)]", rp_export, len);
      this->frame_reactive_net_ -= (float) rp_export;
      this->frame_reactive_seen_ = true;
      break;
    }

    case 0x1F:  // Current L1 (1.0.31.7.0.255)
    case 0x33:  // Current L2 (1.0.51.7.0.255)
    case 0x47:  // Current L3 (1.0.71.7.0.255)
      if (this->uart_buffer_[data_start] == 0x10) {
        // 0x10 long-signed tag, then 0-2 value bytes at 0.1A resolution with
        // leading zero bytes stripped. An empty value (tag only) is sent even
        // under load, so it means "no reading this frame" - keep last state.
        uint16_t value_len = data_length - 1;
        uint8_t phase = (obis_type == 0x1F) ? 1 : (obis_type == 0x33) ? 2 : 3;
        if (value_len == 0) {
          ESP_LOGD(TAG, "A1: Current L%d: empty value - skipped", phase);
          break;
        }
        uint16_t acc = 0;
        for (uint16_t j = 0; j < value_len && j < 2; j++)
          acc = (acc << 8) | this->uart_buffer_[data_start + 1 + j];
        int32_t raw = (value_len >= 2) ? (int16_t) acc : (int32_t) acc;
        float current_a = fabsf(raw / 10.0f);

        const char *obis_codes[] = {"1.0.31.7.0.255", "1.0.51.7.0.255", "1.0.71.7.0.255"};
        ESP_LOGI(TAG, "A1: Current L%d (%s): %.1f A [raw: %d, %u byte(s)]", phase, obis_codes[phase - 1], current_a,
                 raw, value_len);

        this->last_current_a_[phase - 1] = current_a;
        sensor::Sensor *sensors[] = {this->current_l1_sensor_, this->current_l2_sensor_, this->current_l3_sensor_};
        if (sensors[phase - 1] != nullptr)
          sensors[phase - 1]->publish_state(current_a);
      } else {
        ESP_LOGD(TAG, "A1: Current record without 0x10 tag (type 0x%02X, %u bytes) - skipped", obis_type, data_length);
      }
      break;

    case 0x20:    // Voltage L1 (1.0.32.7.0.255)
    case 0x34:    // Voltage L2 (1.0.52.7.0.255)
    case 0x48: {  // Voltage L3 (1.0.72.7.0.255)
      uint8_t phase = (obis_type == 0x20) ? 1 : (obis_type == 0x34) ? 2 : 3;
      uint16_t voltage_raw;
      if (data_length >= 2) {
        voltage_raw = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
      } else {
        // The meter often truncates the high byte (e.g. 0x26 for 08:26 = 208.6V).
        // Adjacent high-byte candidates are 25.6V apart while grid voltage moves
        // slowly, so reconstruct with the candidate closest to the last known value.
        uint8_t low = this->uart_buffer_[data_start];
        uint16_t best = 0;
        int32_t best_diff = 0x7FFFFFFF;
        for (uint16_t high = 0x07; high <= 0x09; high++) {
          uint16_t cand = (high << 8) | low;
          int32_t diff = (int32_t) cand - (int32_t) this->last_voltage_raw_[phase - 1];
          if (diff < 0)
            diff = -diff;
          if (diff < best_diff) {
            best_diff = diff;
            best = cand;
          }
        }
        voltage_raw = best;
        ESP_LOGD(TAG, "A1: Voltage L%d reconstructed from truncated 0x%02X -> %u", phase, low, voltage_raw);
      }
      float voltage_v = voltage_raw / 10.0f;

      if (voltage_v < 100.0f || voltage_v > 300.0f) {
        ESP_LOGW(TAG, "A1: Voltage L%d out of range: %.1f V", phase, voltage_v);
        break;
      }
      this->last_voltage_raw_[phase - 1] = voltage_raw;

      const char *obis_codes[] = {"1.0.32.7.0.255", "1.0.52.7.0.255", "1.0.72.7.0.255"};
      ESP_LOGI(TAG, "A1: Voltage L%d (%s): %.1f V", phase, obis_codes[phase - 1], voltage_v);

      sensor::Sensor *sensors[] = {this->voltage_l1_sensor_, this->voltage_l2_sensor_, this->voltage_l3_sensor_};
      if (sensors[phase - 1] != nullptr)
        sensors[phase - 1]->publish_state(voltage_v);
      break;
    }

    default:
      ESP_LOGD(TAG, "A1: Unknown OBIS type 0x%02X (%d bytes)", obis_type, data_length);
      break;
  }
}

}  // namespace mbus_meter
}  // namespace esphome
