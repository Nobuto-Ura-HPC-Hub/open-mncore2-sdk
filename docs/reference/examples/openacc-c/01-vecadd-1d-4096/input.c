// 01-vecadd-1d-4096: c[i] = a[i] + b[i] (1D, 4096 要素)
//
// gcc input.c -o test -lm && ./test   で普通の C として動作する。
// openacc2mncore.sh input.c vecadd    で MN-Core 2 向けに変換する。

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define N 4096

int main(void)
{
    double a[N], b[N], c[N];

    // プロローグ: テストデータ初期化
    for (int i = 0; i < N; i++) {
        a[i] = (double)(i + 1);
        b[i] = (double)(i + 1) * 10.0;
    }

    // @vecadd_1d {
    #pragma acc parallel loop
    for (int i = 0; i < N; i++) {
        c[i] = a[i] + b[i];
    }
    // @vecadd_1d }

    // エピローグ: 配列ダンプ + 結果検証
    for (int i = 0; i < N; i++)
        printf("c[%d] = %.15g\n", i, c[i]);

    int errors = 0;
    for (int i = 0; i < N; i++) {
        double expected = a[i] + b[i];
        if (fabs(c[i] - expected) > 1e-10) {
            if (errors < 5)
                fprintf(stderr, "MISMATCH c[%d]: got=%g expected=%g\n", i, c[i], expected);
            errors++;
        }
    }

    if (errors > 0) {
        fprintf(stderr, "FAIL: %d/%d mismatches\n", errors, N);
        return 1;
    }
    fprintf(stderr, "PASS: c[i] == a[i] + b[i] for all %d elements\n", N);
    return 0;
}
