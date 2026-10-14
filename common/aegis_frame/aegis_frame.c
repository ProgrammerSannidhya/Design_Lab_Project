#include "aegis_frame.h"
#include <string.h>

enum { S_IDLE, S_VER, S_TYPE, S_SEQ, S_LEN, S_PAYLOAD, S_CRC_HI, S_CRC_LO, S_ETX };

static uint16_t crc16_update(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    return crc;
}

size_t aegis_frame_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint8_t len,
                          uint8_t *out, size_t out_cap)
{
    if (len > AEGIS_MAX_PAYLOAD || out_cap < (size_t)len + AEGIS_FRAME_OVERHEAD) return 0;
    size_t n = 0;
    out[n++] = AEGIS_STX; out[n++] = AEGIS_PROTO_VER; out[n++] = type; out[n++] = seq; out[n++] = len;
    if (len) { memcpy(out + n, payload, len); n += len; }
    uint16_t crc = 0xFFFF;
    for (size_t i = 1; i < n; i++) crc = crc16_update(crc, out[i]);
    out[n++] = (uint8_t)(crc >> 8); out[n++] = (uint8_t)crc; out[n++] = AEGIS_ETX;
    return n;
}

void aegis_parser_reset(aegis_parser_t *p) { p->state = S_IDLE; p->idx = 0; }

aegis_parse_status_t aegis_parser_feed(aegis_parser_t *p, uint8_t b, aegis_frame_t *out)
{
    switch (p->state) {
    case S_IDLE:
        if (b == AEGIS_STX) { p->crc = 0xFFFF; p->state = S_VER; }
        break;
    case S_VER:
        if (b != AEGIS_PROTO_VER) { p->state = S_IDLE; return AEGIS_PARSE_ERR_FORMAT; }
        p->crc = crc16_update(p->crc, b); p->state = S_TYPE; break;
    case S_TYPE: p->f.type = b; p->crc = crc16_update(p->crc, b); p->state = S_SEQ; break;
    case S_SEQ:  p->f.seq = b;  p->crc = crc16_update(p->crc, b); p->state = S_LEN; break;
    case S_LEN:
        if (b > AEGIS_MAX_PAYLOAD) { p->state = S_IDLE; return AEGIS_PARSE_ERR_FORMAT; }
        p->f.len = b; p->idx = 0; p->crc = crc16_update(p->crc, b);
        p->state = b ? S_PAYLOAD : S_CRC_HI; break;
    case S_PAYLOAD:
        p->f.payload[p->idx++] = b; p->crc = crc16_update(p->crc, b);
        if (p->idx == p->f.len) p->state = S_CRC_HI;
        break;
    case S_CRC_HI: p->crc_rx = (uint16_t)b << 8; p->state = S_CRC_LO; break;
    case S_CRC_LO: p->crc_rx |= b; p->state = S_ETX; break;
    case S_ETX:
        p->state = S_IDLE;
        if (b != AEGIS_ETX) return AEGIS_PARSE_ERR_FORMAT;
        if (p->crc_rx != p->crc) return AEGIS_PARSE_ERR_CRC;
        *out = p->f;
        return AEGIS_PARSE_FRAME;
    default: p->state = S_IDLE; break;
    }
    return AEGIS_PARSE_NONE;
}

const char *aegis_sym_name(uint8_t sym)
{
    switch (sym) {
    case AEGIS_SYM_QR: return "QR";
    case AEGIS_SYM_EAN13: return "EAN13";
    case AEGIS_SYM_CODE128: return "CODE128";
    case AEGIS_SYM_DATAMATRIX: return "DATAMATRIX";
    default: return "UNKNOWN";
    }
}

bool aegis_code_valid(uint8_t sym, const char *code, size_t len)
{
    if (!code || len < 4 || len > AEGIS_MAX_CODE_LEN) return false;
    for (size_t i = 0; i < len; i++) {
        char c = code[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  c == '-' || c == '_' || c == '.' || c == ':' || c == '/';
        if (!ok) return false;
    }
    if (sym == AEGIS_SYM_EAN13) {
        if (len != 13) return false;
        int sum = 0;
        for (int i = 0; i < 13; i++) if (code[i] < '0' || code[i] > '9') return false;
        for (int i = 0; i < 12; i++) sum += (code[i] - '0') * ((i & 1) ? 3 : 1);
        if ((10 - sum % 10) % 10 != code[12] - '0') return false;
    }
    return true;
}
