/* test_emit_from_debug.c — @distribute + @broadcast + dvadd + @emit_from + @reduce の E2E 検証
 *
 * Usage: test_emit_from_debug <kernel.idma.dat> <steps> <switch>
 *
 *   PE j の初期値は j+1（f64）。@broadcast の k 回目の値は k（f64）。k 回目の後の PE n の値は (n+1) + k(k+1)/2。
 *   @emit_from は、k 回目の後に、k <= switch なら HW PE ID 817、それ以外は HW PE ID 529 の値を書き出す。
 *   書き出しの領域の k 番目の 64 u64 のブロックの、n % 64 番目がその値である。
 *   @reduce は、全 PE の最終値の合計を sub_pe_id 別の 4 個で返す。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "mnc2.h"

#define PE_COUNT       4096
#define OFFSET_INIT    (0ULL)
#define OFFSET_BCAST   (4096ULL * 8)
#define OFFSET_REDUCE  (8448ULL * 8)
#define OFFSET_EMIT    (16384ULL * 8)
#define REDUCE_COUNT   4
#define PE_A           817
#define PE_B           529
#define DMAID_SEND     0x10
#define WD_REDUCE      0x06

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, sizeof u); return u; }
static double   val(uint64_t u) { double d; memcpy(&d, &u, sizeof d); return d; }

int main(int argc, char *argv[])
{
    if (argc != 4) { fprintf(stderr, "usage: %s <kernel> <steps> <switch>\n", argv[0]); return 2; }
    const char *kernel = argv[1];
    int steps = atoi(argv[2]);
    int sw = atoi(argv[3]);

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    size_t send_u64 = (((size_t)PE_COUNT + steps + 63) / 64) * 64;
    size_t send_bytes = send_u64 * sizeof(uint64_t);
    size_t reduce_bytes = REDUCE_COUNT * sizeof(uint64_t);
    size_t emit_bytes = (size_t)steps * 64 * sizeof(uint64_t);
    void *send_buf = mnc2_alloc_host_buffer(dev, send_bytes);
    void *reduce_buf = mnc2_alloc_host_buffer(dev, reduce_bytes);
    void *emit_buf = mnc2_alloc_host_buffer(dev, emit_bytes);
    if (!send_buf || !reduce_buf || !emit_buf) { fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1; }

    int rc = MNC2_SUCCESS;
    int errors = 0;

    uint64_t *sb = (uint64_t *)send_buf;
    memset(sb, 0, send_bytes);
    for (int j = 0; j < PE_COUNT; j++) sb[j] = bits((double)(j + 1));
    for (int k = 1; k <= steps; k++) sb[PE_COUNT + k - 1] = bits((double)k);

    rc = mnc2_send(dev, send_buf, OFFSET_INIT, send_bytes, DMAID_SEND);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send\n"); goto cleanup; }

    mnc2_kernel_t kn = mnc2_load_kernel(dev, kernel);
    if (!kn) { fprintf(stderr, "FAIL: load kernel: %s\n", kernel); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(kn);
    mnc2_free_kernel(kn);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); goto cleanup; }

    /* @reduce の受信で kernel の終わりを待つ。@emit_from の書き込みは、その後に読む。 */
    rc = mnc2_recv(dev, reduce_buf, OFFSET_REDUCE, reduce_bytes, WD_REDUCE);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv reduce\n"); goto cleanup; }
    memset(emit_buf, 0, emit_bytes);
    rc = mnc2_recv(dev, emit_buf, OFFSET_EMIT, emit_bytes, 0);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv emit\n"); goto cleanup; }

    /* @emit_from の書き出し */
    uint64_t *em = (uint64_t *)emit_buf;
    for (int k = 1; k <= steps; k++) {
        int n = (k <= sw) ? PE_A : PE_B;
        double expect = (double)(n + 1) + (double)k * (double)(k + 1) / 2.0;
        uint64_t got = em[(size_t)(k - 1) * 64 + (n % 64)];
        if (got != bits(expect)) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH step %d (HW PE ID %d): got=%.17g expect=%.17g\n", k, n, val(got), expect);
            errors++;
        }
    }

    /* @reduce の結果。sub_pe_id p の PE は j = 4m + p（m = 0..1023）で、最終値は j + 1 + T */
    double total_add = (double)steps * (double)(steps + 1) / 2.0;
    uint64_t *rd = (uint64_t *)reduce_buf;
    for (int p = 0; p < REDUCE_COUNT; p++) {
        double expect = 2095104.0 + 1024.0 * ((double)p + 1.0 + total_add);
        if (rd[p] != bits(expect)) {
            fprintf(stderr, "  MISMATCH reduce sub_pe_id %d: got=%.17g expect=%.17g\n", p, val(rd[p]), expect);
            errors++;
        }
    }

    if (errors) fprintf(stderr, "emit_from_debug FAIL: %d errors\n", errors);
    else        printf("emit_from_debug PASS (%d steps, switch %d)\n", steps, sw);

cleanup:
    mnc2_free_host_buffer(dev, send_buf, send_bytes);
    mnc2_free_host_buffer(dev, reduce_buf, reduce_bytes);
    mnc2_free_host_buffer(dev, emit_buf, emit_bytes);
    mnc2_reset(dev);
    mnc2_close(dev);
    return (rc != MNC2_SUCCESS || errors) ? 1 : 0;
}
