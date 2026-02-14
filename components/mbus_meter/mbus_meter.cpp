#include "mbus_meter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mbus_meter {

static const char *const TAG = "mbus_meter";

void MbusMeter::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Norwegian HAN M-Bus Meter...");
  this->uart_counter_ = 0;
  this->last_frame_time_ = 0;
  ESP_LOGI(TAG, "M-Bus Meter component initialized");
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
  memset(this->uart_buffer_, 0, sizeof(this->uart_buffer_));
}

bool MbusMeter::is_valid_frame_start(uint16_t position) {
  if (position + 2 >= this->uart_counter_) return false;
  
  // Check for Norwegian HAN frame start patterns
  return ((this->uart_buffer_[position] == 0x2A || this->uart_buffer_[position] == 0xA1) &&
          this->uart_buffer_[position + 1] == 0x08 &&
          this->uart_buffer_[position + 2] == 0x83);
}

bool MbusMeter::read_message() {
  uint32_t now = millis();
  
  // Check for frame timeout
  if (this->uart_counter_ > 0 && now - this->last_frame_time_ > FRAME_TIMEOUT_MS) {
    // Frame timeout - process if we have enough data
    if (this->uart_counter_ > 15) {
      // For A1 frames, only process if we have substantial data (at least 100 bytes)
      if (this->uart_buffer_[0] == 0xA1 && this->uart_counter_ < 100) {
        ESP_LOGD(TAG, "A1 frame timeout: discarding %d bytes (too short)", this->uart_counter_);
      }
      // For 2A frames, need at least 18 bytes for valid frame
      else if (this->uart_buffer_[0] == 0x2A && this->uart_counter_ < 18) {
        ESP_LOGD(TAG, "2A frame timeout: discarding %d bytes (too short)", this->uart_counter_);
      }
      else {
        ESP_LOGD(TAG, "Frame timeout - processing buffer of %d bytes", this->uart_counter_);
        this->process_current_frame();
      }
    } else {
      ESP_LOGV(TAG, "Frame timeout: discarding %d bytes (insufficient data)", this->uart_counter_);
    }
    this->reset_buffer();
    return false;
  }
  
  // Read available bytes
  while (this->available() > 0 && this->uart_counter_ < sizeof(this->uart_buffer_)) {
    uint8_t byte;
    this->read_byte(&byte);
    this->last_frame_time_ = now;
    
    this->uart_buffer_[this->uart_counter_] = byte;
    this->uart_counter_++;
    
    // Check for complete frame
    if (this->uart_counter_ >= 20 && this->is_valid_frame_start(0)) {
      // For A1 frames, wait for more data as they can be much longer (up to 200+ bytes)
      if (this->uart_buffer_[0] == 0xA1) {
        if (this->uart_counter_ >= 150) {
          ESP_LOGD(TAG, "Processing A1 frame of %d bytes", this->uart_counter_);
          this->process_current_frame();
          this->reset_buffer();
          return true;
        }
        // For A1 frames, don't process until we have at least 150 bytes - just continue reading
      }
      // For other frames (2A, etc), process when we have substantial data
      else if (this->uart_buffer_[0] != 0xA1 && this->uart_counter_ >= 50) {
        // Processing frame
        this->process_current_frame();
        this->reset_buffer();
        return true;
      }
    }
    
    // Prevent buffer overflow
    if (this->uart_counter_ >= sizeof(this->uart_buffer_) - 1) {
      ESP_LOGW(TAG, "Buffer overflow - processing and resetting");
      this->process_current_frame();
      this->reset_buffer();
      return false;
    }
  }
  
  return false;
}

void MbusMeter::process_current_frame() {
  if (this->uart_counter_ < 10) {
    // Frame too short
    return;
  }
  
  // Processing Norwegian HAN frame
  
  // Check if this is a 2A listing frame (starts with 2A and contains 01:01:07)
  // 2A frames are short (typically 16-20 bytes) and contain real-time power data
  // Pattern: 2A:08:83:13:04:13:E6:40:01:01:02:01:01:07:[POWER]:02:02:16...
  // KNOWN METER BUG: Sometimes sends 2 digits instead of 4 for high power values
  if (this->uart_buffer_[0] == 0x2A) {
    uint32_t power_value = this->search_for_real_time_power();
    if (power_value > 0) {
      ESP_LOGI(TAG, "2A frame: Power: %u W", power_value);
      
      // If using separate 2A frame sensor, publish to it
      if (this->use_2a_frame_own_sensor_ && this->power_2a_frame_sensor_ != nullptr) {
        this->power_2a_frame_sensor_->publish_state(power_value);
      } 
      // Otherwise, publish to main power sensor
      else if (!this->use_2a_frame_own_sensor_ && this->power_sensor_ != nullptr) {
        this->power_sensor_->publish_state(power_value);
      }
    } else {
      ESP_LOGD(TAG, "2A frame: No valid power reading found (possible meter bug)");
    }
    return;  // 2A frames only contain power data - skip all other processing
  }
  
  // Check if this is an A1 frame (starts with A1 and contains multiple OBIS values)
  // A1 frames are sent every 10 seconds and contain comprehensive meter data
  // Values are separated by 02:02:16 pattern
  if (this->uart_buffer_[0] == 0xA1) {
    ESP_LOGI(TAG, "A1 frame detected, length: %d bytes", this->uart_counter_);
    this->parse_a1_frame();
    return;  // A1 frames have their own parsing logic
  }
  
  // Process longer frames (>25 bytes) for text sensors and other OBIS data
  // Frame header check removed for cleaner logs
  
  // Look for Norwegian HAN OBIS patterns in longer frames
  for (uint16_t i = 0; i < this->uart_counter_ - 5; i++) {
    // Look for OBIS data pattern: 02 02 01
    if (this->uart_buffer_[i] == 0x02 && 
        this->uart_buffer_[i + 1] == 0x02 && 
        this->uart_buffer_[i + 2] == 0x01) {
      // Found HAN OBIS pattern
      this->parse_han_obis(i);
    }
    // Also look for the alternative pattern: 02 02 16 (extended OBIS format)
    else if (i < this->uart_counter_ - 7 &&
             this->uart_buffer_[i] == 0x02 && 
             this->uart_buffer_[i + 1] == 0x02 && 
             this->uart_buffer_[i + 2] == 0x16) {
      // Found extended OBIS pattern
      this->parse_extended_obis(i);
    }
  }
}

