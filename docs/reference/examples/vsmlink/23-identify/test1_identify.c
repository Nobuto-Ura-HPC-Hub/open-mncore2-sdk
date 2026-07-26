/* test1_identify.c — @identify 版 test1 の E2E 検証
 *
 * @identify x $lr8 4096 で PE ID を配布し、
 * 偶奇分岐して 偶数PE→0, 奇数PE→ID を @collect する。
 *
 * @identify は実装上 @distribute と同じで PDM slot 8 からデータを読む。
 * test1 と同様に IDs=[0..4095] を事前に送信する必要がある。
 *
 * IDs=[0..4095] → 期待値 [0, 1, 0, 3, ..., 0, 4095]
 *
 * Usage: MNC2_BACKEND=emu:lib ./test1_identify_exe [kernel]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "mnc2.h"

#define ELEM_COUNT    4096
#define ELEM_BYTES    (ELEM_COUNT * sizeof(uint64_t))
#define OFFSET_IDS    (0ULL * 8)    /* slot 8: IDs (identify.param :addr 0) */
#define OFFSET_OUT    (8192ULL * 8) /* slot 24: output */
#define DMAID_TRIGGER 0x10
#define WD_RECV       0x1e  /* .param recv_wait_tag と対応 */

int main(int argc, char *argv[])
{
    const char *kernel = "_build/test1_identify.idma.dat";
    const char *output_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output_path = argv[++i];
        else kernel = argv[i];
    }

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *ids_buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    void *out_buf = mnc2_alloc_host_buffer(dev, ELEM_BYTES);
    if (!ids_buf || !out_buf) { fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1; }

    int rc = MNC2_SUCCESS;

    /* @identify は PDM slot 8 から読む（@distribute と同じ仕組み） */
    uint64_t *ids = (uint64_t *)ids_buf;
    for (int i = 0; i < ELEM_COUNT; i++) ids[i] = (uint64_t)i;
    rc = mnc2_send(dev, ids_buf, OFFSET_IDS, ELEM_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send IDs\n"); goto cleanup; }

    mnc2_kernel_t k = mnc2_load_kernel(dev, kernel);
    if (!k) { fprintf(stderr, "FAIL: load kernel: %s\n", kernel); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); goto cleanup; }

    memset(out_buf, 0, ELEM_BYTES);
    rc = mnc2_recv(dev, out_buf, OFFSET_OUT, ELEM_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv\n"); goto cleanup; }

    if (output_path) {
        FILE *fp = fopen(output_path, "wb");
        if (fp) { fwrite(out_buf, sizeof(uint64_t), ELEM_COUNT, fp); fclose(fp); }
        else { perror(output_path); }
    }

    int errors = 0;
    uint64_t *out = (uint64_t *)out_buf;
    for (int i = 0; i < ELEM_COUNT; i++) {
        uint64_t id     = (uint64_t)i;
        uint64_t expect = (id % 2 == 0) ? 0 : id;
        if (out[i] != expect) {
            if (errors < 5)
                fprintf(stderr, "  MISMATCH [%d]: got=%" PRIu64 " expect=%" PRIu64 "\n",
                        i, out[i], expect);
            errors++;
        }
    }
    if (errors) fprintf(stderr, "test1_identify FAIL: %d errors\n", errors);
    else        printf("test1_identify PASS\n");

cleanup:
    mnc2_free_host_buffer(dev, ids_buf, ELEM_BYTES);
    mnc2_free_host_buffer(dev, out_buf, ELEM_BYTES);
    mnc2_reset(dev);
    mnc2_close(dev);
    return (rc != MNC2_SUCCESS || errors) ? 1 : 0;
}
