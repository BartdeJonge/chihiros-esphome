#include "esphome.h"

// inspired from:
// - https://github.com/DFRobot/DFRobot_MAX17043/blob/master/DFRobot_MAX17043.h
// - https://github.com/exxamalte/esphome-customisations/tree/master/mlx90614

#define MAX17043_ADDRESS        0x36

#define MAX17043_VCELL          0x02 // voltage
#define MAX17043_SOC            0x04 // percentage
#define MAX17043_MODE           0x06
#define MAX17043_VERSION        0x08
#define MAX17043_CONFIG         0x0c
#define MAX17043_COMMAND        0xfe


class MAX17043Sensor : public PollingComponent, public Sensor {
 public:
  Sensor *voltage_sensor = new Sensor();
  Sensor *percentage_sensor = new Sensor();

  MAX17043Sensor() : PollingComponent(10000) {}

  void setup() override {
    // Initialize the device here. Usually Wire.begin() will be called in here,
    // though that call is unnecessary if you have an 'i2c:' entry in your config
    ESP_LOGD("custom", "Starting up MAX17043 sensor");

    Wire.begin();
  }

  uint16_t read16(uint8_t reg) {
      uint16_t        temp;
      Wire.begin();
      Wire.beginTransmission(MAX17043_ADDRESS);
      Wire.write(reg);
      Wire.endTransmission();
      Wire.requestFrom(MAX17043_ADDRESS, 2);
      temp = (uint16_t)Wire.read() << 8;
      temp |= (uint16_t)Wire.read();
      Wire.endTransmission();
      return temp;
  }


  void update() override {
    float voltage = (1.25f * (float)(read16(MAX17043_VCELL) >> 4)) / 1000;
    voltage_sensor->publish_state(voltage);

    uint16_t percentage_tmp = read16(MAX17043_SOC);
    float percentage = (float)((percentage_tmp >> 8) + 0.003906f * (percentage_tmp & 0x00ff));

    percentage_sensor->publish_state(percentage);
  }
};