void MbusMeter::parse_extended_obis(uint16_t position) {
  // Parse extended OBIS format (02 02 16) which may contain voltage/current data
  if (position + 10 >= this->uart_counter_) {
    ESP_LOGW(TAG, "Not enough data for extended OBIS parsing at position %d", position);
    return;
  }
  
  // Parsing extended OBIS
  
  // Extended OBIS format pattern: 02 02 16 [DATA_TYPE] [DATA_LENGTH] [DATA...]
  // This might contain voltage and current measurements
  
  if (position + 5 < this->uart_counter_) {
    uint8_t data_type = this->uart_buffer_[position + 3];
    uint8_t data_length = this->uart_buffer_[position + 4];
    
    // Extended OBIS data
    
    // Check if this looks like sensor data (voltage/current typically 2 bytes)
    if (data_length == 2 && position + 7 < this->uart_counter_) {
      uint16_t raw_value = (this->uart_buffer_[position + 5] << 8) | this->uart_buffer_[position + 6];
      // Extended OBIS raw value
      
      // Static counter to distribute current readings across phases
      static uint8_t current_phase = 1;
      
      // Try to identify if this is voltage or current based on reasonable ranges
      if (raw_value >= 2000 && raw_value <= 2500) {
        // Likely voltage (200-250V with 0.1V resolution)
        float voltage = raw_value / 10.0f;
        ESP_LOGI(TAG, "Extended OBIS voltage: %.1f V", voltage);
        
        // Publish to first available voltage sensor
        if (this->voltage_l1_sensor_ != nullptr) {
          this->voltage_l1_sensor_->publish_state(voltage);
        }
      } else if (raw_value >= 100 && raw_value <= 5000) {
        // Current values detected but skipping Extended OBIS current parsing
        // A1 frames provide more accurate current readings
        ESP_LOGD(TAG, "Extended OBIS: Skipping current reading %u (using A1 frame data instead)", raw_value);
        
        // Still cycle through phases for other potential readings
        current_phase++;
        if (current_phase > 3) current_phase = 1;
      } else {
        ESP_LOGD(TAG, "Extended OBIS: Unknown value type %u (0x%04X)", raw_value, raw_value);
      }
    }
  }
}

