/* ============================================================================
 * AegisMed - Simulated MLX90614 custom Wokwi chip
 * ----------------------------------------------------------------------------
 * This is an EDUCATIONAL SIMULATION of an MLX90614 non-contact IR
 * thermometer. Unlike the MAX30102 sim, this chip implements the REAL
 * MLX90614 register formula for RAM register 0x07 (Object Temperature 1),
 * so the same firmware read logic (raw * 0.02 - 273.15) will also work
 * against a genuine MLX90614 on physical hardware.
 *
 * Simplifications vs the real chip:
 *   - PEC (CRC) byte is not sent; the ESP32 firmware only requests 2 bytes.
 *   - Only register 0x07 is implemented (ambient temp / emissivity /
 *     other RAM+EEPROM registers are not modeled).
 *
 * The "temp" slider in mlx90614.chip.json holds the object temperature in
 * tenths of a degree C (e.g. 368 = 36.8 C) so it can be represented as a
 * plain integer attribute. If your Wokwi build's attribute/control schema
 * differs, you can hardcode DEFAULT_TEMP_TENTHS_C below and rebuild - this
 * is the documented manual fallback for changing the simulated value.
 *
 * Built against Wokwi's Custom Chip C API (wokwi-api.h). See
 * https://docs.wokwi.com/chips-api/getting-started for the current API
 * surface if this needs adjusting for a newer Wokwi toolchain version.
 * ==========================================================================*/

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

#define REG_TOBJ1 0x07

#define DEFAULT_TEMP_TENTHS_C 368  // 36.8 C

typedef struct {
  pin_t   pin_sda;
  pin_t   pin_scl;
  uint32_t attr_temp_tenths;
  uint8_t current_reg;
  uint8_t byte_index;   // 0 = low byte next, 1 = high byte next
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = (chip_state_t *)user_data;
  (void)address;
  if (read) {
    chip->byte_index = 0; // restart the 2-byte sequence for a new read
  }
  return true;
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;

  if (chip->current_reg != REG_TOBJ1) {
    return 0x00;
  }

  int32_t tenths = (int32_t)attr_read(chip->attr_temp_tenths);
  float temp_c = tenths / 10.0f;

  // Real MLX90614 formula: raw = (Temp_C + 273.15) / 0.02
  uint16_t raw = (uint16_t)((temp_c + 273.15f) / 0.02f);

  uint8_t value = (chip->byte_index == 0) ? (uint8_t)(raw & 0xFF)
                                           : (uint8_t)((raw >> 8) & 0xFF);
  chip->byte_index = (chip->byte_index == 0) ? 1 : 0;
  return value;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->current_reg = data;   // master selects the command/register byte
  chip->byte_index = 0;
  return true;
}

static void on_i2c_disconnect(void *user_data) {
  (void)user_data;
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));

  chip->pin_sda = pin_init("SDA", INPUT);
  chip->pin_scl = pin_init("SCL", INPUT);
  chip->current_reg = REG_TOBJ1;
  chip->byte_index = 0;

  chip->attr_temp_tenths = attr_init("temp", DEFAULT_TEMP_TENTHS_C);

  const i2c_config_t i2c_config = {
    .user_data = chip,
    .address = 0x5A,           // real-world MLX90614 I2C address
    .scl = chip->pin_scl,
    .sda = chip->pin_sda,
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
  };
  i2c_init(&i2c_config);

  printf("AegisMed simulated MLX90614 ready (addr 0x5A)\n");
}
