#include "medicine_source.h"
#include <string.h>
#include "aegis_frame.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_client.h"

static const char *TAG = "medicine";

static aegis_parser_t s_parser;
static bool s_have_seq;
static uint8_t s_last_seq;
static int64_t s_last_scan_us, s_last_hb_us;
static uint32_t s_crc_err, s_fmt_err, s_rejected;

#if CONFIG_AEGIS_CAMERA_SOURCE_UART
#define LINK_UART UART_NUM_2
static void send_reply(uint8_t type, uint8_t seq)
{
    uint8_t f[AEGIS_MAX_FRAME];
    size_t n = aegis_frame_encode(type, seq, NULL, 0, f, sizeof f);
    uart_write_bytes(LINK_UART, f, n);
}
#else
static void send_reply(uint8_t type, uint8_t seq) { (void)type; (void)seq; }   /* mock has no peer */
#endif

static void handle_frame(const aegis_frame_t *f)
{
    switch (f->type) {
    case AEGIS_TYPE_SCAN: {
        if (f->len < 2) { send_reply(AEGIS_TYPE_NACK, f->seq); s_rejected++; return; }
        uint8_t sym = f->payload[0];
        const char *code = (const char *)&f->payload[1];
        size_t clen = f->len - 1;
        if (!aegis_code_valid(sym, code, clen)) {
            ESP_LOGW(TAG, "rejected invalid code (sym=%s, len=%u)", aegis_sym_name(sym), (unsigned)clen);
            send_reply(AEGIS_TYPE_NACK, f->seq);
            s_rejected++;
            return;
        }
        send_reply(AEGIS_TYPE_ACK, f->seq);
        int64_t now = esp_timer_get_time();
        if (s_have_seq && s_last_seq == f->seq && now - s_last_scan_us < 2000000) return;  /* CAM retransmit */
        s_have_seq = true; s_last_seq = f->seq; s_last_scan_us = now;

        net_record_t r = { .kind = NET_REC_SCAN };
        memcpy(r.u.scan.code, code, clen);
        r.u.scan.symbology = sym;
        net_client_submit(&r);
        ESP_LOGI(TAG, "scan accepted: %s '%s'", aegis_sym_name(sym), r.u.scan.code);
        break;
    }
    case AEGIS_TYPE_STATUS: s_last_hb_us = esp_timer_get_time(); break;
    default: ESP_LOGW(TAG, "unexpected frame type %u", f->type); break;
    }
}

static void process_bytes(const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        aegis_frame_t f;
        switch (aegis_parser_feed(&s_parser, d[i], &f)) {
        case AEGIS_PARSE_FRAME:      handle_frame(&f); break;
        case AEGIS_PARSE_ERR_CRC:    s_crc_err++; ESP_LOGW(TAG, "frame dropped: CRC error (total %lu)", (unsigned long)s_crc_err); break;
        case AEGIS_PARSE_ERR_FORMAT: s_fmt_err++; ESP_LOGW(TAG, "frame dropped: format error (total %lu)", (unsigned long)s_fmt_err); break;
        default: break;
        }
    }
}

bool medicine_source_cam_alive(void) { return s_last_hb_us && (esp_timer_get_time() - s_last_hb_us) < 15000000; }

#if CONFIG_AEGIS_CAMERA_SOURCE_UART
static void source_task(void *arg)
{
    uart_config_t u = { .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
                        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                        .source_clk = UART_SCLK_DEFAULT };
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &u));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, CONFIG_AEGIS_UART_TX_GPIO, CONFIG_AEGIS_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART2 up (RX=%d TX=%d)", CONFIG_AEGIS_UART_RX_GPIO, CONFIG_AEGIS_UART_TX_GPIO);
    uint8_t buf[64];
    for (;;) {
        int n = uart_read_bytes(LINK_UART, buf, sizeof buf, pdMS_TO_TICKS(100));
        if (n > 0) process_bytes(buf, n);
        else aegis_parser_reset(&s_parser);      /* inter-byte timeout: discard partial frame */
    }
}
#else
/* Wokwi has no camera model, so this task plays the ESP32-CAM: it builds real frames (real CRC)
 * and pushes them through process_bytes(). Every 5th frame is deliberately corrupted, and one code
 * has a bad EAN-13 check digit, to exercise both rejection paths. */
static void source_task(void *arg)
{
    static const struct { uint8_t sym; const char *code; } k[] = {
        { AEGIS_SYM_QR, "AEGIS-PARA-500" }, { AEGIS_SYM_EAN13, "8901234567890" },
        { AEGIS_SYM_QR, "AEGIS-IBU-400" },  { AEGIS_SYM_EAN13, "8901234567891" /* bad check digit */ },
    };
    uint8_t seq = 0, payload[AEGIS_MAX_PAYLOAD], frame[AEGIS_MAX_FRAME];
    ESP_LOGW(TAG, "MOCK camera active: simulated scans every %d s (no real camera)", CONFIG_AEGIS_MOCK_SCAN_PERIOD_S);
    for (unsigned i = 0;; i++) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_AEGIS_MOCK_SCAN_PERIOD_S * 1000));
        size_t l = strlen(k[i % 4].code);
        payload[0] = k[i % 4].sym;
        memcpy(payload + 1, k[i % 4].code, l);
        size_t n = aegis_frame_encode(AEGIS_TYPE_SCAN, ++seq, payload, (uint8_t)(l + 1), frame, sizeof frame);
        if (i % 5 == 4) frame[n / 2] ^= 0x40;   /* simulated line noise */
        process_bytes(frame, n);
        aegis_parser_reset(&s_parser);
    }
}
#endif

esp_err_t medicine_source_start(void)
{
    aegis_parser_reset(&s_parser);
    return xTaskCreate(source_task, "medicine", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
