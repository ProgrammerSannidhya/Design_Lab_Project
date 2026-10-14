/*
 * PLACEHOLDER, NOT A DRIVER. The existing project is an Arduino sketch (sketch.ino) and I have not
 * seen the custom chips' I2C register maps (chips/*.chip.json/.c), so no real MAX30102/MLX90614
 * driver is included yet. This returns slowly varying synthetic values so the network/DB pipeline
 * can be tested. Replace the body of sensors_read() with i2c_master reads (mutex-guarded bus shared
 * with the OLED) once the chip register interfaces are confirmed.
 */
#include "sensors.h"

esp_err_t sensors_init(void) { return ESP_OK; }

esp_err_t sensors_read(sensor_sample_t *o)
{
    static unsigned n;
    n++;
    o->hr = 72 + (n % 7);
    o->spo2 = 97 + (n % 3);
    o->temp_c = 36.6f + (float)(n % 5) * 0.1f;
    return ESP_OK;
}
