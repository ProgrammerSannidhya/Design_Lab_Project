#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Starts the medicine-scan input: real ESP32-CAM over UART2, or the Wokwi mock (Kconfig choice).
 * Both feed the SAME frame parser/validator; only the byte source differs. */
esp_err_t medicine_source_start(void);
bool medicine_source_cam_alive(void);   /* heartbeat seen in last 15 s (UART mode only) */
