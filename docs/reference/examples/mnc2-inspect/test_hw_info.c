#include <assert.h>
#include <stdio.h>

#include "mnc2_hw_info.h"

static void test_get_hw_info(void)
{
    mnc2_hw_info_t info;
    int rc = mnc2_get_hw_info(&info);
    assert(rc == 0);

    assert(info.ngroups == 4);
    assert(info.nl2b_per_group == 2);
    assert(info.nl1b_per_l2b == 8);
    assert(info.nmab_per_l1b == 16);
    assert(info.npe_per_mab == 4);
    assert(info.total_pe == 4096);
    assert(info.total_pe == info.ngroups * info.nl2b_per_group
                          * info.nl1b_per_l2b * info.nmab_per_l1b
                          * info.npe_per_mab);

    assert(info.pdm_size_lw == 524288);
    assert(info.dram_size_lw == 536870912);
    assert(info.l2bm_size_lw == 32768);
    assert(info.lm_size_lw == 2048);
    assert(info.grf_size_lw == 256);
    assert(info.nlm_per_pe == 2);
    assert(info.ngrf_per_pe == 2);

    assert(info.pdm_size_bytes == 4194304);
    assert(info.pdm_size_bytes == info.pdm_size_lw * 8);
    assert(info.pdm_align_bytes == 8);
    assert(info.ddma_unit_bytes == 4);
    assert(info.ddma_max_bytes == 4194304);
    assert(info.dmaid_max == 63);
    assert(info.wd_max == 63);

    printf("  test_get_hw_info: PASS\n");
}

static void test_null(void)
{
    assert(mnc2_get_hw_info(NULL) == -1);
    printf("  test_null: PASS\n");
}

int main(void)
{
    printf("mnc2_hw_info tests:\n");
    test_get_hw_info();
    test_null();
    printf("All tests PASSED.\n");
    return 0;
}
