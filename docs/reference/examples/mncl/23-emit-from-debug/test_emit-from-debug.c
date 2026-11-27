/* test_emit-from-debug.c -- emit_from の example の E2E test (emu:lib)
 *
 * k 回目（1〜100）の後の PE n の値: (n + 1) + k (k + 1) / 2     （初期値 n + 1 に、1 + 2 + ... + k を足した値）
 * emit_from は、k 回目に、HW PE ID が pe(k)（k <= 60 なら 817、それ以外は 529）の PE の属する L1B の 64 PE の値を、64 u64 のブロックとして、
 * d の (k - 1) 番目のブロックに書く。ブロックの i 番目は、L1B の先頭の PE + i の値（817 は先頭が 768、529 は 512）。
 * 100 回 > 64 ブロックなので、L1BM（16 ブロック）と L2BM（64 ブロック）の専用の領域が溢れて、PDM へ書き出される経路も通る。
 * ホストは、collect の完了を待ってから、d を読む。
 *
 * PDM layout (emit-from-debug.param):
 *   a (slot 8):  PDM[0..4095]       b (slot 16): PDM[4096..4195]
 *   c (slot 32): PDM[8192..12287]   d (slot 24): PDM[16384..22783]
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define PE_COUNT    4096
#define PE_BYTES    (PE_COUNT * sizeof(uint64_t))
#define NCALL       100
#define SWITCH      60
#define BLOCK       64
#define B_BYTES     (BLOCK * sizeof(uint64_t))          /* broadcast の入力は 64 u64 が最小の転送単位 */
#define EMIT_BYTES  ((size_t)NCALL * BLOCK * sizeof(uint64_t))
#define OFFSET_A    0ULL
#define OFFSET_B    (4096ULL * 8)
#define OFFSET_C    (8192ULL * 8)
#define OFFSET_D    (16384ULL * 8)
#define TAG_A       0x10
#define RECV_TAG    0x1e

int main(void)
{
    printf("=== 23-emit-from-debug: broadcast を足して emit_from で書き出す（%d 回）===\n\n", NCALL);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    printf("23-emit-from-debug backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    void *abuf = mnc2_alloc_host_buffer(dev, PE_BYTES);
    void *bbuf = mnc2_alloc_host_buffer(dev, B_BYTES * 2);
    void *cbuf = mnc2_alloc_host_buffer(dev, PE_BYTES);
    void *dbuf = mnc2_alloc_host_buffer(dev, EMIT_BYTES);
    if (!abuf || !bbuf || !cbuf || !dbuf) { fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup; }

    uint64_t *a = (uint64_t *)abuf, *b = (uint64_t *)bbuf;
    for (int j = 0; j < PE_COUNT; j++) { double v = (double)(j + 1); memcpy(&a[j], &v, sizeof v); }
    memset(bbuf, 0, B_BYTES * 2);
    for (int k = 0; k < NCALL; k++) { double v = (double)(k + 1); memcpy(&b[k], &v, sizeof v); }

    /* b は 128 u64 を送る（100 個 + 余り。64 u64 の倍数） */
    if (mnc2_send(dev, bbuf, OFFSET_B, B_BYTES * 2, 0) != MNC2_SUCCESS ||
        mnc2_send(dev, abuf, OFFSET_A, PE_BYTES, TAG_A) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send\n"); rc = 1; goto cleanup;
    }

    mnc2_kernel_t kernel = mnc2_load_kernel(dev, "_build/emit-from-debug.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup; }
    int krc = mnc2_exec_kernel(kernel);
    mnc2_free_kernel(kernel);
    if (krc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    /* collect の完了を待つ（kernel の終わり）。その後で、emit_from の書き出しを読む（待ちのタグは 0） */
    memset(cbuf, 0, PE_BYTES);
    if (mnc2_recv(dev, cbuf, OFFSET_C, PE_BYTES, RECV_TAG) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv c\n"); rc = 1; goto cleanup; }
    memset(dbuf, 0, EMIT_BYTES);
    if (mnc2_recv(dev, dbuf, OFFSET_D, EMIT_BYTES, 0) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv d\n"); rc = 1; goto cleanup; }

    int errors = 0;
    uint64_t *d = (uint64_t *)dbuf, *c = (uint64_t *)cbuf;
    for (int k = 1; k <= NCALL; k++) {
        int pe = (k <= SWITCH) ? 817 : 529;
        int base = (pe / BLOCK) * BLOCK;
        for (int i = 0; i < BLOCK; i++) {
            double want = (double)(base + i + 1) + (double)k * (double)(k + 1) / 2.0;
            double got; memcpy(&got, &d[(k - 1) * BLOCK + i], sizeof got);
            if (got != want) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH k=%d block[%d] (PE %d): got=%.9g expected=%.9g\n", k, i, base + i, got, want);
                errors++;
            }
        }
    }
    for (int j = 0; j < PE_COUNT; j++) {
        double want = (double)(j + 1) + (double)NCALL * (double)(NCALL + 1) / 2.0;
        double got; memcpy(&got, &c[j], sizeof got);
        if (got != want) { if (errors < 5) fprintf(stderr, "  MISMATCH collect[%d]: got=%.9g expected=%.9g\n", j, got, want); errors++; }
    }

    if (errors > 0) { fprintf(stderr, "FAIL: %d mismatches\n", errors); rc = 1; }
    else            { printf("PASS emit_from %d 回（HW PE ID 817 を %d 回、529 を %d 回。%d 要素のブロック）と collect %d/%d\n",
                             NCALL, SWITCH, NCALL - SWITCH, BLOCK, PE_COUNT, PE_COUNT); rc = 0; }

cleanup:
    if (abuf) mnc2_free_host_buffer(dev, abuf, PE_BYTES);
    if (bbuf) mnc2_free_host_buffer(dev, bbuf, B_BYTES * 2);
    if (cbuf) mnc2_free_host_buffer(dev, cbuf, PE_BYTES);
    if (dbuf) mnc2_free_host_buffer(dev, dbuf, EMIT_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