void MbusMeter::parse_han_obis(uint16_t position) {
  if (position + 10 >= this->uart_counter_) {
    ESP_LOGW(TAG, "Not enough data for OBIS parsing at position %d", position);
    return;
  }
  
  // OBIS data structure check removed for cleaner logs
  
  // Norwegian HAN OBIS structure analysis from logs:
  // Pattern: 02 02 01 [OBIS_TYPE] [LENGTH] [DATA...]
  // Based on actual frame data from logs
  
  if (position + 4 >= this->uart_counter_) return;
  
  uint8_t obis_type = this->uart_buffer_[position + 3];
  uint8_t data_length = this->uart_buffer_[position + 4];
  
  // HAN OBIS Type and Length check
  
  // Handle different OBIS types based on Norwegian HAN specification
  if (obis_type == 0x01) {
    // OBIS List version identifier (1.1.0.2.129.255) - visible-string
    if (data_length == 0x02 && position + 6 < this->uart_counter_) {
      // Check if there's a length prefix (0x0B = 11 bytes)
      if (this->uart_buffer_[position + 5] == 0x0B) {
        // Skip the length byte and parse the actual text
        this->parse_text_value(position + 6, this->obis_version_text_sensor_);
      } else {
        this->parse_text_value(position + 5, this->obis_version_text_sensor_);
      }
    }
  }
  else if (obis_type == 0x07) {
    // Active power+ (1.0.1.7.0.255) - double-long-unsigned, kW resolution W
    if (data_length == 0x04 && position + 8 < this->uart_counter_) {
      this->parse_power_value(position + 5);
    }
  }
  else if (obis_type == 0x10) {
    // Meter ID (0.0.96.1.0.255) - visible-string, 16 digits
    // But length 55 seems wrong - limit to reasonable length
    uint8_t safe_length = (data_length > 20) ? 20 : data_length;
    if (position + 5 + safe_length < this->uart_counter_) {
      // Parsing Meter ID with safe length
      this->parse_text_value(position + 5, this->meter_id_text_sensor_);
    }
  }
  else if (obis_type == 0x1F) {
    // Current L1 (1.0.31.7.0.255) - long-signed, 0.1 A resolution
    if (data_length >= 0x02 && position + 7 < this->uart_counter_) {
      this->parse_current_value(position + 5, 1);
    }
  }
  else if (obis_type == 0x33) {
    // Current L2 (1.0.51.7.0.255) - long-signed, 0.1 A resolution
    if (data_length >= 0x02 && position + 7 < this->uart_counter_) {
      this->parse_current_value(position + 5, 2);
    }
  }
  else if (obis_type == 0x47) {
    // Current L3 (1.0.71.7.0.255) - long-signed, 0.1 A resolution
    if (data_length >= 0x02 && position + 7 < this->uart_counter_) {
      this->parse_current_value(position + 5, 3);
    }
  }
  else if (obis_type == 0x20) {
    // Voltage L1 (1.0.32.7.0.255) - long-unsigned, 0.1 V resolution
    if (data_length >= 0x02 && position + 7 < this->uart_counter_) {
      this->parse_voltage_value(position + 5, 1);
    }
  }
  else if (obis_type == 0x34) {
    // Voltage L2 (1.0.52.7.0.255) - long-unsigned, 0.1 V resolution
    if (data_length >= 0x02 && position + 7 < this->uart_counter_) {
      this->parse_voltage_value(position + 5, 2);
    }
  }
  else if (obis_type == 0x48) {
    // Voltage L3 (1.0.72.7.0.255) - long-unsigned, 0.1 V resolution
    if (data_length >= 0x02 && position + 7 < this->uart_counter_) {
      this->parse_voltage_value(position + 5, 3);
    }
  }
  else if (obis_type == 0x08) {
    // Check if this is energy or something else based on context
    if (data_length == 0x04 && position + 9 < this->uart_counter_) {
      // Could be Active energy (1.0.1.8.0.255) - double-long-unsigned
      this->parse_energy_value(position + 5);
    }
  }
  else if (obis_type == 0x02) {
    // Active power- (1.0.2.7.0.255) - double-long-unsigned, export direction
    if (data_length == 0x04 && position + 9 < this->uart_counter_) {
      // Parse export power but don't publish to main power sensor
      uint32_t export_power = this->extract_obis_value(position + 5, 4);
      ESP_LOGI(TAG, "Active power- export (1.0.2.7.0.255): %u W", export_power);
    }
  }
  else if (obis_type == 0x03) {
    // Reactive power+ (1.0.3.7.0.255) - double-long-unsigned, import direction
    if (data_length == 0x04 && position + 9 < this->uart_counter_) {
      uint32_t reactive_power = this->extract_obis_value(position + 5, 4);
      ESP_LOGI(TAG, "Reactive power+ import (1.0.3.7.0.255): %u VAr", reactive_power);
      if (this->reactive_power_sensor_ != nullptr) {
        this->reactive_power_sensor_->publish_state(reactive_power);
      }
    }
  }
  else if (obis_type == 0x04) {
    // Reactive power- (1.0.4.7.0.255) - double-long-unsigned, export direction
    if (data_length == 0x04 && position + 9 < this->uart_counter_) {
      uint32_t reactive_power_export = this->extract_obis_value(position + 5, 4);
      ESP_LOGI(TAG, "Reactive power- export (1.0.4.7.0.255): %u VAr", reactive_power_export);
    }
  }
  else {
    // Unknown HAN OBIS type
  }
}

void MbusMeter::parse_power_value(uint16_t position) {
  // Skip old power parsing - use only 2A listing power search
  // The old static values (34 02 01 01) are meter constants, not real-time power
  
  uint32_t power_value = this->search_for_real_time_power();
  
  if (power_value > 0) {
    ESP_LOGI(TAG, "Active power+ (1.0.1.7.0.255): %u W", power_value);
    // Only publish to main power sensor if NOT using separate 2A frame sensor
    if (!this->use_2a_frame_own_sensor_ && this->power_sensor_ != nullptr) {
      this->power_sensor_->publish_state(power_value);
    }
  } else {
    ESP_LOGD(TAG, "2A frame: No valid power data found");
  }
}

void MbusMeter::parse_current_value(uint16_t position, uint8_t phase) {
  if (position + 1 >= this->uart_counter_) return;
  
  // Norwegian HAN spec: Current phase - long-signed, 0.1 A resolution
  // Format 3.1 (xxx.x A) - 0.5 second RMS current
  // Data type: long-signed (16-bit signed)
  
  // Extract as signed 16-bit value
  int16_t raw_current = (this->uart_buffer_[position] << 8) | this->uart_buffer_[position + 1];
  
  // User feedback: Current values are too high (30.8A, 26.3A) for 20A service
  // Try different scaling factors to get reasonable values
  float current_a_div10 = raw_current / 10.0f;    // Standard 0.1A resolution
  float current_a_div100 = raw_current / 100.0f;  // Alternative 0.01A resolution
  
  // Current value analysis
  
  // Choose appropriate scaling based on reasonable current values (max 20A service)
  float current_a;
  if (fabs(current_a_div100) <= 20.0f && fabs(current_a_div100) >= 0.1f) {
    current_a = current_a_div100;  // Use /100 scaling if it gives reasonable values
    // Using /100 scaling
  } else if (fabs(current_a_div10) <= 20.0f && fabs(current_a_div10) >= 0.1f) {
    current_a = current_a_div10;   // Use /10 scaling if it gives reasonable values
    // Using /10 scaling
  } else {
    // If neither scaling gives reasonable values, use /100 as default
    current_a = current_a_div100;
    ESP_LOGW(TAG, "Current L%d value seems out of range even after scaling: %.2f A", phase, current_a);
  }
  
  // Handle negative values properly (current can be negative in some cases)
  if (current_a < 0) {
    // Negative current detected
    current_a = fabs(current_a);  // Take absolute value for display
  }
  
  // Log with correct OBIS code format
  const char* obis_codes[] = {"1.0.31.7.0.255", "1.0.51.7.0.255", "1.0.71.7.0.255"};
  ESP_LOGI(TAG, "Current L%d (%s): %.2f A", phase, obis_codes[phase-1], current_a);
  
  switch (phase) {
    case 1:
      if (this->current_l1_sensor_ != nullptr) {
        this->current_l1_sensor_->publish_state(current_a);
      }
      break;
    case 2:
      if (this->current_l2_sensor_ != nullptr) {
        this->current_l2_sensor_->publish_state(current_a);
      }
      break;
    case 3:
      if (this->current_l3_sensor_ != nullptr) {
        this->current_l3_sensor_->publish_state(current_a);
      }
      break;
  }
}

