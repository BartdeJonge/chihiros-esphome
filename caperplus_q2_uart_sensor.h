s#include "esphome.h"

#define bytes_to_uint16(MSB,LSB) (((unsigned int) ((unsigned char) MSB)) & 255)<<8 | (((unsigned char) LSB)&255)

class CaperPlusQ2UartSensor : public Component, public UARTDevice
{
public:
  CaperPlusQ2UartSensor(UARTComponent *parent) : UARTDevice(parent) {}

  Sensor *temperature_sensor = new Sensor();
  Sensor *ph_sensor = new Sensor();
  Sensor *tds_sensor = new Sensor();

protected:
  float pH = 0.0;
  float temperature = 0.0;
  float tds_float = 0.0; 
  
  uint32_t last_read_time = 0;
  // FIXED: Changed cooldown tracking timer to exactly 30,000 milliseconds
  const uint32_t READ_COOLDOWN_MS = 30000; 

  void clearSerialBuffer()
  {
    uint16_t availableBytes = available();
    if (availableBytes > 0)
    {
      uint8_t b[availableBytes];
      read_array(b, availableBytes);
      yield(); 
    }
  }

  void setup() override 
  {
    // Intentionally blank
  }

  void loop() override 
  {
    // During the 30-second cooldown, we aggressively dump trailing buffer data 
    // and instantly yield execution back to the Wi-Fi/MQTT/API engine
    if (millis() - last_read_time < READ_COOLDOWN_MS)
    {
      clearSerialBuffer();
      return;
    }

    uint8_t data[62];
    uint16_t value;

    memset(&data[0], 0x00, 62);

    if (available() <= 61)
    {
      return;
    }

    read_array(data, 62);

    if ((data[0] == 0xFF) && (data[1] == 0xFF) && (data[2] == 0x00) && (data[3] == 0x3A))
    {
      // Valid header found
    }
    else
    {
      clearSerialBuffer();
      return;
    }

    // Process variables via their explicit memory indices
    value = bytes_to_uint16(data[49], data[50]);
    pH = value / 100.0;

    uint16_t raw_tds = bytes_to_uint16(data[53], data[54]);
    tds_float = (float)raw_tds;

    value = bytes_to_uint16(data[55], data[56]);
    temperature = value / 100.0;

    // Publish to the native sensors
    temperature_sensor->publish_state(temperature);
    ph_sensor->publish_state(pH);
    tds_sensor->publish_state(tds_float);

    // Lock in the timestamp to begin the next 30-second silent window
    last_read_time = millis();

    clearSerialBuffer();
  }
};
