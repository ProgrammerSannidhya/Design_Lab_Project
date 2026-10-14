/*
 * AEGISMED ESP32-CAM <-> ESP32 UART protocol (shared by both firmwares).
 *
 *  STX(0x02) | VER | TYPE | SEQ | LEN | PAYLOAD[LEN<=96] | CRC16 hi | CRC16 lo | ETX(0x03)
 *
 *  CRC16-CCITT (poly 0x1021, init 0xFFFF) over VER..PAYLOAD.
 *  SCAN payload: [0]=symbology (aegis_sym_t), [1..]=ASCII code (no terminator).
 *  Length-delimited (not text-delimited): payload bytes can never be confused with framing.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AEGIS_STX            0x02
#define AEGIS_ETX            0x03
#define AEGIS_PROTO_VER      1
#define AEGIS_MAX_PAYLOAD    96
#define AEGIS_FRAME_OVERHEAD 8
#define AEGIS_MAX_FRAME      (AEGIS_MAX_PAYLOAD + AEGIS_FRAME_OVERHEAD)
#define AEGIS_MAX_CODE_LEN   64

typedef enum {
    AEGIS_TYPE_SCAN   = 1,  /* CAM -> ESP32: decoded code        */
    AEGIS_TYPE_ACK    = 2,  /* ESP32 -> CAM: accepted (same SEQ) */
    AEGIS_TYPE_NACK   = 3,  /* ESP32 -> CAM: rejected (same SEQ) */
    AEGIS_TYPE_STATUS = 4,  /* CAM -> ESP32: heartbeat, 4B uptime seconds (big endian) */
} aegis_type_t;

typedef enum {
    AEGIS_SYM_UNKNOWN = 0, AEGIS_SYM_QR = 1, AEGIS_SYM_EAN13 = 2,
    AEGIS_SYM_CODE128 = 3, AEGIS_SYM_DATAMATRIX = 4,
} aegis_sym_t;

typedef struct {
    uint8_t type, seq, len;
    uint8_t payload[AEGIS_MAX_PAYLOAD];
} aegis_frame_t;

typedef enum { AEGIS_PARSE_NONE = 0, AEGIS_PARSE_FRAME, AEGIS_PARSE_ERR_CRC, AEGIS_PARSE_ERR_FORMAT } aegis_parse_status_t;

typedef struct {
    uint8_t state, idx;
    uint16_t crc, crc_rx;
    aegis_frame_t f;
} aegis_parser_t;

/* Returns frame length written to out, or 0 on error. */
size_t aegis_frame_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t len,
                          uint8_t *out, size_t out_cap);

void aegis_parser_reset(aegis_parser_t *p);   /* also call after an inter-byte timeout */
aegis_parse_status_t aegis_parser_feed(aegis_parser_t *p, uint8_t byte, aegis_frame_t *out);

const char *aegis_sym_name(uint8_t sym);      /* "QR","EAN13","CODE128","DATAMATRIX","UNKNOWN" */

/* Charset [A-Za-z0-9-_.:/], length 4..64; EAN13 additionally needs 13 digits + valid check digit. */
bool aegis_code_valid(uint8_t sym, const char *code, size_t len);