void MbusMeter::parse_voltage_value(uint16_t position, uint8_t phase) {
  if (position + 1 >= this->uart_counter_) return;
  
  // Norwegian HAN spec: Voltage phase - long-unsigned, 0.1 V resolution
  // Format 3.1 (xxx.x V) - 0.5 second RMS voltage
  // Data type: long-unsigned (16-bit unsigned)
  // 4W meter: Phase voltage, 3W meter: Line voltage
  
  uint16_t raw_voltage = (this->uart_buffer_[position] << 8) | this->uart_buffer_[position + 1];
  float voltage_v = raw_voltage / 10.0f;  // Resolution 0.1 V
  
  // Sanity check for voltage values (typically 200-250V for Norwegian households)
  if (voltage_v < 100.0f || voltage_v > 300.0f) {
    ESP_LOGW(TAG, "Voltage value seems out of range: %.1f V", voltage_v);
  }
  
  // Log with correct OBIS code format
  const char* obis_codes[] = {"1.0.32.7.0.255", "1.0.52.7.0.255", "1.0.72.7.0.255"};
  ESP_LOGI(TAG, "Voltage L%d (%s): %.1f V", phase, obis_codes[phase-1], voltage_v);
  
  switch (phase) {
    case 1:
      if (this->voltage_l1_sensor_ != nullptr) {
        this->voltage_l1_sensor_->publish_state(voltage_v);
      }
      break;
    case 2:
      if (this->voltage_l2_sensor_ != nullptr) {
        this->voltage_l2_sensor_->publish_state(voltage_v);
      }
      break;
    case 3:
      if (this->voltage_l3_sensor_ != nullptr) {
        this->voltage_l3_sensor_->publish_state(voltage_v);
      }
      break;
  }
}

void MbusMeter::parse_energy_value(uint16_t position) {
  if (position + 3 >= this->uart_counter_) return;
  
  // Norwegian HAN spec: Cumulative hourly active import energy (A+)
  // OBIS: 1.0.1.8.0.255 - double-long-unsigned
  // Unit: kWh/Wh (depending on meter config)
  // Resolution: 10 Wh or 0.01 Wh
  // Format: 7.2 (xxxxxxx.xx kWh/Wh)
  
  uint32_t energy_raw = (this->uart_buffer_[position] << 24) |
                       (this->uart_buffer_[position + 1] << 16) |
                       (this->uart_buffer_[position + 2] << 8) |
                       this->uart_buffer_[position + 3];
  
  // The energy value depends on meter configuration
  // Most Norwegian meters use 10 Wh resolution
  // Some use 0.01 Wh resolution
  
  // Check if value seems to be in 10 Wh units (typical for Norwegian meters)
  uint32_t energy_wh;
  if (energy_raw < 1000000) {
    // Likely 10 Wh resolution
    energy_wh = energy_raw * 10;
    // Energy interpreted as 10 Wh resolution
  } else {
    // Likely already in Wh (0.01 Wh resolution)
    energy_wh = energy_raw;
    // Energy interpreted as 1 Wh resolution
  }
  
  ESP_LOGI(TAG, "Active import energy (1.0.1.8.0.255): %u Wh", energy_wh);
  
  if (this->energy_sensor_ != nullptr) {
    this->energy_sensor_->publish_state(energy_wh);
  }
}

void MbusMeter::parse_text_value(uint16_t position, text_sensor::TextSensor* sensor) {
  if (sensor == nullptr || position >= this->uart_counter_) return;
  
  // Norwegian HAN spec uses visible-string data type
  // OBIS List version: should be "AIDON_V0001" or similar
  // Meter ID: 16 digits (GIAI GS1 format)
  // Meter type: "6515", "6525", "6534", "6540", "6550"
  
  std::string text_value;
  
  // Norwegian HAN text values are typically not length-prefixed
  // Look for direct ASCII text in the data
  uint16_t start_pos = position;
  
  // Parsing text from position
  
  // Text data bytes check removed for cleaner logs
  
  // Extract text characters
  uint16_t max_chars = 20;  // Reasonable limit for text
  for (uint16_t i = 0; i < max_chars && (start_pos + i) < this->uart_counter_; i++) {
    uint8_t byte = this->uart_buffer_[start_pos + i];
    
    // Text byte processing
    
    if (byte >= 32 && byte <= 126) { // Printable ASCII
      text_value += (char)byte;
    } else if (byte == 0x00) {
      break; // Null terminator
    } else if (byte < 32) {
      // Non-printable control character, stop here
      break;
    } else {
      // Extended ASCII, might be valid text
      text_value += (char)byte;
    }
  }
  
  if (!text_value.empty()) {
    ESP_LOGI(TAG, "Text value: '%s'", text_value.c_str());
    sensor->publish_state(text_value);
  } else {
    // No valid text found
  }
}

