/* test_identify_reduce.c — @identify + @reduce :liadd E2E 検証
 *
 * PE ID [0..4095] を @identify で配布し、@reduce :liadd で合計。
 * reduce の 4 部分和（PE position 0〜3）を個別に検証する。
 *
 * PE position p に属する PE の ID: {4k+p | k=0..1023}
 *   position 0: 0+4+8+…+4092 = 2,095,104
 *   position 1: 1+5+9+…+4093 = 2,096,128
 *   position 2: 2+6+10+…+4094 = 2,097,152
 *   position 3: 3+7+11+…+4095 = 2,098,176
 *   total: 8,386,560
 *
 * Usage: MNC2_BACKEND=emu:lib ./test_identify_reduce [kernel]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT     4096
#define ELEM_BYTES     (ELEM_COUNT * sizeof(uint64_t))

/* @identify: PDM addr 0 */
#define IDENTIFY_OFFSET   (0ULL * 8)
#define IDENTIFY_DMAID    0x10

/* @reduce: PDM addr 4096, 4 部分和 */
#define REDUCE_OFFSET     (4096ULL * 8)
#define REDUCE_COUNT      4
#define REDUCE_RECV_TAG   0x06

/* 期待される部分和: sum of {4k+p | k=0..1023} = 4*523776 + 1024*p */
static const uint64_t expected[4] = {
    2095104,  /* position 0 */
    2096128,  /* position 1 */
    2097152,  /* position 2 */
    2098176,  /* position 3 */
};

int main(int argc, char *argv[])
{
    const char *kernel = "_build/identify_reduce.idma.dat";
    const char *output_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output_path = argv[++i];
        else kernel = argv[i];
    }

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *ids_buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *rbuf    = mnc2_alloc_host_buffer(dev, REDUCE_COUNT * sizeof(uint64_t));
    if (!ids_buf || !rbuf) { fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1; }

    int rc = MNC2_SUCCESS;

    /* PE IDs [0..4095] を PDM に送信 */
    uint64_t *ids = (uint64_t *)ids_buf;
    for (int i = 0; i < ELEM_COUNT; i++) ids[i] = (uint64_t)i;
    rc = mnc2_send(dev, ids_buf, IDENTIFY_OFFSET, ELEM_BYTES,
                   IDENTIFY_DMAID);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send IDs\n"); goto cleanup; }

    /* カーネル実行 */
    mnc2_kernel_t k = mnc2_load_kernel(dev, kernel);
    if (!k) { fprintf(stderr, "FAIL: load kernel: %s\n", kernel); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); goto cleanup; }

    /* reduce 結果受信 (4 部分和) */
    memset(rbuf, 0, REDUCE_COUNT * sizeof(uint64_t));
    rc = mnc2_recv(dev, rbuf, REDUCE_OFFSET, REDUCE_COUNT * sizeof(uint64_t),
                   REDUCE_RECV_TAG);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv reduce\n"); goto cleanup; }

    if (output_path) {
        FILE *fp = fopen(output_path, "wb");
        if (fp) { fwrite(rbuf, sizeof(uint64_t), REDUCE_COUNT, fp); fclose(fp); }
        else { perror(output_path); }
    }

    /* 4 部分和を個別検証 */
    uint64_t *vals = (uint64_t *)rbuf;
    int errors = 0;
    for (int p = 0; p < REDUCE_COUNT; p++) {
        if (vals[p] != expected[p]) {
            fprintf(stderr, "  MISMATCH position[%d]: got=%" PRIu64
                    " expect=%" PRIu64 "\n", p, vals[p], expected[p]);
            errors++;
        }
    }
    uint64_t total = vals[0] + vals[1] + vals[2] + vals[3];
    if (errors)
        fprintf(stderr, "identify_reduce FAIL: %d position errors (total=%" PRIu64 ")\n",
                errors, total);
    else
        printf("identify_reduce PASS (total=%" PRIu64 ")\n", total);

cleanup:
    mnc2_free_host_buffer(dev, ids_buf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, rbuf, REDUCE_COUNT * sizeof(uint64_t));
    mnc2_close(dev);
    return (rc != MNC2_SUCCESS || errors) ? 1 : 0;
}
