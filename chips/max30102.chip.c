/* ============================================================================
 * AegisMed - Simulated MAX30102 custom Wokwi chip
 * ----------------------------------------------------------------------------
 * This is an EDUCATIONAL SIMULATION of a MAX30102 heart-rate / SpO2 sensor.
 * It does NOT implement the real MAX3010x FIFO-based protocol. Instead it
 * exposes a simplified single-byte register map so it can be driven directly
 * from Arduino Wire calls in the AegisMed sketch:
 *
 *   Register 0xFF  -> read-only, returns 0x15 (matches real MAX3010x PART_ID
 *                      so init-detection code that checks for 0x15 works)
 *   Register 0x2C  -> read-only, current simulated heart rate (BPM, uint8)
 *   Register 0x2D  -> read-only, current simulated SpO2 (%, uint8)
 *
 * The live BPM/SpO2 values are controlled by the "hr" and "spo2" sliders
 * defined in max30102.chip.json (visible in the Wokwi custom-chip control
 * panel). If your Wokwi build does not support the "controls"/slider schema
 * used here, you can instead hardcode demo values by editing DEFAULT_HR and
 * DEFAULT_SPO2 below and rebuilding the chip - that is the documented manual
 * fallback requested for this project.
 *
 * Built against Wokwi's Custom Chip C API (wokwi-api.h). See
 * https://docs.wokwi.com/chips-api/getting-started for the current API
 * surface if this needs adjusting for a newer Wokwi toolchain version.
 * ==========================================================================*/

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

#define REG_PART_ID   0xFF
#define REG_HEARTRATE 0x2C
#define REG_SPO2      0x2D

#define REAL_PART_ID  0x15

#define DEFAULT_HR    75
#define DEFAULT_SPO2  98

typedef struct {
  pin_t   pin_sda;
  pin_t   pin_scl;
  uint32_t attr_hr;
  uint32_t attr_spo2;
  uint8_t current_reg;
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  (void)address;
  (void)read;
  return true; // always ACK - this chip only lives on one fixed address
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;

  switch (chip->current_reg) {
    case REG_PART_ID:
      return REAL_PART_ID;

    case REG_HEARTRATE: {
      uint32_t hr = attr_read(chip->attr_hr);
      if (hr < 30) hr = 30;
      if (hr > 250) hr = 250;
      return (uint8_t)hr;
    }

    case REG_SPO2: {
      uint32_t spo2 = attr_read(chip->attr_spo2);
      if (spo2 > 100) spo2 = 100;
      return (uint8_t)spo2;
    }

    default:
      return 0x00;
  }
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  // The master's first write after a START selects the register pointer.
  chip->current_reg = data;
  return true;
}

static void on_i2c_disconnect(void *user_data) {
  (void)user_data;
  // Nothing to clean up between transactions.
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));

  chip->pin_sda = pin_init("SDA", INPUT);
  chip->pin_scl = pin_init("SCL", INPUT);
  chip->current_reg = REG_PART_ID;

  chip->attr_hr   = attr_init("hr", DEFAULT_HR);
  chip->attr_spo2 = attr_init("spo2", DEFAULT_SPO2);

  const i2c_config_t i2c_config = {
    .user_data = chip,
    .address = 0x57,           // real-world MAX3010x I2C address
    .scl = chip->pin_scl,
    .sda = chip->pin_sda,
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
  };
  i2c_init(&i2c_config);

  printf("AegisMed simulated MAX30102 ready (addr 0x57)\n");
}
