#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace mbus_meter {

class MbusMeter : public Component, public uart::UARTDevice {
 public:
  MbusMeter() : uart::UARTDevice() {}
  void set_power_sensor(sensor::Sensor *sensor) { power_sensor_ = sensor; }
  void set_current_l1_sensor(sensor::Sensor *sensor) { current_l1_sensor_ = sensor; }
  void set_current_l2_sensor(sensor::Sensor *sensor) { current_l2_sensor_ = sensor; }
  void set_current_l3_sensor(sensor::Sensor *sensor) { current_l3_sensor_ = sensor; }
  void set_voltage_l1_sensor(sensor::Sensor *sensor) { voltage_l1_sensor_ = sensor; }
  void set_voltage_l2_sensor(sensor::Sensor *sensor) { voltage_l2_sensor_ = sensor; }
  void set_voltage_l3_sensor(sensor::Sensor *sensor) { voltage_l3_sensor_ = sensor; }
  void set_energy_sensor(sensor::Sensor *sensor) { energy_sensor_ = sensor; }
  void set_reactive_power_sensor(sensor::Sensor *sensor) { reactive_power_sensor_ = sensor; }
  void set_reactive_energy_sensor(sensor::Sensor *sensor) { reactive_energy_sensor_ = sensor; }
  void set_reactive_export_energy_sensor(sensor::Sensor *sensor) { reactive_export_energy_sensor_ = sensor; }
  void set_power_2a_frame_sensor(sensor::Sensor *sensor) { power_2a_frame_sensor_ = sensor; }
  void set_use_2a_frame_own_sensor(bool use_2a_frame_own_sensor) { use_2a_frame_own_sensor_ = use_2a_frame_own_sensor; }
  
  void set_obis_version_text_sensor(text_sensor::TextSensor *sensor) { obis_version_text_sensor_ = sensor; }
  void set_meter_id_text_sensor(text_sensor::TextSensor *sensor) { meter_id_text_sensor_ = sensor; }
  void set_meter_type_text_sensor(text_sensor::TextSensor *sensor) { meter_type_text_sensor_ = sensor; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  bool read_message();
  void process_current_frame();
  void parse_han_obis(uint16_t position);
  void parse_current_value(uint16_t position, uint8_t phase);
  void parse_voltage_value(uint16_t position, uint8_t phase);
  void parse_energy_value(uint16_t position);
  void parse_text_value(uint16_t position, text_sensor::TextSensor *sensor);
  uint32_t extract_obis_value(uint16_t position, uint8_t length);
  bool is_valid_frame_start(uint16_t position);
  void reset_buffer();
  uint32_t search_for_real_time_power();
  void parse_a1_frame();
  uint16_t find_next_separator(uint16_t start_pos);
  void parse_a1_obis_value(uint8_t obis_type, uint16_t data_start, uint16_t data_end);

  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *current_l1_sensor_{nullptr};
  sensor::Sensor *current_l2_sensor_{nullptr};
  sensor::Sensor *current_l3_sensor_{nullptr};
  sensor::Sensor *voltage_l1_sensor_{nullptr};
  sensor::Sensor *voltage_l2_sensor_{nullptr};
  sensor::Sensor *voltage_l3_sensor_{nullptr};
  sensor::Sensor *energy_sensor_{nullptr};
  sensor::Sensor *reactive_power_sensor_{nullptr};
  sensor::Sensor *reactive_energy_sensor_{nullptr};
  sensor::Sensor *reactive_export_energy_sensor_{nullptr};
  sensor::Sensor *power_2a_frame_sensor_{nullptr};
  
  text_sensor::TextSensor *obis_version_text_sensor_{nullptr};
  text_sensor::TextSensor *meter_id_text_sensor_{nullptr};
  text_sensor::TextSensor *meter_type_text_sensor_{nullptr};

  uint8_t uart_buffer_[4096]{0};
  uint16_t uart_counter_{0};
  uint32_t last_frame_time_{0};
  bool use_2a_frame_own_sensor_{false};

  static const uint16_t FRAME_TIMEOUT_MS = 2000;
};

}  // namespace mbus_meter
}  // namespace esphome