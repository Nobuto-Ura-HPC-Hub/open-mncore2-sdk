// 03-vecadd-2d-4096: c[i][j] = a[i][j] + b[i][j] (2D, 64x64 = 4096 要素)
//
// gcc input.c -o test -lm && ./test   で普通の C として動作する。
// openacc2mncore.sh input.c vecadd    で MN-Core 2 向けに変換する。

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define NI 64
#define NJ 64
#define TOTAL (NI * NJ)

int main(void)
{
    double a[NI][NJ], b[NI][NJ], c[NI][NJ];

    for (int i = 0; i < NI; i++)
        for (int j = 0; j < NJ; j++) {
            a[i][j] = (double)(i * NJ + j + 1);
            b[i][j] = (double)(i * NJ + j + 1) * 10.0;
        }

    // @vecadd_2d {
    #pragma acc parallel loop collapse(2)
    for (int i = 0; i < NI; i++)
        for (int j = 0; j < NJ; j++)
            c[i][j] = a[i][j] + b[i][j];
    // @vecadd_2d }

    for (int i = 0; i < NI; i++)
        for (int j = 0; j < NJ; j++)
            printf("c[%d] = %.15g\n", i * NJ + j, c[i][j]);

    int errors = 0;
    for (int i = 0; i < NI; i++)
        for (int j = 0; j < NJ; j++) {
            double expected = a[i][j] + b[i][j];
            if (fabs(c[i][j] - expected) > 1e-10) {
                if (errors < 5)
                    fprintf(stderr, "MISMATCH c[%d][%d]: got=%g expected=%g\n",
                            i, j, c[i][j], expected);
                errors++;
            }
        }

    if (errors > 0) {
        fprintf(stderr, "FAIL: %d/%d mismatches\n", errors, TOTAL);
        return 1;
    }
    fprintf(stderr, "PASS: c[i][j] == a[i][j] + b[i][j] for all %d elements\n", TOTAL);
    return 0;
}
