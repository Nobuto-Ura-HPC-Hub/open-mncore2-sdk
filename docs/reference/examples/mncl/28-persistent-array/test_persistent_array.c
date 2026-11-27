/* test_persistent_array.c -- persistent の配列 tab[8] を、2 つの kernel（init、step）で共有する。
 *
 * init: tab[0..3] = a、tab[4..7] = b（PE ごとに 8 個）
 * step: tab[k] = tab[k] * 2、out = tab[0] + ... + tab[7]
 *
 * init を 1 回、step を 3 回実行する。step の r 回目（r = 1, 2, 3）の out は (a と b の 8 個の和) * 2^r。
 *
 * PDM の並び（persistent-array.param）:
 *   a   (init の引数): PDM[0..16383]      distribute4。番地 = 成分 * 4096 + PE の番号
 *   b   (init の引数): PDM[16384..32767]  同上
 *   out (step の引数): PDM[32768..36863]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define NPE      4096
#define V4_BYTES (4 * NPE * sizeof(double))
#define OUT_BYTES (NPE * sizeof(double))
#define OFFSET_A   (0ULL)
#define OFFSET_B   (16384ULL * 8)
#define OFFSET_OUT (32768ULL * 8)
#define RECV_TAG 0x1e
#define ROUNDS 3

int main(void)
{
    printf("=== 28-persistent-array: init -> step x%d (4096 PE、persistent double tab[8]) ===\n", ROUNDS);

    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    printf("backend: %s\n", mnc2_get_backend_name(dev));

    int rc = 0;
    mnc2_kernel_t init_k = NULL, step_k = NULL;
    double *a = mnc2_alloc_host_buffer(dev, V4_BYTES);
    double *b = mnc2_alloc_host_buffer(dev, V4_BYTES);
    double *out = mnc2_alloc_host_buffer(dev, OUT_BYTES);
    double sum0[NPE];
    if (!a || !b || !out) { fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup; }

    /* PE の番号 i の tab[k] の初期値は i + k（2 進で正確に表せる値） */
    for (int i = 0; i < NPE; i++) {
        sum0[i] = 0.0;
        for (int c = 0; c < 4; c++) {
            a[c * NPE + i] = (double)(i + c);
            b[c * NPE + i] = (double)(i + 4 + c);
            sum0[i] += a[c * NPE + i] + b[c * NPE + i];
        }
    }

    /* a と b を送って init を実行する（exec の前に送り終えるので、タグは 0） */
    if (mnc2_send(dev, a, OFFSET_A, V4_BYTES, 0) != MNC2_SUCCESS || mnc2_send(dev, b, OFFSET_B, V4_BYTES, 0) != MNC2_SUCCESS) {
        fprintf(stderr, "FAIL: send\n"); rc = 1; goto cleanup;
    }
    init_k = mnc2_load_kernel(dev, "_build/init.idma.dat");
    step_k = mnc2_load_kernel(dev, "_build/step.idma.dat");
    if (!init_k || !step_k) { fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup; }
    if (mnc2_exec_kernel(init_k) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec init\n"); rc = 1; goto cleanup; }

    /* step を 3 回実行する。tab は kernel の実行をまたいで残るので、out は実行のたびに 2 倍になる */
    double scale = 1.0;
    for (int r = 1; r <= ROUNDS; r++) {
        if (mnc2_exec_kernel(step_k) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec step %d\n", r); rc = 1; goto cleanup; }
        memset(out, 0, OUT_BYTES);
        if (mnc2_recv(dev, out, OFFSET_OUT, OUT_BYTES, RECV_TAG) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv %d\n", r); rc = 1; goto cleanup; }
        scale *= 2.0;
        int errors = 0;
        for (int i = 0; i < NPE; i++) {
            double want = sum0[i] * scale;
            if (out[i] != want) {
                if (errors < 5) fprintf(stderr, "  MISMATCH step %d out[%d]: got=%g want=%g\n", r, i, out[i], want);
                errors++;
            }
        }
        if (errors) { fprintf(stderr, "FAIL: step %d, %d/%d mismatches\n", r, errors, NPE); rc = 1; goto cleanup; }
        printf("PASS: step %d out == (tab の初期値の和) x %g（%d 要素）\n", r, scale, NPE);
    }

cleanup:
    if (init_k) mnc2_free_kernel(init_k);
    if (step_k) mnc2_free_kernel(step_k);
    if (a) mnc2_free_host_buffer(dev, a, V4_BYTES);
    if (b) mnc2_free_host_buffer(dev, b, V4_BYTES);
    if (out) mnc2_free_host_buffer(dev, out, OUT_BYTES);
    mnc2_close(dev);
    return rc ? 1 : 0;
}
