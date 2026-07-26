// 05-vecadd-3d-4096: c[i][j][k] = a[i][j][k] + b[i][j][k] (3D, 16x16x16 = 4096 要素)
//
// gcc input.c -o test -lm && ./test   で普通の C として動作する。
// openacc2mncore.sh input.c vecadd    で MN-Core 2 向けに変換する。

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define NI 16
#define NJ 16
#define NK 16
#define TOTAL (NI * NJ * NK)

int main(void)
{
    double a[NI][NJ][NK], b[NI][NJ][NK], c[NI][NJ][NK];

    for (int i = 0; i < NI; i++)
        for (int j = 0; j < NJ; j++)
            for (int k = 0; k < NK; k++) {
                a[i][j][k] = (double)((i * NJ + j) * NK + k + 1);
                b[i][j][k] = (double)((i * NJ + j) * NK + k + 1) * 10.0;
            }

    // @vecadd_3d {
    #pragma acc parallel loop collapse(3)
    for (int i = 0; i < NI; i++)
        for (int j = 0; j < NJ; j++)
            for (int k = 0; k < NK; k++)
                c[i][j][k] = a[i][j][k] + b[i][j][k];
    // @vecadd_3d }

    for (int i = 0; i < NI; i++)
        for (int j = 0; j < NJ; j++)
            for (int k = 0; k < NK; k++)
                printf("c[%d] = %.15g\n", (i * NJ + j) * NK + k, c[i][j][k]);

    int errors = 0;
    for (int i = 0; i < NI; i++)
        for (int j = 0; j < NJ; j++)
            for (int k = 0; k < NK; k++) {
                double expected = a[i][j][k] + b[i][j][k];
                if (fabs(c[i][j][k] - expected) > 1e-10) {
                    if (errors < 5)
                        fprintf(stderr, "MISMATCH c[%d][%d][%d]: got=%g expected=%g\n",
                                i, j, k, c[i][j][k], expected);
                    errors++;
                }
            }

    if (errors > 0) {
        fprintf(stderr, "FAIL: %d/%d mismatches\n", errors, TOTAL);
        return 1;
    }
    fprintf(stderr, "PASS: c[i][j][k] == a[i][j][k] + b[i][j][k] for all %d elements\n", TOTAL);
    return 0;
}