uint32_t MbusMeter::extract_obis_value(uint16_t position, uint8_t length) {
  uint32_t value = 0;
  
  if (position + length > this->uart_counter_) {
    ESP_LOGW(TAG, "Not enough data for value extraction at pos %d, need %d bytes", position, length);
    return 0;
  }
  
  // Extracting value
  
  // Extract value based on length (big-endian format)
  for (uint8_t i = 0; i < length && i < 4; i++) {
    value = (value << 8) | this->uart_buffer_[position + i];
  }
  
  // Value extracted
  return value;
}

uint32_t MbusMeter::search_for_real_time_power() {
  // Fast and simple 2A listing power search
  // Pattern: 01:01:07:[POWER_BYTES]:02:02
  // The meter has a bug where it sometimes sends 2 digits instead of 4
  // Known issue: 0x29 (41) actually means 10000W when meter truncates

  for (uint16_t i = 0; i < this->uart_counter_ - 6; i++) {
    if (this->uart_buffer_[i] == 0x01 && 
        this->uart_buffer_[i + 1] == 0x01 && 
        this->uart_buffer_[i + 2] == 0x07) {
      
      // Check for two-byte power first: 01:01:07:XX:YY:02:02:16
      if (i + 7 < this->uart_counter_ &&
          this->uart_buffer_[i + 5] == 0x02 &&
          this->uart_buffer_[i + 6] == 0x02 &&
          this->uart_buffer_[i + 7] == 0x16) {
        uint32_t power = (this->uart_buffer_[i + 3] << 8) | this->uart_buffer_[i + 4];
        ESP_LOGI(TAG, "2A frame: Power: %u W (two-byte: %02X:%02X)", power,
                 this->uart_buffer_[i + 3], this->uart_buffer_[i + 4]);
        return power;
      }
      
      // Check for single-byte power: 01:01:07:XX:02:02:16
      if (i + 6 < this->uart_counter_ &&
          this->uart_buffer_[i + 4] == 0x02 &&
          this->uart_buffer_[i + 5] == 0x02 &&
          this->uart_buffer_[i + 6] == 0x16) {
        uint32_t power = this->uart_buffer_[i + 3];
        
        // Handle meter bug: Sometimes high power values are truncated
        // Extended exclusion range for values under 50W
        if (power < 50) {
          // Log the suspicious value
          ESP_LOGW(TAG, "2A frame: Suspicious low power value %02X (%u W) - possible meter bug", 
                   this->uart_buffer_[i + 3], power);
          
          // Known problematic values that represent high power
          // 0x29 (41) has been observed to mean 10000W
          if (power == 0x29) {
            power = 10000;
            ESP_LOGI(TAG, "2A frame: Applied known correction: 0x29 -> 10000W");
          }
          // Add more mappings as discovered
          else if (power >= 0x20 && power <= 0x2F) {
            // Range 0x20-0x2F (32-47) might be truncated high values
            // Without history, we can't determine exact value
            ESP_LOGW(TAG, "2A frame: Value in suspicious range 0x20-0x2F, may be truncated high power");
            continue;  // Skip this reading
          }
          else {
            // Very low values (< 32W) are likely errors
            ESP_LOGD(TAG, "2A frame: Ignoring invalid power value %02X (%u W)", power, power);
            continue;
          }
        }
        
        ESP_LOGI(TAG, "2A frame: Power: %u W (single-byte: %02X)", power, this->uart_buffer_[i + 3]);
        return power;
      }
    }
  }
  
  return 0;
}

