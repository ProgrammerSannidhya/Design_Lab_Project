#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t aegis_config_init(void);                                   /* NVS init */
void aegis_config_get(const char *key, const char *def, char *out, size_t cap); /* NVS value or default */
esp_err_t aegis_config_set(const char *key, const char *val);        /* persist override */
uint32_t aegis_config_next_boot_id(void);                            /* persistent boot counter */
