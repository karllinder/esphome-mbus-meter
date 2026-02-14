#include "esphome.h"

class MbusReader : public Component, public uart::UARTDevice, public Sensor {
  public:
  MbusReader(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

  static constexpr size_t kBufferSize = 4096;
  uint8_t temp_byte = 0;
  uint8_t *temp_byte_pointer = &temp_byte;
  uint8_t uart_buffer_[kBufferSize]{0};
  uint16_t uart_counter = 0;
  char uart_message[900];
  char temp_string[10];
  char obis_code[32];
  char temp_obis[10];
  uint32_t obis_value = 0;
  float wattage = 0;
  float amperage = 0;
  float voltage = 0;
  float energy = 0;

  Sensor *wattage_sensor = new Sensor();
  Sensor *ampl1_sensor = new Sensor();
  Sensor *ampl2_sensor = new Sensor();
  Sensor *ampl3_sensor = new Sensor();
  Sensor *voltl1_sensor = new Sensor();
  Sensor *voltl2_sensor = new Sensor();
  Sensor *voltl3_sensor = new Sensor();
  Sensor *energy_sensor = new Sensor();
  Sensor *reactive_power_sensor = new Sensor();
  Sensor *reactive_energy_sensor = new Sensor();

  void setup() override {

  }

  void loop() override {
    bool have_message = read_message();
  }

  bool read_message() {
    while(available() >= 1) {
      read_byte(this->temp_byte_pointer);
      if(temp_byte == 126) {
        if(uart_counter > 2 && uart_counter < kBufferSize) {
          uart_buffer_[uart_counter] = temp_byte;
          uart_counter++;
          strncpy(uart_message, "", sizeof(uart_message));
          for (uint16_t i = 0; i < uart_counter && i < 256; i++) {
            if(i > 0 && uart_buffer_[i-1] == 9 && uart_buffer_[i] == 6) {
              strncpy(obis_code, "", sizeof(obis_code));
              for (uint16_t y = 1; y < 6; y++) {
                sprintf(temp_obis, "%d.", uart_buffer_[i + y]);
                strncat(obis_code, temp_obis, sizeof(obis_code) - strlen(obis_code) - 1);
              }
              sprintf(temp_obis, "%d", uart_buffer_[i + 6]);
              strncat(obis_code, temp_obis, sizeof(obis_code) - strlen(obis_code) - 1);
              obis_value = 0;
              if(uart_buffer_[i + 7] == 6) {
                for(uint8_t y = 0; y < 4; y++) {
                  obis_value += (long)uart_buffer_[i + 8 + y] << ((3-y) * 8);
                }
              } else if(uart_buffer_[i + 7] == 18) {
                for(uint8_t y = 0; y < 2; y++) {
                  obis_value += (long)uart_buffer_[i + 8 + y] << ((1-y) * 8);
                }
              }
              
              if(strcmp(obis_code, "1.0.1.7.0.255") == 0) {
                  ESP_LOGV("uart", "Wattage: %d", obis_value);
                  wattage_sensor->publish_state(obis_value);
              } else if (strcmp(obis_code, "1.0.31.7.0.255") == 0) {
                  ESP_LOGV("uart", "Amp L1: %d", obis_value);
                  ampl1_sensor->publish_state(obis_value);
              } else if (strcmp(obis_code, "1.0.51.7.0.255") == 0) {
                  ESP_LOGV("uart", "Amp L2: %d", obis_value);
                  ampl2_sensor->publish_state(obis_value);
              } else if (strcmp(obis_code, "1.0.71.7.0.255") == 0) {
                  ESP_LOGV("uart", "Amp L3: %d", obis_value);
                  ampl3_sensor->publish_state(obis_value);
              } else if (strcmp(obis_code, "1.0.32.7.0.255") == 0) {
                  ESP_LOGV("uart", "Volt L1 : %d", obis_value);
                  voltl1_sensor->publish_state(obis_value);
              } else if (strcmp(obis_code, "1.0.52.7.0.255") == 0) {
                  ESP_LOGV("uart", "Volt L2: %d", obis_value);
                  voltl2_sensor->publish_state(obis_value);
              } else if (strcmp(obis_code, "1.0.72.7.0.255") == 0) {
                  ESP_LOGV("uart", "Volt L3: %d", obis_value);
                  voltl3_sensor->publish_state(obis_value);
              } else if (strcmp(obis_code, "1.0.1.8.0.255") == 0) {
                  ESP_LOGV("uart", "Energy Usage Last Hour: %d", obis_value);
                  energy_sensor->publish_state(obis_value);
              } else if (strcmp(obis_code, "1.0.4.7.0.255") == 0) {
                  ESP_LOGV("uart", "Reactive Power: %d", obis_value);
                  reactive_power_sensor->publish_state(obis_value);
              } else if (strcmp(obis_code, "1.0.3.8.0.255") == 0) {
                  ESP_LOGV("uart", "Reactive Power Last Hour: %d", obis_value);
                  reactive_energy_sensor->publish_state(obis_value);
              } else {
                ESP_LOGV("uart", "Unknown OBIS %s, value: %d", obis_code, obis_value);
              }
            }
            //strncat(uart_message, " ", 1);
          }
          ESP_LOGV("uart", "%d length received", uart_counter);
          ESP_LOGV("uart", "Message length: %d", strlen(uart_message));
          uart_counter = 0;
          strncpy(uart_message, "", sizeof(uart_message));
        } else {
          uart_counter = 0;
        }
      }
      if(uart_counter < kBufferSize) {
        uart_buffer_[uart_counter] = temp_byte;
        uart_counter++;
      }
    }

    return false;
  }

  ~MbusReader() {
    delete wattage_sensor;
    delete ampl1_sensor;
    delete ampl2_sensor;
    delete ampl3_sensor;
    delete voltl1_sensor;
    delete voltl2_sensor;
    delete voltl3_sensor;
    delete energy_sensor;
    delete reactive_power_sensor;
    delete reactive_energy_sensor;
  }
};
