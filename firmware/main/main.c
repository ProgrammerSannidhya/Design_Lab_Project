/*
 * AEGISMED main controller (ESP-IDF).
 *
 *  sensor_task      -> net_client_submit()  \
 *  medicine_source  -> net_client_submit()   >-- ring buffer --> net task --> HTTP POST --> FastAPI
 *  wifi_manager (event driven, no task)    /
 *
 * Display/LED/buzzer/button tasks from the full design are not in this drop.
 */
#include <math.h>
#include "aegis_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "medicine_source.h"
#include "net_client.h"
#include "sensors.h"
#include "wifi_manager.h"

static const char *TAG = "main";

static void sensor_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        sensor_sample_t s;
        if (sensors_read(&s) == ESP_OK) {
            net_record_t r = { .kind = NET_REC_READING };
            r.u.reading.hr = s.hr;
            r.u.reading.spo2 = s.spo2;
            r.u.reading.temp_c = s.temp_c;
            net_client_submit(&r);      /* never blocks on the network */
        } else {
            ESP_LOGW(TAG, "sensor read failed");
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CONFIG_AEGIS_SENSOR_PERIOD_MS));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(aegis_config_init());
    ESP_ERROR_CHECK(net_client_start());
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(medicine_source_start());
    ESP_ERROR_CHECK(sensors_init());
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 6, NULL);
}
