#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "aegis_frame.h"
#include "esp_err.h"

typedef enum { NET_REC_READING = 1, NET_REC_SCAN = 2 } net_rec_kind_t;

typedef struct {
    net_rec_kind_t kind;
    uint32_t id;          /* buffer-internal, assigned by net_client_submit */
    uint32_t boot_id;     /* assigned by submit */
    uint32_t seq;         /* assigned by submit; (device, boot_id, seq) is the idempotency key */
    int64_t  uptime_ms;   /* assigned by submit */
    int64_t  epoch_s;     /* assigned by submit; 0 if SNTP not synced yet */
    union {
        struct { int16_t hr; int16_t spo2; float temp_c; } reading;   /* hr/spo2 <= 0 or NaN temp = invalid -> null */
        struct { char code[AEGIS_MAX_CODE_LEN + 1]; uint8_t symbology; } scan;
    } u;
} net_record_t;

esp_err_t net_client_start(void);                 /* ring buffer + sender task */
void net_client_submit(net_record_t *rec);        /* non-blocking; drops OLDEST record if buffer full */
uint32_t net_client_pending(void);
uint32_t net_client_dropped(void);
