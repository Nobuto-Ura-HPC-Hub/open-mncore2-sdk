/* test_broadcast_collect.c — broadcast + collect の往復 (emu:lib / device)
 *
 * ホストが struct {x,y,z,m} を 1 個作り、@broadcast size 4 で全 PE に配る。各 PE は m を
 * @distribute した per-PE の id で差し替え、@collect size 4 で PDM (成分ごと) に集める。
 * ホストは成分ごと出力を struct に組み直し、x,y,z が broadcast 値 (全 PE 同一)、m が per-PE
 * の id (0..4095) になっていることを確認する。
 * 設計とレイアウトは docs/broadcast-collect-layout.md を参照。
 *
 * PDM レイアウト (broadcast_collect.param):
 *   slot 8  (broadcast 源): PDM[0..63]      先頭 4 u64 に x,y,z,m (AoS)
 *   slot 16 (distribute 源): PDM[4096..8191]  [id × 4096] (成分ごと)
 *   slot 24 (collect 出力): PDM[8192..24575]  [x×4096][y×4096][z×4096][m×4096] (成分ごと SoA)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mnc2.h"

#define PE_COUNT      4096
#define BCAST_COUNT   64
#define BCAST_BYTES   (BCAST_COUNT * sizeof(double))
#define ID_BYTES      (PE_COUNT * sizeof(double))
#define OUT_COUNT     (4 * PE_COUNT)
#define OUT_BYTES     (OUT_COUNT * sizeof(double))
#define OFF_BCAST     0
#define OFF_ID        (4096ULL * 8)
#define OFF_OUT       (8192ULL * 8)
#define DMAID_TRIGGER 0x10
#define WD_RECV       0x1e

/* broadcast する struct のテスト値 (m は各 PE で id に上書きされる) */
#define BX 1.5
#define BY 2.5
#define BZ 3.5
#define BM 99.0

int main(void)
{
    printf("=== broadcast + collect 往復 (struct {x,y,z,m}, 4096 PE) ===\n\n");

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }

    void *bcast_buf = mnc2_alloc_host_buffer(dev, BCAST_BYTES);
    void *id_buf    = mnc2_alloc_host_buffer(dev, ID_BYTES);
    void *out_buf   = mnc2_alloc_host_buffer(dev, OUT_BYTES);
    if (!bcast_buf || !id_buf || !out_buf) {
        fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1;
    }

    int rc;

    /* broadcast 源: 先頭 4 u64 に x,y,z,m。残りはマスクで読まれないので 0 でよい。非トリガー send。 */
    double *bb = (double *)bcast_buf;
    for (int i = 0; i < BCAST_COUNT; i++) bb[i] = 0.0;
    bb[0] = BX; bb[1] = BY; bb[2] = BZ; bb[3] = BM;
    rc = mnc2_send(dev, bcast_buf, OFF_BCAST, BCAST_BYTES, 0);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send broadcast\n"); rc = 1; goto cleanup; }

    /* distribute 源: id[p] = (double)p。トリガー send (wait i10 を解除)。 */
    double *ib = (double *)id_buf;
    for (int p = 0; p < PE_COUNT; p++) ib[p] = (double)p;
    rc = mnc2_send(dev, id_buf, OFF_ID, ID_BYTES, DMAID_TRIGGER);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send id\n"); rc = 1; goto cleanup; }

    /* exec */
    mnc2_kernel_t k = mnc2_load_kernel(dev, "_build/broadcast_collect.idma.dat");
    if (!k) { fprintf(stderr, "FAIL: load kernel\n"); rc = 1; goto cleanup; }
    rc = mnc2_exec_kernel(k);
    mnc2_free_kernel(k);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto cleanup; }

    /* recv 出力 (成分ごと SoA): [x×4096][y×4096][z×4096][m×4096] */
    memset(out_buf, 0, OUT_BYTES);
    rc = mnc2_recv(dev, out_buf, OFF_OUT, OUT_BYTES, WD_RECV);
    if (rc != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv\n"); rc = 1; goto cleanup; }

    /* 照合: PE p の struct を成分ごと出力から組み直す。
       x,y,z は broadcast 値 (全 p 同一)、m は per-PE の id (= p)。 */
    {
        double *op = (double *)out_buf;
        int errors = 0;
        for (int p = 0; p < PE_COUNT; p++) {
            double x = op[0 * PE_COUNT + p];
            double y = op[1 * PE_COUNT + p];
            double z = op[2 * PE_COUNT + p];
            double m = op[3 * PE_COUNT + p];
            if (x != BX || y != BY || z != BZ || m != (double)p) {
                if (errors < 5)
                    fprintf(stderr, "  MISMATCH PE %d: got {%g,%g,%g,%g} expected {%g,%g,%g,%g}\n",
                            p, x, y, z, m, BX, BY, BZ, (double)p);
                errors++;
            }
        }
        if (errors > 0) {
            fprintf(stderr, "  FAIL: %d PE で不一致 (broadcast か distribute か collect のレイアウトずれ)\n", errors);
            rc = 1;
        } else {
            printf("  PASS: x,y,z = broadcast 値 (全 PE 同一)、m = per-PE id (0..%d)。broadcast+collect 往復 OK\n",
                   PE_COUNT - 1);
            rc = 0;
        }
    }

cleanup:
    if (bcast_buf) mnc2_free_host_buffer(dev, bcast_buf, BCAST_BYTES);
    if (id_buf)    mnc2_free_host_buffer(dev, id_buf, ID_BYTES);
    if (out_buf)   mnc2_free_host_buffer(dev, out_buf, OUT_BYTES);
    mnc2_close(dev);
    return (rc != 0) ? 1 : 0;
}
