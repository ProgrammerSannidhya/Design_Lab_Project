#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef struct { int16_t hr; int16_t spo2; float temp_c; } sensor_sample_t;   /* hr/spo2<=0, NaN temp = invalid */

esp_err_t sensors_init(void);
esp_err_t sensors_read(sensor_sample_t *out);
