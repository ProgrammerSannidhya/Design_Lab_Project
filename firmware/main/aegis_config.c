/* NVS-backed configuration: values in NVS namespace "aegis" override Kconfig defaults. */
#include "aegis_config.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NS "aegis"
static const char *TAG = "config";

esp_err_t aegis_config_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    return e;
}

void aegis_config_get(const char *key, const char *def, char *out, size_t cap)
{
    nvs_handle_t h;
    size_t len = cap;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        esp_err_t e = nvs_get_str(h, key, out, &len);
        nvs_close(h);
        if (e == ESP_OK) return;
    }
    strlcpy(out, def, cap);
}

esp_err_t aegis_config_set(const char *key, const char *val)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_str(h, key, val);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

uint32_t aegis_config_next_boot_id(void)
{
    nvs_handle_t h;
    uint32_t id = 0;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) { ESP_LOGW(TAG, "boot_id unavailable"); return 0; }
    nvs_get_u32(h, "boot_id", &id);
    id++;
    nvs_set_u32(h, "boot_id", id);
    nvs_commit(h);
    nvs_close(h);
    return id;
}