void MbusMeter::parse_a1_frame() {
  // A1 frame structure:
  // A1:08:83:13:E6:40:01:0D:[header]
  // Then OBIS entries separated by 02:02:16
  // Each OBIS entry: 02:01:[OBIS_TYPE]:07:[VALUE_BYTES]
  // Sometimes no value, directly to next 02:02:16
  
  ESP_LOGD(TAG, "A1 frame hex dump (length: %d):", this->uart_counter_);
  for (uint16_t i = 0; i < this->uart_counter_ && i < 300; i += 16) {
    std::string line = "";
    for (uint16_t j = i; j < i + 16 && j < this->uart_counter_; j++) {
      char hex[4];
      sprintf(hex, "%02X:", this->uart_buffer_[j]);
      line += hex;
    }
    ESP_LOGD(TAG, "  %04X: %s", i, line.c_str());
  }
  
  uint16_t pos = 0;
  
  // Skip A1 frame header (typically A1:08:83:13:E6:40:01:0D)
  while (pos < this->uart_counter_ && pos < 20) {
    if (this->uart_buffer_[pos] == 0x02 && 
        pos + 1 < this->uart_counter_ &&
        (this->uart_buffer_[pos + 1] == 0x02 || this->uart_buffer_[pos + 1] == 0x01)) {
      break;
    }
    pos++;
  }
  
  // Process the complete A1 frame by searching for all OBIS patterns
  ESP_LOGD(TAG, "A1 frame: Scanning %d bytes for OBIS patterns", this->uart_counter_);
  
  // First, search for energy counter patterns (extended A1 frames)
  // Pattern: 02:01:01:08:XX = Active energy import
  // Pattern: 02:01:02:08:XX = Active energy export  
  // Pattern: 02:01:XX:08:XX = Generic energy/reactive energy patterns
  for (uint16_t i = 15; i < this->uart_counter_ - 6; i++) {
    if (this->uart_buffer_[i] == 0x02 && 
        i + 4 < this->uart_counter_ &&
        this->uart_buffer_[i + 1] == 0x01 &&
        this->uart_buffer_[i + 3] == 0x08) {
      
      uint8_t energy_type = this->uart_buffer_[i + 2];
      uint8_t energy_value = this->uart_buffer_[i + 4];
      
      ESP_LOGD(TAG, "A1 frame: Found energy pattern 02:01:%02X:08:%02X at pos %d", 
               energy_type, energy_value, i);
      
      // Energy counters: Resolution 10 Wh/VArh, Format 7.2 (xxxxxxx.xx kWh/kVArh)
      if (energy_type == 0x01) {
        // Active energy import (1.0.1.8.0.255)
        uint32_t energy_wh = energy_value * 10;
        float energy_kwh = energy_wh / 1000.0f;
        ESP_LOGI(TAG, "A1 frame: Active energy import (1.0.1.8.0.255): %u Wh (%.2f kWh) [raw: %02X]", 
                 energy_wh, energy_kwh, energy_value);
        if (this->energy_sensor_ != nullptr) {
          this->energy_sensor_->publish_state(energy_wh);
        }
      } else if (energy_type == 0x02) {
        // Active energy export (1.0.2.8.0.255)
        uint32_t export_wh = energy_value * 10;
        float export_kwh = export_wh / 1000.0f;
        ESP_LOGI(TAG, "A1 frame: Active energy export (1.0.2.8.0.255): %u Wh (%.2f kWh) [raw: %02X]", 
                 export_wh, export_kwh, energy_value);
      } else if (energy_type == 0x03) {
        // Reactive energy import (1.0.3.8.0.255)
        uint32_t reactive_varh = energy_value * 10;
        float reactive_kvarh = reactive_varh / 1000.0f;
        ESP_LOGI(TAG, "A1 frame: Reactive energy import (1.0.3.8.0.255): %u VArh (%.2f kVArh) [raw: %02X]", 
                 reactive_varh, reactive_kvarh, energy_value);
        if (this->reactive_energy_sensor_ != nullptr) {
          this->reactive_energy_sensor_->publish_state(reactive_varh);
        }
      } else if (energy_type == 0x04) {
        // Reactive energy export (1.0.4.8.0.255)
        uint32_t reactive_export_varh = energy_value * 10;
        float reactive_export_kvarh = reactive_export_varh / 1000.0f;
        ESP_LOGI(TAG, "A1 frame: Reactive energy export (1.0.4.8.0.255): %u VArh (%.2f kVArh) [raw: %02X]", 
                 reactive_export_varh, reactive_export_kvarh, energy_value);
        if (this->reactive_export_energy_sensor_ != nullptr) {
          this->reactive_export_energy_sensor_->publish_state(reactive_export_varh);
        }
      } else {
        // Other reactive energy patterns
        uint32_t reactive_varh = energy_value * 10;
        float reactive_kvarh = reactive_varh / 1000.0f;
        ESP_LOGI(TAG, "A1 frame: Reactive energy (1.0.%d.8.0.255): %u VArh (%.2f kVArh) [raw: %02X]", 
                 energy_type, reactive_varh, reactive_kvarh, energy_value);
        if (this->reactive_energy_sensor_ != nullptr) {
          this->reactive_energy_sensor_->publish_state(reactive_varh);
        }
      }
      
      // Skip past this pattern
      i += 4;
      continue;
    }
  }
  
  // Then search for standard OBIS patterns
  for (uint16_t i = 15; i < this->uart_counter_ - 6; i++) {
    
    // Look for standard OBIS pattern: 02:01:XX:07
    if (this->uart_buffer_[i] == 0x02 && 
        i + 3 < this->uart_counter_ &&
        this->uart_buffer_[i + 1] == 0x01 &&
        this->uart_buffer_[i + 3] == 0x07) {
      
      uint8_t obis_type = this->uart_buffer_[i + 2];
      uint16_t data_start = i + 4;
      
      // Find next separator 02:02:16
      uint16_t data_end = this->find_next_separator(data_start);
      
      ESP_LOGD(TAG, "A1 frame: Found OBIS type 0x%02X at pos %d, data from %d to %d", 
               obis_type, i, data_start, data_end);
      
      // Log the actual data bytes for debugging
      if (data_end > data_start && data_end - data_start <= 8) {
        std::string hex_data = "";
        for (uint16_t j = data_start; j < data_end; j++) {
          char hex[4];
          sprintf(hex, "%02X:", this->uart_buffer_[j]);
          hex_data += hex;
        }
        ESP_LOGD(TAG, "A1 frame: OBIS 0x%02X data: %s", obis_type, hex_data.c_str());
        
        // Parse the OBIS value
        this->parse_a1_obis_value(obis_type, data_start, data_end);
      } else if (data_end == data_start) {
        ESP_LOGD(TAG, "A1 frame: OBIS 0x%02X has no data (empty)", obis_type);
      }
      
      // Continue searching from after the separator
      if (data_end > i) {
        i = data_end + 2;  // Skip past 02:02:16
      }
    }
    // Look for voltage pattern: 23:02:01:XX:07
    else if (this->uart_buffer_[i] == 0x23 && 
             i + 4 < this->uart_counter_ &&
             this->uart_buffer_[i + 1] == 0x02 &&
             this->uart_buffer_[i + 2] == 0x01 &&
             this->uart_buffer_[i + 4] == 0x07) {
      
      uint8_t obis_type = this->uart_buffer_[i + 3];
      uint16_t data_start = i + 5;
      uint16_t data_end = this->find_next_separator(data_start);
      
      ESP_LOGD(TAG, "A1 frame: Found voltage OBIS type 0x%02X at pos %d", obis_type, i);
      
      if (data_end > data_start) {
        // Map voltage OBIS types to standard values
        uint8_t voltage_obis = 0x20;  // Default to L1
        if (obis_type == 0x34) voltage_obis = 0x34;  // L2
        else if (obis_type == 0x48) voltage_obis = 0x48;  // L3
        
        this->parse_a1_obis_value(voltage_obis, data_start, data_end);
      }
      
      // Continue searching
      if (data_end > i) {
        i = data_end + 2;
      }
    }
  }
}

