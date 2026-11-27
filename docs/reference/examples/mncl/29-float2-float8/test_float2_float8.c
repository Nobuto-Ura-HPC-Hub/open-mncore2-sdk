/* test_float2_float8.c -- 29-float2-float8 のホスト
 *
 * float2 / float8 は f32 を u64 に 2 つずつ詰める。**.x（float8 の偶数の要素）が u64 の上位 32 ビット**。
 * ホストの C の float[2] をそのまま u64 として送ると上位と下位が入れ替わるので、pack / unpack で並びを合わせる。
 * 値は、積が正確に表せるもの（2 進の仮数部が短いもの）にしてあり、期待値を C の float の演算でそのまま作れる。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mnc2.h"

#define NPE        4096
#define OFF_A      (0ULL * 8)          /* float2: PE あたり u64 1 つ */
#define OFF_B      (4096ULL * 8)       /* float8: PE あたり u64 4 つ（u64 c を 4096 PE 分、c = 0〜3） */
#define OFF_OUT2   (20480ULL * 8)
#define OFF_OUT8   (24576ULL * 8)
#define SEND_TAG   0x10
#define RECV_TAG   0x1e

static uint32_t bits(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }
static float fl(uint32_t b) { float v; memcpy(&v, &b, 4); return v; }
/* 2 つの f32 を u64 に詰める（hi が .x） */
static uint64_t pack(float hi, float lo) { return ((uint64_t)bits(hi) << 32) | bits(lo); }

int main(void)
{
    printf("=== 29-float2-float8: float2 / float8 (4096 PE) ===\n");
    mnc2_device_t dev = mnc2_open(0);
    if (dev == NULL) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    printf("backend: %s\n", mnc2_get_backend_name(dev));

    static float a[NPE][2], b[NPE][8];
    static uint64_t w[4 * NPE];
    int rc = 0, err2 = 0, err8 = 0;
    mnc2_kernel_t kernel = NULL;
    void *buf = mnc2_alloc_host_buffer(dev, sizeof(w));
    if (!buf) { fprintf(stderr, "FAIL: alloc\n"); rc = 1; goto cleanup; }

    for (int i = 0; i < NPE; i++) {
        a[i][0] = (float)(i % 64) * 0.25f;            /* .x */
        a[i][1] = (float)(i % 32) * -0.5f + 3.0f;     /* .y */
        for (int e = 0; e < 8; e++) b[i][e] = (float)((i + e * 7) % 16) * 0.125f + (float)e;
    }

    /* b（float8）: u64 c に要素 2c と 2c + 1 */
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < NPE; i++) w[(size_t)c * NPE + i] = pack(b[i][2 * c], b[i][2 * c + 1]);
    memcpy(buf, w, sizeof(w));
    if (mnc2_send(dev, buf, OFF_B, sizeof(w), 0) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send b\n"); rc = 1; goto cleanup; }

    kernel = mnc2_load_kernel(dev, "_build/float2-float8.idma.dat");
    if (!kernel) { fprintf(stderr, "FAIL: load_kernel\n"); rc = 1; goto cleanup; }
    if (mnc2_exec_kernel(kernel) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    /* a（float2）: u64 1 つ */
    for (int i = 0; i < NPE; i++) w[i] = pack(a[i][0], a[i][1]);
    memcpy(buf, w, NPE * 8);
    if (mnc2_send(dev, buf, OFF_A, NPE * 8, SEND_TAG) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send a\n"); rc = 1; goto cleanup; }

    if (mnc2_recv(dev, buf, OFF_OUT8, sizeof(w), RECV_TAG) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv out8\n"); rc = 1; goto cleanup; }
    memcpy(w, buf, sizeof(w));
    for (int i = 0; i < NPE; i++)
        for (int e = 0; e < 8; e++) {
            const uint64_t u = w[(size_t)(e / 2) * NPE + i];
            const float got = fl((e & 1) ? (uint32_t)u : (uint32_t)(u >> 32));
            const float want = b[i][e] * b[i][e] - b[i][e];
            if (got != want) { if (err8 < 3) fprintf(stderr, "  MISMATCH out8 [%d][%d]: got=%g want=%g\n", i, e, got, want); err8++; }
        }
    if (mnc2_recv(dev, buf, OFF_OUT2, NPE * 8, 0) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv out2\n"); rc = 1; goto cleanup; }
    memcpy(w, buf, NPE * 8);
    for (int i = 0; i < NPE; i++) {
        const float gx = fl((uint32_t)(w[i] >> 32)), gy = fl((uint32_t)w[i]);
        const float wx = a[i][0] * 2.0f + a[i][1], wy = a[i][1] * 2.0f + a[i][0];
        if (gx != wx || gy != wy) { if (err2 < 3) fprintf(stderr, "  MISMATCH out2 [%d]: got=(%g, %g) want=(%g, %g)\n", i, gx, gy, wx, wy); err2++; }
    }
    printf("float2: x * 2 + (x.y, x.x): %s (%d/%d)\n", err2 ? "FAIL" : "PASS", NPE - err2, NPE);
    printf("float8: v * v - v        : %s (%d/%d)\n", err8 ? "FAIL" : "PASS", 8 * NPE - err8, 8 * NPE);
    rc = (err2 || err8) ? 1 : 0;
    printf(rc ? "FAIL\n" : "PASS\n");

cleanup:
    if (kernel) mnc2_free_kernel(kernel);
    if (buf) mnc2_free_host_buffer(dev, buf, sizeof(w));
    mnc2_close(dev);
    return rc;
}
