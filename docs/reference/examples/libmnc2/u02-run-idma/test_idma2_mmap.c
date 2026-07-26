/* test_idma2_mmap.c — BAR2 mmap 経由で IDMA2 kick を試すテストプログラム
 *
 * 目的: kernel driver の ioctl filter を bypass して、mmap 経由で直接
 * IDMA_ADR (0x020) と IDMA_KICK (0x010) に書き込み、IDMA2 が動くか検証。
 *
 * 前提:
 *   - DM (0x10000+) に descriptor が既に書き込まれている
 *     (先に ./run-idma2 を走らせて dm_setup まで実行した後)
 *   - Device-DRAM に kernel が既に書き込まれている
 *
 * 使い方:
 *   ./test-idma2-mmap [--dm-index IDX] [--dm-count CNT]
 *
 * kernel driver の定数 (from linux/gpfn3.h):
 *   GPFN3_BAR2_SIZE              = 0x800000    (8 MiB)
 *   GPFN3_MMAP_OFFSET_GPFN3_REGS = PAGE_SIZE * 0x101
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define BAR2_SIZE        0x800000UL
#define BAR2_MMAP_OFFSET (4096UL * 0x101UL)

/* register offsets (BAR2 内) */
#define IDMA_KICK   0x010
#define IDMA_STAT   0x018
#define IDMA_ADR    0x020
#define DM_BASE     0x10000

int main(int argc, char** argv)
{
    size_t dm_index = 0;
    size_t dm_count = 8;
    /* pod では /dev/mnc2p187s0 等のカスタム名、環境によっては /dev/gpfn3_0 */
    const char* dev_path = "/dev/mnc2p187s0";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dm-index") == 0 && i + 1 < argc)
            dm_index = (size_t)strtoul(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--dm-count") == 0 && i + 1 < argc)
            dm_count = (size_t)strtoul(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--dev") == 0 && i + 1 < argc)
            dev_path = argv[++i];
        else {
            fprintf(stderr, "Usage: test-idma2-mmap [--dm-index IDX] [--dm-count CNT] [--dev PATH]\n");
            return 1;
        }
    }

    /* ---- device open + BAR2 mmap ---- */
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    void* bar2 = mmap(NULL, BAR2_SIZE, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, BAR2_MMAP_OFFSET);
    if (bar2 == MAP_FAILED) {
        perror("mmap BAR2");
        close(fd);
        return 1;
    }

    volatile uint64_t* idma_adr  = (volatile uint64_t*)((char*)bar2 + IDMA_ADR);
    volatile uint64_t* idma_kick = (volatile uint64_t*)((char*)bar2 + IDMA_KICK);
    volatile uint64_t* idma_stat = (volatile uint64_t*)((char*)bar2 + IDMA_STAT);
    volatile uint64_t* dm_entry  = (volatile uint64_t*)((char*)bar2 + DM_BASE + dm_index * 8);

    /* ---- sanity: DM 内容の確認 ---- */
    uint64_t dm_val = *dm_entry;
    fprintf(stderr, "DM[%zu] = 0x%016lx\n", dm_index, (unsigned long)dm_val);
    if ((dm_val & (1ULL << 63)) == 0) {
        fprintf(stderr, "WARN: DM[%zu] valid bit が立っていない\n", dm_index);
    }

    /* ---- IDMA_ADR / IDMA_KICK 書き込み ---- */
    uint64_t addr = DM_BASE + dm_index * 8;
    uint64_t kick = (1ULL << 62) | dm_count;
    fprintf(stderr, "Writing IDMA_ADR(0x020)=0x%lx IDMA_KICK(0x010)=0x%lx via mmap\n",
            (unsigned long)addr, (unsigned long)kick);

    *idma_adr = addr;
    __sync_synchronize();
    *idma_kick = kick;
    __sync_synchronize();

    /* ---- IDMA 完了待ち (Phase 1 開始確認 + Phase 2 完了確認) ---- */
    int started = 0;
    for (int i = 0; i < 1000; i++) {
        uint64_t stat = *idma_stat;
        if ((stat & 0x1fffffULL) != 0) { started = 1; break; }
    }
    if (!started) {
        fprintf(stderr, "WARN: IDMA_STAT が動き始めない\n");
    }

    int done = 0;
    for (int i = 0; i < 5000; i++) {
        uint64_t stat = *idma_stat;
        if ((stat & 0x1fffffULL) == 0) { done = 1; break; }
    }

    uint64_t final_stat = *idma_stat;
    fprintf(stderr, "Final IDMA_STAT = 0x%016lx %s\n",
            (unsigned long)final_stat,
            done ? "(completed)" : "(timeout)");

    munmap(bar2, BAR2_SIZE);
    close(fd);
    return done ? 0 : 1;
}
