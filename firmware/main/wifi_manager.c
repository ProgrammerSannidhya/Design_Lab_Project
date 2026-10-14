/*
 * Wi-Fi station manager. No dedicated task: esp_wifi + esp_event drive everything.
 *  - esp_netif/esp_wifi      : TCP/IP + 802.11 station
 *  - esp_event handlers      : START -> connect, DISCONNECTED -> backoff retry, GOT_IP -> mark online
 *  - FreeRTOS event group    : lets other tasks wait for connectivity without polling
 *  - esp_timer one-shot      : exponential reconnect backoff (1s .. 30s)
 *  - esp_sntp                : wall-clock time for record timestamps
 */
#include "wifi_manager.h"
#include <string.h>
#include <time.h>
#include "aegis_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi";
#define BIT_CONNECTED BIT0
#define BACKOFF_MIN_MS 1000
#define BACKOFF_MAX_MS 30000

static EventGroupHandle_t s_ev;
static esp_timer_handle_t s_retry;
static uint32_t s_backoff = BACKOFF_MIN_MS;
static bool s_sntp_started;

static void retry_cb(void *arg) { esp_wifi_connect(); }

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        xEventGroupClearBits(s_ev, BIT_CONNECTED);
        ESP_LOGW(TAG, "disconnected (reason %d), retry in %u ms", d->reason, (unsigned)s_backoff);
        esp_timer_stop(s_retry);
        esp_timer_start_once(s_retry, (uint64_t)s_backoff * 1000ULL);
        s_backoff = (s_backoff * 2 > BACKOFF_MAX_MS) ? BACKOFF_MAX_MS : s_backoff * 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "connected, ip " IPSTR, IP2STR(&e->ip_info.ip));
        s_backoff = BACKOFF_MIN_MS;
        xEventGroupSetBits(s_ev, BIT_CONNECTED);
        if (!s_sntp_started) {
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            s_sntp_started = true;
        }
    }
}

esp_err_t wifi_manager_start(void)
{
    s_ev = xEventGroupCreate();
    const esp_timer_create_args_t ta = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_retry));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   /* credentials only live in our NVS */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    char ssid[33], pass[65];
    aegis_config_get("wifi_ssid", CONFIG_AEGIS_WIFI_SSID, ssid, sizeof ssid);
    aegis_config_get("wifi_pass", CONFIG_AEGIS_WIFI_PASS, pass, sizeof pass);

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, pass, sizeof wc.sta.password);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_LOGI(TAG, "starting, ssid '%s'", ssid);
    return esp_wifi_start();
}

bool wifi_manager_wait_connected(TickType_t t)
{
    return (xEventGroupWaitBits(s_ev, BIT_CONNECTED, pdFALSE, pdTRUE, t) & BIT_CONNECTED) != 0;
}
bool wifi_manager_is_connected(void) { return (xEventGroupGetBits(s_ev) & BIT_CONNECTED) != 0; }

int8_t wifi_manager_rssi(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}
bool wifi_manager_time_synced(void) { return time(NULL) > 1700000000; }
