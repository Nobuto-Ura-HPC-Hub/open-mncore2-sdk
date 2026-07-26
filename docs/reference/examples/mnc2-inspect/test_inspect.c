#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mnc2_kernel_inspect.h"

static void set_bit(uint8_t *block, int pos)
{
    block[pos / 8] |= (uint8_t)(1 << (pos % 8));
}

static void set_field(uint8_t *block, int pos, int nbits, unsigned val)
{
    for (int i = 0; i < nbits; i++) {
        if (val & (1u << i))
            set_bit(block, pos + i);
    }
}

static void set_imode(uint8_t *block, int mode)
{
    set_field(block, 1022, 2, (unsigned)mode);
}

static void test_imode(void)
{
    uint8_t block[128];

    memset(block, 0, sizeof(block));
    set_imode(block, 0);
    assert(mnc2_block_imode(block) == MNC2_IMODE_FLAT);

    memset(block, 0, sizeof(block));
    set_imode(block, 1);
    assert(mnc2_block_imode(block) == MNC2_IMODE_AUTO);

    memset(block, 0, sizeof(block));
    set_imode(block, 2);
    assert(mnc2_block_imode(block) == MNC2_IMODE_LOOP);

    memset(block, 0, sizeof(block));
    set_imode(block, 3);
    assert(mnc2_block_imode(block) == -1);

    assert(mnc2_block_imode(NULL) == -1);

    printf("  test_imode: PASS\n");
}

static void test_tag_wd_auto(void)
{
    uint8_t block[128];
    memset(block, 0, sizeof(block));
    set_imode(block, 1);

    set_field(block, 292, 8, 0x42);
    assert(mnc2_block_tag_wd(block, 0) == 0x42);

    set_field(block, 596, 8, 0x10);
    assert(mnc2_block_tag_wd(block, 1) == 0x10);

    set_field(block, 900, 8, 0xFF);
    assert(mnc2_block_tag_wd(block, 2) == 0xFF);

    assert(mnc2_block_tag_wd(block, 3) == -1);
    assert(mnc2_block_tag_wd(block, -1) == -1);

    printf("  test_tag_wd_auto: PASS\n");
}

static void test_tag_wd_flat(void)
{
    uint8_t block[128];
    memset(block, 0, sizeof(block));
    set_imode(block, 0);

    set_field(block, 270, 8, 0x23);
    assert(mnc2_block_tag_wd(block, 0) == 0x23);

    set_field(block, 702, 8, 0x19);
    assert(mnc2_block_tag_wd(block, 1) == 0x19);

    assert(mnc2_block_tag_wd(block, 2) == -1);

    printf("  test_tag_wd_flat: PASS\n");
}

static void test_mvid_auto(void)
{
    uint8_t block[128];
    memset(block, 0, sizeof(block));
    set_imode(block, 1);

    set_field(block, 935, 8, 0xAB);
    assert(mnc2_block_mvid(block) == 0xAB);

    printf("  test_mvid_auto: PASS\n");
}

static void test_mvid_flat(void)
{
    uint8_t block[128];
    memset(block, 0, sizeof(block));
    set_imode(block, 0);

    set_field(block, 911, 8, 0x7F);
    assert(mnc2_block_mvid(block) == 0x7F);

    printf("  test_mvid_flat: PASS\n");
}

static void test_mvid_loop(void)
{
    uint8_t block[128];
    memset(block, 0, sizeof(block));
    set_imode(block, 2);

    assert(mnc2_block_mvid(block) == -1);

    printf("  test_mvid_loop: PASS\n");
}

static void test_scan_tags(void)
{
    uint8_t data[256];
    memset(data, 0, sizeof(data));

    set_imode(data, 1);
    set_field(data, 292, 8, 5);
    set_field(data, 900, 8, 10);
    set_field(data, 935, 8, 20);

    set_imode(data + 128, 0);
    set_field(data + 128, 270, 8, 0x23);
    set_field(data + 128, 911, 8, 0x19);

    uint8_t tags_wd[16], tags_mv[16];
    int wd_count, mv_count;

    int rc = mnc2_idma_scan_tags(data, 256, tags_wd, tags_mv, 16,
                                 &wd_count, &mv_count);
    assert(rc == 0);

    assert(wd_count == 3);
    assert(tags_wd[0] == 5);
    assert(tags_wd[1] == 10);
    assert(tags_wd[2] == 0x23);

    assert(mv_count == 2);
    assert(tags_mv[0] == 20);
    assert(tags_mv[1] == 0x19);

    printf("  test_scan_tags: PASS\n");
}

static void test_scan_tags_errors(void)
{
    uint8_t data[128];
    uint8_t tags_wd[16], tags_mv[16];
    int wd_count, mv_count;

    assert(mnc2_idma_scan_tags(NULL, 128, tags_wd, tags_mv, 16,
                               &wd_count, &mv_count) == -1);

    assert(mnc2_idma_scan_tags(data, 100, tags_wd, tags_mv, 16,
                               &wd_count, &mv_count) == -1);

    assert(mnc2_idma_scan_tags(data, 128, tags_wd, tags_mv, 0,
                               &wd_count, &mv_count) == -1);

    printf("  test_scan_tags_errors: PASS\n");
}

static void test_tag_wd_zero(void)
{
    uint8_t block[128];
    memset(block, 0, sizeof(block));
    set_imode(block, 1);

    assert(mnc2_block_tag_wd(block, 0) == 0);

    printf("  test_tag_wd_zero: PASS\n");
}

int main(void)
{
    printf("mnc2_kernel_inspect tests:\n");
    test_imode();
    test_tag_wd_auto();
    test_tag_wd_flat();
    test_tag_wd_zero();
    test_mvid_auto();
    test_mvid_flat();
    test_mvid_loop();
    test_scan_tags();
    test_scan_tags_errors();
    printf("All tests PASSED.\n");
    return 0;
}
