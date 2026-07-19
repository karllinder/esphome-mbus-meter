#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace mbus_meter {

class MbusMeter : public Component, public uart::UARTDevice {
 public:
  void set_power_sensor(sensor::Sensor *sensor) { power_sensor_ = sensor; }
  void set_current_l1_sensor(sensor::Sensor *sensor) { current_l1_sensor_ = sensor; }
  void set_current_l2_sensor(sensor::Sensor *sensor) { current_l2_sensor_ = sensor; }
  void set_current_l3_sensor(sensor::Sensor *sensor) { current_l3_sensor_ = sensor; }
  void set_voltage_l1_sensor(sensor::Sensor *sensor) { voltage_l1_sensor_ = sensor; }
  void set_voltage_l2_sensor(sensor::Sensor *sensor) { voltage_l2_sensor_ = sensor; }
  void set_voltage_l3_sensor(sensor::Sensor *sensor) { voltage_l3_sensor_ = sensor; }
  void set_energy_sensor(sensor::Sensor *sensor) { energy_sensor_ = sensor; }
  void set_export_energy_sensor(sensor::Sensor *sensor) { export_energy_sensor_ = sensor; }
  void set_reactive_power_sensor(sensor::Sensor *sensor) { reactive_power_sensor_ = sensor; }
  void set_reactive_energy_sensor(sensor::Sensor *sensor) { reactive_energy_sensor_ = sensor; }
  void set_reactive_export_energy_sensor(sensor::Sensor *sensor) { reactive_export_energy_sensor_ = sensor; }
  void set_power_2a_frame_sensor(sensor::Sensor *sensor) { power_2a_frame_sensor_ = sensor; }
  void set_use_2a_frame_own_sensor(bool use_2a_frame_own_sensor) { use_2a_frame_own_sensor_ = use_2a_frame_own_sensor; }

  void set_obis_version_text_sensor(text_sensor::TextSensor *sensor) { obis_version_text_sensor_ = sensor; }
  void set_meter_id_text_sensor(text_sensor::TextSensor *sensor) { meter_id_text_sensor_ = sensor; }
  void set_meter_type_text_sensor(text_sensor::TextSensor *sensor) { meter_type_text_sensor_ = sensor; }
  void set_meter_time_text_sensor(text_sensor::TextSensor *sensor) { meter_time_text_sensor_ = sensor; }

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
  uint32_t reconstruct_power(uint8_t low_byte);
  void parse_a1_frame();
  uint16_t find_next_separator(uint16_t start_pos);
  void parse_a1_obis_value(uint8_t obis_type, uint16_t data_start, uint16_t data_end);
  void handle_energy_record(uint8_t energy_type, uint16_t value_start, uint16_t value_length);
  void parse_clock_record(uint16_t year_lo_pos);

  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *current_l1_sensor_{nullptr};
  sensor::Sensor *current_l2_sensor_{nullptr};
  sensor::Sensor *current_l3_sensor_{nullptr};
  sensor::Sensor *voltage_l1_sensor_{nullptr};
  sensor::Sensor *voltage_l2_sensor_{nullptr};
  sensor::Sensor *voltage_l3_sensor_{nullptr};
  sensor::Sensor *energy_sensor_{nullptr};
  sensor::Sensor *export_energy_sensor_{nullptr};
  sensor::Sensor *reactive_power_sensor_{nullptr};
  sensor::Sensor *reactive_energy_sensor_{nullptr};
  sensor::Sensor *reactive_export_energy_sensor_{nullptr};
  sensor::Sensor *power_2a_frame_sensor_{nullptr};

  text_sensor::TextSensor *obis_version_text_sensor_{nullptr};
  text_sensor::TextSensor *meter_id_text_sensor_{nullptr};
  text_sensor::TextSensor *meter_type_text_sensor_{nullptr};
  text_sensor::TextSensor *meter_time_text_sensor_{nullptr};

  uint8_t uart_buffer_[4096]{0};
  uint16_t uart_counter_{0};
  uint32_t last_frame_time_{0};
  bool use_2a_frame_own_sensor_{false};
  // Last known raw voltage (0.1V units) per phase, used to reconstruct
  // values whose high byte the meter has truncated. Start at nominal 230.0V.
  uint16_t last_voltage_raw_[3]{2300, 2300, 2300};
  // Net reactive power for the frame being parsed (import positive, export
  // negative); published once per A1 frame to avoid 0/-value bouncing
  float frame_reactive_net_{0.0f};
  bool frame_reactive_seen_{false};
  // Last published power and the total phase current at that moment; an
  // unchanged current means an unchanged load level, which lets truncated
  // power values track the last published value exactly
  uint32_t last_power_w_{0};
  float last_power_sum_i_{-1000.0f};
  // Last full 2-byte reactive export value, to recognize truncated repeats
  // (a 1-byte value matching its high byte while the load is unchanged)
  uint32_t last_reactive_export_{0};
  float last_reactive_sum_i_{-1000.0f};
  // Last known phase currents, reference for reconstructing truncated
  // single-byte power values (the meter drops the high byte under load)
  float last_current_a_[3]{0.0f, 0.0f, 0.0f};
  // Truncated power byte seen in the current A1 frame; resolved at end of
  // frame once this frame's currents are known
  uint8_t frame_power_byte_{0};
  bool frame_power_pending_{false};
  // Last published raw value per energy counter (import, export, reactive
  // import, reactive export). A lossy big-endian read can only underestimate
  // (dropped bytes remove digits), so truncated counter reads are recognized
  // by falling below the last published value.
  uint32_t last_energy_raw_[4]{0, 0, 0, 0};

  static const uint16_t FRAME_TIMEOUT_MS = 2000;
  static const uint16_t FRAME_START_MIN_BYTES = 20;
  static const uint16_t A1_FRAME_MIN_BYTES_TIMEOUT = 100;
  static const uint16_t A1_FRAME_MIN_BYTES_IMMEDIATE = 150;
  static const uint16_t SHORT_FRAME_MIN_BYTES_TIMEOUT = 18;
  static const uint16_t SHORT_FRAME_MIN_BYTES_IMMEDIATE = 50;
};

}  // namespace mbus_meter
}  // namespace esphome