uint16_t MbusMeter::find_next_separator(uint16_t start_pos) {
  // Find next occurrence of 02:02:16
  for (uint16_t i = start_pos; i < this->uart_counter_ - 2; i++) {
    if (this->uart_buffer_[i] == 0x02 &&
        this->uart_buffer_[i + 1] == 0x02 &&
        this->uart_buffer_[i + 2] == 0x16) {
      return i;
    }
  }
  return this->uart_counter_;
}

void MbusMeter::parse_a1_obis_value(uint8_t obis_type, uint16_t data_start, uint16_t data_end) {
  // Check if there's any data between start and separator
  if (data_end <= data_start) {
    ESP_LOGD(TAG, "A1 frame: No value for OBIS type 0x%02X", obis_type);
    return;
  }
  
  uint16_t data_length = data_end - data_start;
  
  // Norwegian HAN A1 format analysis:
  // Current L1: 02:01:1F:07:10:07:02:02:16 - value is 07 (0.7A), 10 is data type
  // Voltage L1: 02:01:20:07:08:EC:02:02:16 - value is 08:EC (228.4V)
  
  // Log raw data for analysis
  std::string hex_data = "";
  for (uint16_t j = data_start; j < data_end && j < data_start + 8; j++) {
    char hex[4];
    sprintf(hex, "%02X:", this->uart_buffer_[j]);
    hex_data += hex;
  }
  ESP_LOGD(TAG, "A1 frame: OBIS 0x%02X raw data: %s (length: %d)", obis_type, hex_data.c_str(), data_length);
  
  // Parse based on OBIS type according to Norwegian HAN specification
  switch (obis_type) {
    case 0x01:  // Active power+ (1.0.1.7.0.255)
      if (data_length >= 2) {
        uint32_t power = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        ESP_LOGI(TAG, "A1 frame: Active power+ (1.0.1.7.0.255): %u W", power);
        // A1 frame power always goes to main sensor
        if (this->power_sensor_ != nullptr) {
          this->power_sensor_->publish_state(power);
        }
      }
      break;
      
    case 0x02:  // Active power- (1.0.2.7.0.255)
      if (data_length >= 2) {
        uint32_t export_power = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        ESP_LOGI(TAG, "A1 frame: Active power- (1.0.2.7.0.255): %u W", export_power);
        // Note: Export power, not updating main power sensor
      }
      break;
      
    case 0x03:  // Reactive power+ (1.0.3.7.0.255)
      if (data_length >= 1) {
        uint32_t reactive_power = this->uart_buffer_[data_start];
        if (data_length >= 2) {
          reactive_power = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        }
        ESP_LOGI(TAG, "A1 frame: Reactive power+ (1.0.3.7.0.255): %u VAr", reactive_power);
        if (this->reactive_power_sensor_ != nullptr) {
          this->reactive_power_sensor_->publish_state(reactive_power);
        }
      }
      break;
      
    case 0x04:  // Reactive power- (1.0.4.7.0.255)
      // Check for reactive power pattern - could be single byte or two bytes
      if (data_length >= 1) {
        uint32_t reactive_export = this->uart_buffer_[data_start];
        if (data_length >= 2) {
          reactive_export = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        }
        ESP_LOGI(TAG, "A1 frame: Reactive power- (1.0.4.7.0.255): %u VAr (len: %d)", reactive_export, data_length);
        if (this->reactive_power_sensor_ != nullptr && reactive_export > 0) {
          this->reactive_power_sensor_->publish_state(reactive_export);
        }
      } else {
        ESP_LOGD(TAG, "A1 frame: Reactive power- has no value data");
      }
      break;
      
    case 0x1F:  // Current L1 (1.0.31.7.0.255)
      // Pattern: 02:01:1F:07:10:07:02:02:16 - value is 07 (0.7A), 10 is data type
      if (data_length >= 2) {
        // Skip data type byte (first byte), take actual value (second byte)
        uint8_t current_raw = this->uart_buffer_[data_start + 1];
        float current_a = current_raw / 10.0f;  // 0.1A resolution (07 = 0.7A)
        ESP_LOGI(TAG, "A1 frame: Current L1 (1.0.31.7.0.255): %.1f A (raw: %u)", current_a, current_raw);
        if (this->current_l1_sensor_ != nullptr) {
          this->current_l1_sensor_->publish_state(current_a);
        }
      }
      break;
      
    case 0x33:  // Current L2 (1.0.51.7.0.255)
      // Same pattern as L1: skip data type byte, take actual value
      if (data_length >= 2) {
        uint8_t current_raw = this->uart_buffer_[data_start + 1];
        float current_a = current_raw / 10.0f;  // 0.1A resolution
        ESP_LOGI(TAG, "A1 frame: Current L2 (1.0.51.7.0.255): %.1f A (raw: %u)", current_a, current_raw);
        if (this->current_l2_sensor_ != nullptr) {
          this->current_l2_sensor_->publish_state(current_a);
        }
      }
      break;
      
    case 0x47:  // Current L3 (1.0.71.7.0.255)
      // Same pattern as L1: skip data type byte, take actual value
      if (data_length >= 2) {
        uint8_t current_raw = this->uart_buffer_[data_start + 1];
        float current_a = current_raw / 10.0f;  // 0.1A resolution
        ESP_LOGI(TAG, "A1 frame: Current L3 (1.0.71.7.0.255): %.1f A (raw: %u)", current_a, current_raw);
        if (this->current_l3_sensor_ != nullptr) {
          this->current_l3_sensor_->publish_state(current_a);
        }
      }
      break;
      
    case 0x20:  // Voltage L1 (1.0.32.7.0.255)
      // Pattern: 02:01:20:07:08:EC:02:02:16 - value is 08:EC (228.4V)
      if (data_length >= 2) {
        // Take first two bytes directly as voltage value
        uint16_t voltage_raw = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        float voltage_v = voltage_raw / 10.0f;  // 0.1V resolution (08:EC = 2284 = 228.4V)
        ESP_LOGI(TAG, "A1 frame: Voltage L1 (1.0.32.7.0.255): %.1f V (raw: %u)", voltage_v, voltage_raw);
        if (this->voltage_l1_sensor_ != nullptr && voltage_v > 100.0f && voltage_v < 300.0f) {
          this->voltage_l1_sensor_->publish_state(voltage_v);
        }
      }
      break;
      
    case 0x34:  // Voltage L2 (1.0.52.7.0.255)
      // Same pattern as L1: take first two bytes directly
      if (data_length >= 2) {
        uint16_t voltage_raw = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        float voltage_v = voltage_raw / 10.0f;  // 0.1V resolution
        ESP_LOGI(TAG, "A1 frame: Voltage L2 (1.0.52.7.0.255): %.1f V (raw: %u)", voltage_v, voltage_raw);
        if (this->voltage_l2_sensor_ != nullptr && voltage_v > 100.0f && voltage_v < 300.0f) {
          this->voltage_l2_sensor_->publish_state(voltage_v);
        }
      }
      break;
      
    case 0x48:  // Voltage L3 (1.0.72.7.0.255)
      // Same pattern as L1: take first two bytes directly
      if (data_length >= 2) {
        uint16_t voltage_raw = (this->uart_buffer_[data_start] << 8) | this->uart_buffer_[data_start + 1];
        float voltage_v = voltage_raw / 10.0f;  // 0.1V resolution
        ESP_LOGI(TAG, "A1 frame: Voltage L3 (1.0.72.7.0.255): %.1f V (raw: %u)", voltage_v, voltage_raw);
        if (this->voltage_l3_sensor_ != nullptr && voltage_v > 100.0f && voltage_v < 300.0f) {
          this->voltage_l3_sensor_->publish_state(voltage_v);
        }
      }
      break;
      
    case 0x08:  // Energy counters pattern (1.0.X.8.0.255)
      // Extended A1 frames can contain multiple energy counter types
      // Pattern found: 02:01:01:08:2A = Active energy import (raw: 2A = 42)
      // Pattern found: 02:01:08:B6 = Reactive energy import (raw: B6 = 182)  
      // Resolution: 10 Wh for active, 10 VArh for reactive, Format 7.2 (xxxxxxx.xx kWh/kVArh)
      
      if (data_length >= 1) {
        // Single byte energy value in extended A1 frames
        uint32_t energy_raw = this->uart_buffer_[data_start];
        
        // Check for multi-byte pattern by looking at context
        if (data_start >= 4) {
          // Look back for OBIS pattern to determine energy type
          if (this->uart_buffer_[data_start - 4] == 0x01 && this->uart_buffer_[data_start - 3] == 0x01) {
            // Active energy import (1.0.1.8.0.255): resolution 10 Wh, format 7.2 (xxxxxxx.xx kWh)
            uint32_t energy_wh = energy_raw * 10;
            float energy_kwh = energy_wh / 1000.0f;  // Convert to kWh for display
            ESP_LOGI(TAG, "A1 frame: Active energy import (1.0.1.8.0.255): %u Wh (%.2f kWh) [raw: %02X]", 
                     energy_wh, energy_kwh, energy_raw);
            if (this->energy_sensor_ != nullptr) {
              this->energy_sensor_->publish_state(energy_wh);
            }
          } else if (this->uart_buffer_[data_start - 4] == 0x01 && this->uart_buffer_[data_start - 3] == 0x02) {
            // Active energy export (1.0.2.8.0.255): resolution 10 Wh, format 7.2 (xxxxxxx.xx kWh)
            uint32_t export_wh = energy_raw * 10;
            float export_kwh = export_wh / 1000.0f;
            ESP_LOGI(TAG, "A1 frame: Active energy export (1.0.2.8.0.255): %u Wh (%.2f kWh) [raw: %02X]", 
                     export_wh, export_kwh, energy_raw);
          } else {
            // Generic reactive energy pattern
            uint32_t reactive_varh = energy_raw * 10;
            float reactive_kvarh = reactive_varh / 1000.0f;
            ESP_LOGI(TAG, "A1 frame: Reactive energy (1.0.X.8.0.255): %u VArh (%.2f kVArh) [raw: %02X]", 
                     reactive_varh, reactive_kvarh, energy_raw);
            if (this->reactive_energy_sensor_ != nullptr) {
              this->reactive_energy_sensor_->publish_state(reactive_varh);
            }
          }
        } else {
          // Fallback for unclear context
          uint32_t energy_wh = energy_raw * 10;
          ESP_LOGI(TAG, "A1 frame: Energy value: %u units (raw: %02X)", energy_wh, energy_raw);
        }
      }
      break;
      
    default:
      ESP_LOGD(TAG, "A1 frame: Unknown OBIS type 0x%02X with %d bytes", obis_type, data_length);
      break;
  }
}

}  // namespace mbus_meter
}  // namespace esphome