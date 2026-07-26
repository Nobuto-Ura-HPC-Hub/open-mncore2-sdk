/*
 * data_parse.h — u01/u02 共通のデータパースユーティリティ
 *
 * ヘッダオンリー (static 関数)。
 * DMA 送信データの構築に使う動的バッファと各種リテラルパーサを提供する。
 */

#ifndef ULIB_DATA_PARSE_H
#define ULIB_DATA_PARSE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "parse_u64.h"

/* ------------------------------------------------------------------ */
/* 動的バッファ                                                         */
/* ------------------------------------------------------------------ */

struct buf {
    uint8_t *data;
    size_t   len;
    size_t   cap;
};

static inline void buf_init(struct buf *b)
{
    b->cap = 256;
    b->data = malloc(b->cap);
    b->len = 0;
}

static inline void buf_append(struct buf *b, const void *p, size_t n)
{
    while (b->len + n > b->cap) {
        b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

/* ------------------------------------------------------------------ */
/* リテラルパーサ                                                       */
/* ------------------------------------------------------------------ */

/* float → bf16 変換 (上位 16bit) */
static inline uint16_t float_to_bf16(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return (uint16_t)(bits >> 16);
}

/* 0x プレフィックス付き hex を桁数でサイズ判定してバッファに追加 */
static inline int parse_hex_literal(struct buf *b, const char *s)
{
    const char *hex = s + 2;
    size_t ndigits = strlen(hex);

    if (ndigits <= 2) {
        uint8_t v = (uint8_t)strtoul(hex, NULL, 16);
        buf_append(b, &v, 1);
    } else if (ndigits <= 4) {
        uint16_t v = (uint16_t)strtoul(hex, NULL, 16);
        buf_append(b, &v, 2);
    } else if (ndigits <= 8) {
        uint32_t v = (uint32_t)strtoul(hex, NULL, 16);
        buf_append(b, &v, 4);
    } else if (ndigits <= 16) {
        uint64_t v = strtoull(hex, NULL, 16);
        buf_append(b, &v, 8);
    } else {
        fprintf(stderr, "エラー: hex リテラルが長すぎる: %s\n", s);
        return -1;
    }
    return 0;
}

/* 型付きリテラル (f64:, f32:, bf16:) をパース */
static inline int parse_typed_literal(struct buf *b, const char *s)
{
    if (strncmp(s, "f64:", 4) == 0) {
        double v = strtod(s + 4, NULL);
        buf_append(b, &v, 8);
    } else if (strncmp(s, "f32:", 4) == 0) {
        float v = strtof(s + 4, NULL);
        buf_append(b, &v, 4);
    } else if (strncmp(s, "bf16:", 5) == 0) {
        float f = strtof(s + 5, NULL);
        uint16_t v = float_to_bf16(f);
        buf_append(b, &v, 2);
    } else {
        fprintf(stderr, "エラー: 不明な形式: %s\n", s);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* stdin バイナリ読み込み                                                */
/* ------------------------------------------------------------------ */

static inline int read_stdin_binary(struct buf *b)
{
    uint8_t tmp[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, tmp, sizeof(tmp))) > 0)
        buf_append(b, tmp, (size_t)n);
    if (n < 0) {
        perror("read stdin");
        return -1;
    }
    return 0;
}

#endif /* ULIB_DATA_PARSE_H */
