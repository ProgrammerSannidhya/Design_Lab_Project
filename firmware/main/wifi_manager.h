#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"

esp_err_t wifi_manager_start(void);                       /* STA mode, event-driven, auto-reconnect */
bool wifi_manager_wait_connected(TickType_t timeout);     /* blocks only the caller */
bool wifi_manager_is_connected(void);
int8_t wifi_manager_rssi(void);                           /* 0 if not associated */
bool wifi_manager_time_synced(void);                      /* SNTP has set the clock */
