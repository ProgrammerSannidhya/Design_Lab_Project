/*
 * REST client. Producers (sensor/medicine tasks) call net_client_submit(), which only takes a
 * short mutex and never touches the network. A single sender task drains the buffer oldest-first
 * with esp_http_client; a record leaves the buffer only after the backend answered 200/201.
 * 200 = "duplicate already stored", so retries after a lost response are safe.
 */
#include "net_client.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "aegis_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wifi_manager.h"

static const char *TAG = "net";
#define CAP CONFIG_AEGIS_QUEUE_CAPACITY

static net_record_t *s_buf;
static size_t s_head, s_count;
static uint32_t s_next_id, s_seq, s_boot_id, s_dropped;
static SemaphoreHandle_t s_mu;
static char s_base[128], s_dev[32], s_key[64];

void net_client_submit(net_record_t *r)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    r->id = ++s_next_id;
    r->boot_id = s_boot_id;
    r->seq = ++s_seq;
    r->uptime_ms = esp_timer_get_time() / 1000;
    time_t now = time(NULL);
    r->epoch_s = now > 1700000000 ? (int64_t)now : 0;
    if (s_count == CAP) { s_head = (s_head + 1) % CAP; s_count--; s_dropped++; }
    s_buf[(s_head + s_count) % CAP] = *r;
    s_count++;
    xSemaphoreGive(s_mu);
}

static bool peek(net_record_t *out)
{
    bool ok = false;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    if (s_count) { *out = s_buf[s_head]; ok = true; }
    xSemaphoreGive(s_mu);
    return ok;
}

/* Pop only if the head is still the record we sent (it may have been dropped meanwhile). */
static void pop_if(uint32_t id)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    if (s_count && s_buf[s_head].id == id) { s_head = (s_head + 1) % CAP; s_count--; }
    xSemaphoreGive(s_mu);
}

uint32_t net_client_pending(void) { return (uint32_t)s_count; }
uint32_t net_client_dropped(void) { return s_dropped; }

static int build_body(const net_record_t *r, char *b, size_t n)
{
    char ts[40];
    if (r->epoch_s) {
        time_t t = (time_t)r->epoch_s; struct tm tm; gmtime_r(&t, &tm);
        snprintf(ts, sizeof ts, "\"%04d-%02d-%02dT%02d:%02d:%02dZ\"", tm.tm_year + 1900, tm.tm_mon + 1,
                 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        strcpy(ts, "null");
    }
    int o = snprintf(b, n, "{\"boot_id\":%lu,\"seq\":%lu,\"uptime_ms\":%lld,",
                     (unsigned long)r->boot_id, (unsigned long)r->seq, (long long)r->uptime_ms);
    if (r->kind == NET_REC_READING) {
        o += snprintf(b + o, n - o, "\"recorded_at\":%s,", ts);
        o += r->u.reading.hr > 0 ? snprintf(b + o, n - o, "\"heart_rate\":%d,", r->u.reading.hr)
                                 : snprintf(b + o, n - o, "\"heart_rate\":null,");
        o += r->u.reading.spo2 > 0 ? snprintf(b + o, n - o, "\"spo2\":%d,", r->u.reading.spo2)
                                   : snprintf(b + o, n - o, "\"spo2\":null,");
        o += isnan(r->u.reading.temp_c) ? snprintf(b + o, n - o, "\"temperature_c\":null}")
                                        : snprintf(b + o, n - o, "\"temperature_c\":%.1f}", r->u.reading.temp_c);
    } else {   /* code charset was validated on receipt: no JSON escaping needed */
        o += snprintf(b + o, n - o, "\"scanned_at\":%s,\"medicine_code\":\"%s\",\"symbology\":\"%s\"}", ts,
                      r->u.scan.code, aegis_sym_name(r->u.scan.symbology));
    }
    return o;
}

/* Returns HTTP status, or -1 on transport error. */
static int post_record(const net_record_t *r)
{
    char url[224], body[384];
    snprintf(url, sizeof url, "%s/api/v1/devices/%s/%s", s_base, s_dev,
             r->kind == NET_REC_READING ? "readings" : "medicine-scans");
    int len = build_body(r, body, sizeof body);

    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 5000 };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "X-Device-Key", s_key);
    esp_http_client_set_post_field(c, body, len);
    esp_err_t e = esp_http_client_perform(c);
    int status = (e == ESP_OK) ? esp_http_client_get_status_code(c) : -1;
    if (e != ESP_OK) ESP_LOGW(TAG, "POST failed: %s", esp_err_to_name(e));
    esp_http_client_cleanup(c);
    return status;
}

static void net_task(void *arg)
{
    uint32_t backoff = 0;
    for (;;) {
        net_record_t r;
        if (!peek(&r)) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
        if (!wifi_manager_wait_connected(pdMS_TO_TICKS(2000))) continue;

        int st = post_record(&r);
        if (st == 200 || st == 201) {
            pop_if(r.id);
            backoff = 0;
            ESP_LOGI(TAG, "sent %s seq=%lu (%s), pending=%u", r.kind == NET_REC_READING ? "reading" : "scan",
                     (unsigned long)r.seq, st == 201 ? "stored" : "duplicate", (unsigned)s_count);
        } else if (st >= 400 && st < 500 && st != 401 && st != 403 && st != 408 && st != 429) {
            ESP_LOGE(TAG, "backend rejected seq=%lu with %d, dropping poison record", (unsigned long)r.seq, st);
            pop_if(r.id);
        } else {   /* transport error, 5xx, auth/config problem: keep data, back off */
            backoff = backoff ? (backoff * 2 > 30000 ? 30000 : backoff * 2) : 1000;
            ESP_LOGW(TAG, "send failed (status %d), retry in %u ms, pending=%u", st, (unsigned)backoff, (unsigned)s_count);
            vTaskDelay(pdMS_TO_TICKS(backoff));
        }
    }
}

esp_err_t net_client_start(void)
{
    s_buf = calloc(CAP, sizeof(net_record_t));
    s_mu = xSemaphoreCreateMutex();
    if (!s_buf || !s_mu) return ESP_ERR_NO_MEM;
    s_boot_id = aegis_config_next_boot_id();
    aegis_config_get("backend", CONFIG_AEGIS_BACKEND_URL, s_base, sizeof s_base);
    aegis_config_get("dev_id", CONFIG_AEGIS_DEVICE_ID, s_dev, sizeof s_dev);
    aegis_config_get("dev_key", CONFIG_AEGIS_DEVICE_KEY, s_key, sizeof s_key);
    ESP_LOGI(TAG, "device '%s' boot_id=%lu backend=%s", s_dev, (unsigned long)s_boot_id, s_base);
    return xTaskCreate(net_task, "net", 6144, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
