/* soa2aos — collect の SoA 出力を device 上で AoS に並べ替える。
 *
 * 引数なし (CI 用): テストデータで自己完結し、AoS になっているか検証する。
 * --pipe:  stdin から SoA を読み、AoS を stdout に書く (Unix フィルタ)。
 *          例: cat soa.bin | soa2aos --pipe > aos.bin
 * --gen:   テスト用 SoA を stdout に書く。例: soa2aos --gen > soa.bin
 *
 * 形式 (いずれも float64 LE、4096 粒子):
 *   SoA: [x×4096][y×4096][z×4096][m×4096]   (16384 double = 131072 byte)
 *   AoS: struct { double x,y,z,m; } [4096]   (粒子順は順列)
 *
 * カーネルは対合 (involution): 2 回適用すると元に戻る。よって同じツールで
 * SoA->AoS も AoS->SoA もできる。仕組みは docs/broadcast-collect-layout.md。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include "mnc2.h"

#define PE    4096
#define N     (4 * PE)
#define BYTES (N * sizeof(double))

int main(int argc, char **argv)
{
    int pipe_mode = 0, gen_mode = 0, use_id = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pipe")) pipe_mode = 1;
        else if (!strcmp(argv[i], "--gen")) gen_mode = 1;
        else if (!strcmp(argv[i], "--use-global-id")) use_id = 1;
    }

    /* --gen: テスト SoA を stdout へ (device 不要) */
    if (gen_mode) {
        double *g = malloc(BYTES);
        for (int c = 0; c < 4; c++)
            for (int p = 0; p < PE; p++) g[c * PE + p] = (double)((c + 1) * 1000 + p);
        fwrite(g, 1, BYTES, stdout);
        free(g);
        return 0;
    }

    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "FAIL: mnc2_open\n"); return 1; }
    void *ib = mnc2_alloc_host_buffer(dev, BYTES), *ob = mnc2_alloc_host_buffer(dev, BYTES);
    if (!ib || !ob) { fprintf(stderr, "FAIL: alloc\n"); mnc2_close(dev); return 1; }
    int rc = 0;
    double *in = (double *)ib, *o = (double *)ob;

    if (pipe_mode) {
        size_t got = fread(in, 1, BYTES, stdin);
        if (got != BYTES) {
            fprintf(stderr, "FAIL: stdin %zu byte (期待 %zu = SoA 4096 粒子)\n", got, (size_t)BYTES);
            rc = 1; goto done;
        }
    } else {
        /* 自己完結のテストデータ (SoA): x[p]=1000+p, y=2000+p, z=3000+p, m=4000+p */
        for (int c = 0; c < 4; c++)
            for (int p = 0; p < PE; p++) in[c * PE + p] = (double)((c + 1) * 1000 + p);
    }

    if (mnc2_send(dev, ib, 0, BYTES, 0x10) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: send\n"); rc = 1; goto done; }

    /* カーネルはバイナリと同じ _build/ にある (CWD 非依存にするため argv[0] から解決) */
    char kp[512];
    { char *d = strdup(argv[0]); snprintf(kp, sizeof(kp), "%s/aos%s.idma.dat", dirname(d), use_id ? "_id" : ""); free(d); }
    mnc2_kernel_t k = mnc2_load_kernel(dev, kp);
    if (!k) { fprintf(stderr, "FAIL: load %s\n", kp); rc = 1; goto done; }
    if (mnc2_exec_kernel(k) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: exec\n"); rc = 1; goto done; }
    mnc2_free_kernel(k);
    memset(ob, 0, BYTES);
    if (mnc2_recv(dev, ob, 131072ULL * 8, BYTES, 0x1e) != MNC2_SUCCESS) { fprintf(stderr, "FAIL: recv\n"); rc = 1; goto done; }

    if (pipe_mode) {
        fwrite(ob, 1, BYTES, stdout);
        fprintf(stderr, "soa2aos: SoA -> AoS 変換 (%d 粒子、struct{x,y,z,m}[]、順列)\n", PE);
    } else {
        /* 自己完結の検証。x,y,z は両モード共通で「全 4096 粒子が AoS で揃う」を見る。m は通常モード
           では元の m (4000+p)、--use-global-id では flat_id (0..4095 の順列) を見る。 */
        int *seen_p = calloc(PE, sizeof(int)), *seen_id = calloc(PE, sizeof(int));
        int bad = 0, dupp = 0, missp = 0, badid = 0, dupid = 0, missid = 0;
        for (int kk = 0; kk < PE; kk++) {
            double v0 = o[4 * kk], v1 = o[4 * kk + 1], v2 = o[4 * kk + 2], v3 = o[4 * kk + 3];
            int p = (int)(v0 - 1000.0);
            if (p < 0 || p >= PE || v0 != (double)(1000 + p) || v1 != (double)(2000 + p) || v2 != (double)(3000 + p)) bad++;
            else { if (seen_p[p]) dupp++; else seen_p[p] = 1; }
            if (use_id) {
                int id = (int)v3;
                if (v3 != (double)id || id < 0 || id >= PE) badid++;
                else { if (seen_id[id]) dupid++; else seen_id[id] = 1; }
            } else if (v3 != (double)(4000 + p)) bad++;
        }
        for (int p = 0; p < PE; p++) { if (!seen_p[p]) missp++; if (use_id && !seen_id[p]) missid++; }
        int ok = (bad == 0 && dupp == 0 && missp == 0) && (!use_id || (badid == 0 && dupid == 0 && missid == 0));
        if (ok) {
            if (use_id) printf("  PASS: 全 %d 粒子の struct が AoS で揃い、m が flat_id (0..%d の順列)。--use-global-id OK\n", PE, PE - 1);
            else        printf("  PASS: 全 %d 粒子の struct が AoS で揃う (順列は問わない)。SoA -> AoS OK\n", PE);
        } else {
            fprintf(stderr, "  FAIL: struct/粒子 壊れ=%d 重複=%d 欠け=%d", bad, dupp, missp);
            if (use_id) fprintf(stderr, " / id 壊れ=%d 重複=%d 欠け=%d", badid, dupid, missid);
            fprintf(stderr, "\n"); rc = 1;
        }
        free(seen_p); free(seen_id);
    }

done:
    if (ib) mnc2_free_host_buffer(dev, ib, BYTES);
    if (ob) mnc2_free_host_buffer(dev, ob, BYTES);
    mnc2_close(dev);
    return rc ? 1 : 0;
}
