/* run_idma.c — 任意の idma.dat を libgpfn3 直接で実行
 *
 * オプション:
 *   --capture X:N    Capture X (B|C|D|E) を threshold N で arm
 *                    (gpfn3_capture_set_inst)。複数指定可。
 *                    指定時のみ終端で snapshot を "# ..." で出力
 *   --no-reset       device open 直後の gpfn3_reset_device を省略
 *   WD1 WD2 ...      BAR2 0x70 (WDBIT_PCIe) を busy-poll し、
 *                    各 WD ビットが 1 → 0 に遷移した時刻を記録
 *
 * Usage:
 *   run-idma PATH.idma.dat [--no-reset] [--capture X:N]* [WD ...]
 *
 * VSM 実行中の中間シグナルを取得するための probe ツール。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <gpfn3.h>

#define WDBIT_PCIE     0x70
#define MAX_WDS        64         /* WD 0..63 */
#define MAX_CAPTURES   4          /* B/C/D/E の 4 個まで */
#define DEFAULT_TIMEOUT_SEC 10    /* --timeout 省略時のデフォルト秒 */

/* 各 checkpoint で 3 つの stamp を同時に記録する:
     wall_ns : host の CLOCK_MONOTONIC (ns)
     inst    : chip 命令カウンタ (gpfn3_capture_get_inst)
     all_nop : chip all_nop カウンタ (gpfn3_capture_get_all_nop)
   差分を取ると (inst_B - inst_A) - (all_nop_B - all_nop_A) で nop 占有
   時間を除外した実質 chip cycle 数、wall_ns 差とあわせて host polling
   jitter + PIO read latency の見積もりに使える。 */

struct event {
    uint64_t id_bit;
    uint64_t wall_ns;
    uint64_t inst;
    uint64_t all_nop;
};

struct capture_spec {
    enum GPFN3_CAPTURE_SEL sel;
    uint64_t threshold;
    char     letter;   /* 'B'/'C'/'D'/'E' — 出力ラベル用 */
};

static volatile sig_atomic_t g_timeout_fired = 0;

static void on_sigalrm(int sig)
{
    (void)sig;
    g_timeout_fired = 1;
}

static inline void now_stamp(gpfn3_device_id_t dev,
                             uint64_t* wall_ns,
                             uint64_t* inst,
                             uint64_t* all_nop)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *wall_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    gpfn3_capture_get_inst(dev, inst);
    gpfn3_capture_get_all_nop(dev, all_nop);
}

/* Linux 環境情報を "# key: value" 形式で stdout に出力する。
   event 行 (0xLL ... / 0xKK ... / 0xNN ...) と区別するため先頭 '#' を
   付けるので、consumer (delta.py 等) は `#` 行をスキップすること。 */
static void print_linux_info(FILE* fp)
{
    struct utsname un;
    if (uname(&un) == 0) {
        fprintf(fp, "# uname: %s %s %s %s\n",
                un.sysname, un.release, un.version, un.machine);
        fprintf(fp, "# host: %s\n", un.nodename);
    }

    FILE* cpufp = fopen("/proc/cpuinfo", "r");
    if (cpufp) {
        char line[512];
        while (fgets(line, sizeof(line), cpufp)) {
            if (strncmp(line, "model name", 10) == 0) {
                const char* colon = strchr(line, ':');
                if (colon) {
                    const char* p = colon + 1;
                    while (*p == ' ' || *p == '\t') p++;
                    fprintf(fp, "# cpu_model: %s", p);
                }
                break;
            }
        }
        fclose(cpufp);
    }

    struct timespec res;
    if (clock_getres(CLOCK_MONOTONIC, &res) == 0) {
        fprintf(fp, "# clock_monotonic_res_ns: %llu\n",
                (unsigned long long)((uint64_t)res.tv_sec * 1000000000ull
                                     + (uint64_t)res.tv_nsec));
    }

    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    if (nproc > 0) {
        fprintf(fp, "# nproc: %ld\n", nproc);
    }
}

/* "X:N" 形式を parse (X in B/C/D/E, N is uint64 dec/hex)。
   成功時 0、失敗時 -1。*/
static int parse_capture_spec(const char* s, struct capture_spec* out)
{
    if (s == NULL || strlen(s) < 3 || s[1] != ':')
        return -1;

    enum GPFN3_CAPTURE_SEL sel;
    char letter;
    switch (s[0]) {
        case 'B': case 'b': sel = GPFN3_CAPTURE_B; letter = 'B'; break;
        case 'C': case 'c': sel = GPFN3_CAPTURE_C; letter = 'C'; break;
        case 'D': case 'd': sel = GPFN3_CAPTURE_D; letter = 'D'; break;
        case 'E': case 'e': sel = GPFN3_CAPTURE_E; letter = 'E'; break;
        default: return -1;
    }

    char* end = NULL;
    uint64_t thr = strtoull(s + 2, &end, 0);
    if (end == s + 2 || *end != '\0')
        return -1;

    out->sel = sel;
    out->letter = letter;
    out->threshold = thr;
    return 0;
}

/* .idma.dat ファイルを読んで DMA memory に置く。成功時 ptr を返し、
   *out_size に byte size を入れる。呼び出し側が gpfn3_free_dma_memory で
   解放すること。失敗時 NULL。

   gpfn3_kick_inst_dma は size % 128 == 0 を assert するので、事前に
   128-byte 境界チェックをする。 */
static void* load_kernel_file(gpfn3_device_id_t dev,
                              const char* path, size_t* out_size)
{
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "FAIL: fopen(%s)\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);
    if (fsize <= 0 || (fsize % 128) != 0) {
        fprintf(stderr, "FAIL: %s size %ld is not a positive multiple of 128\n",
                path, fsize);
        fclose(fp);
        return NULL;
    }
    size_t size = (size_t)fsize;

    void* dma = gpfn3_allocate_dma_memory(dev, size);
    if (dma == NULL) {
        fprintf(stderr, "FAIL: gpfn3_allocate_dma_memory(%zu)\n", size);
        fclose(fp);
        return NULL;
    }

    size_t nread = fread(dma, 1, size, fp);
    fclose(fp);
    if (nread != size) {
        fprintf(stderr, "FAIL: fread %zu / %zu\n", nread, size);
        gpfn3_free_dma_memory(dev, dma, size);
        return NULL;
    }

    *out_size = size;
    return dma;
}

static void usage(FILE* fp)
{
    fprintf(fp,
        "Usage: run-idma PATH.idma.dat [--no-reset] [--timeout SEC] "
        "[--capture X:N]* [WD ...]\n"
        "\n"
        "  PATH.idma.dat   assemble3 --loader 生成の idma.dat\n"
        "  --no-reset      device open 直後の gpfn3_reset_device を省略\n"
        "  --timeout SEC   reset 直後に SIGALRM ベースの timeout を仕掛ける\n"
        "                  (default %d sec、0 = 無限、busy-poll で検知)\n"
        "  --capture X:N   Capture X (B|C|D|E) を threshold N で arm。複数可。\n"
        "                  指定時のみ終端で '# Capture X snapshot:' を出力\n"
        "  WD              WD 番号 (0..%d) を複数指定すると kick 後に\n"
        "                  BAR2 0x%02x (WDBIT_PCIe) を busy-poll して\n"
        "                  各 WD の立ち下がり時刻を記録・表示する\n",
        DEFAULT_TIMEOUT_SEC, MAX_WDS - 1, WDBIT_PCIE);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    const char* path = NULL;
    int no_reset = 0;
    int timeout_sec = DEFAULT_TIMEOUT_SEC;
    uint64_t target_mask = 0;

    struct capture_spec captures[MAX_CAPTURES];
    int n_captures = 0;

    /* argv[1..] を scan */
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--no-reset") == 0) {
            no_reset = 1;
            continue;
        }

        if (strcmp(arg, "--timeout") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "FAIL: missing value after --timeout\n");
                return 1;
            }
            char* end = NULL;
            long t = strtol(argv[i], &end, 0);
            if (end == argv[i] || *end != '\0' || t < 0) {
                fprintf(stderr,
                    "FAIL: invalid --timeout value '%s' (expect non-negative integer)\n",
                    argv[i]);
                return 1;
            }
            timeout_sec = (int)t;
            continue;
        }

        if (strncmp(arg, "--timeout=", 10) == 0) {
            char* end = NULL;
            long t = strtol(arg + 10, &end, 0);
            if (end == arg + 10 || *end != '\0' || t < 0) {
                fprintf(stderr,
                    "FAIL: invalid --timeout value '%s'\n", arg + 10);
                return 1;
            }
            timeout_sec = (int)t;
            continue;
        }

        if (strcmp(arg, "--capture") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "FAIL: missing spec after --capture\n");
                return 1;
            }
            if (n_captures >= MAX_CAPTURES) {
                fprintf(stderr, "FAIL: too many --capture (max %d)\n",
                        MAX_CAPTURES);
                return 1;
            }
            if (parse_capture_spec(argv[i], &captures[n_captures]) < 0) {
                fprintf(stderr,
                    "FAIL: invalid --capture spec '%s' (expect X:N, X in B/C/D/E)\n",
                    argv[i]);
                return 1;
            }
            n_captures++;
            continue;
        }

        if (strncmp(arg, "--capture=", 10) == 0) {
            if (n_captures >= MAX_CAPTURES) {
                fprintf(stderr, "FAIL: too many --capture (max %d)\n",
                        MAX_CAPTURES);
                return 1;
            }
            if (parse_capture_spec(arg + 10, &captures[n_captures]) < 0) {
                fprintf(stderr,
                    "FAIL: invalid --capture spec '%s' (expect X:N, X in B/C/D/E)\n",
                    arg + 10);
                return 1;
            }
            n_captures++;
            continue;
        }

        if (arg[0] == '-' && arg[1] == '-') {
            fprintf(stderr, "FAIL: unknown option '%s'\n", arg);
            usage(stderr);
            return 1;
        }

        /* positional: 最初が path、残りが WD 番号 */
        if (path == NULL) {
            path = arg;
            continue;
        }

        char* end = NULL;
        unsigned long wd = strtoul(arg, &end, 0);
        if (end == arg || *end != '\0' || wd >= MAX_WDS) {
            fprintf(stderr, "FAIL: invalid WD '%s' (must be 0..%d)\n",
                    arg, MAX_WDS - 1);
            return 1;
        }
        uint64_t m = 1ull << wd;
        if (target_mask & m) continue;  /* dedupe */
        target_mask |= m;
    }

    if (path == NULL) {
        usage(stderr);
        return 1;
    }

    /* event 記録用: LL + KK + 各 WD drop */
    struct event event_load;
    struct event event_kick;
    struct event events[MAX_WDS];
    unsigned int n_events = 0;

    int num = 0;
    gpfn3_device_id_t dev = gpfn3_get_device_id(num);
    if (dev == GPFN3_INVALID_DEVICE_ID) {
        fprintf(stderr, "FAIL: gpfn3_get_device_id(%d)\n", num);
        return 1;
    }

    gpfn3_error_t err;

    if (!no_reset) {
        /* device open 直後に software reset を発行して 0x70 を
           0xfffffffffffffffe にリセットする。reset は apb/pio write と
           衝突するので前後に 1 ms の usleep を入れて落ち着かせる。 */
        usleep(1000);
        err = gpfn3_reset_device(dev);
        if (err != GPFN3_SUCCESS) {
            fprintf(stderr, "FAIL: gpfn3_reset_device rc=%d\n", (int)err);
            sleep(1);
            gpfn3_close_device(dev);
            return 1;
        }
        usleep(1000);
    }

    /* Reset 直後に SIGALRM ベースの timeout を仕掛ける。0 のときは
       無限待ち (alarm を呼ばない)。busy-poll の各 iteration で
       g_timeout_fired を check して break する。 */
    if (timeout_sec > 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_sigalrm;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;  /* not SA_RESTART: read_pio 等を中断したい */
        if (sigaction(SIGALRM, &sa, NULL) != 0) {
            fprintf(stderr, "FAIL: sigaction(SIGALRM)\n");
            gpfn3_close_device(dev);
            return 1;
        }
        alarm((unsigned int)timeout_sec);
    }

    /* Capture arm (--capture で指定された分) */
    for (int i = 0; i < n_captures; i++) {
        err = gpfn3_capture_set_inst(dev, captures[i].sel,
                                     captures[i].threshold);
        if (err != GPFN3_SUCCESS) {
            fprintf(stderr,
                "FAIL: gpfn3_capture_set_inst(%c, %llu) rc=%d\n",
                captures[i].letter,
                (unsigned long long)captures[i].threshold, (int)err);
            gpfn3_close_device(dev);
            return 1;
        }
    }

    /* load_kernel_file 直前 */
    now_stamp(dev, &event_load.wall_ns, &event_load.inst, &event_load.all_nop);

    size_t ksize = 0;
    void* kdma = load_kernel_file(dev, path, &ksize);
    if (kdma == NULL) {
        gpfn3_close_device(dev);
        return 1;
    }

    /* gpfn3_kick_inst_dma 直前 */
    now_stamp(dev, &event_kick.wall_ns, &event_kick.inst, &event_kick.all_nop);

    err = gpfn3_kick_inst_dma(dev, kdma, ksize);
    if (err != GPFN3_SUCCESS) {
        fprintf(stderr, "FAIL: gpfn3_kick_inst_dma rc=%d\n", (int)err);
        gpfn3_free_dma_memory(dev, kdma, ksize);
        gpfn3_close_device(dev);
        return 1;
    }

    /* IDMA 完了待ち: PIO 0x018 (IDMA_STAT)
       Phase 1: kick 反映を確認 (n_dma + inst_in_ibuf > 0)
       Phase 2: 完了 (bits[20:0] が 0) を待つ
       n_dma のみチェックだと kick 反映前の 0 を拾って即抜ける race がある。 */
    {
        for (int i = 0; i < 100; i++) {
            uint64_t rdt = 0;
            gpfn3_error_t werr = gpfn3_read_pio(dev, 0x018, &rdt);
            if (werr != GPFN3_SUCCESS) {
                fprintf(stderr, "FAIL: idma_wait gpfn3_read_pio rc=%d\n",
                        (int)werr);
                break;
            }
            if ((rdt & 0x1fffffULL) != 0) break;
        }
        int cnt = 0;
        const int max_cnt = 5000;
        while (cnt < max_cnt) {
            uint64_t rdt = 0;
            gpfn3_error_t werr = gpfn3_read_pio(dev, 0x018, &rdt);
            if (werr != GPFN3_SUCCESS) {
                fprintf(stderr, "FAIL: idma_wait gpfn3_read_pio rc=%d\n",
                        (int)werr);
                break;
            }
            if ((rdt & 0x1fffffULL) == 0)
                break;
            cnt++;
        }
        if (cnt == max_cnt)
            fprintf(stderr, "WARN: idma_wait timeout (IDMA_STAT busy)\n");
    }

    /* WD busy-poll: 指定された各 WD のビットが 0 に落ちたら時刻を記録 */
    uint64_t pending = target_mask;
    while (pending) {
        if (g_timeout_fired) {
            fprintf(stderr,
                "WARN: timeout %d sec fired, pending WDs=0x%016llx\n",
                timeout_sec, (unsigned long long)pending);
            break;
        }
        uint64_t wdbit = 0;
        gpfn3_error_t rerr = gpfn3_read_pio(dev, WDBIT_PCIE, &wdbit);
        if (rerr != GPFN3_SUCCESS) {
            fprintf(stderr, "FAIL: gpfn3_read_pio(0x%02x) rc=%d\n",
                    WDBIT_PCIE, (int)rerr);
            break;
        }
        uint64_t hit_bit = (wdbit & pending) ^ pending;
        if ( hit_bit ) {
            struct event *ep = &events[n_events];
            now_stamp(dev, &ep->wall_ns, &ep->inst, &ep->all_nop);
            ep->id_bit = hit_bit;
            pending &= ~hit_bit;
            n_events++;
        }
    }

    /* Capture snapshot 読み取り (close_device の前) */
    struct gpfn3_capture_t caps[MAX_CAPTURES];
    int caps_ok[MAX_CAPTURES] = {0};
    for (int i = 0; i < n_captures; i++) {
        memset(&caps[i], 0, sizeof(caps[i]));
        err = gpfn3_capture_read(dev, captures[i].sel, &caps[i]);
        caps_ok[i] = (err == GPFN3_SUCCESS) ? 1 : 0;
        if (!caps_ok[i]) {
            fprintf(stderr, "FAIL: gpfn3_capture_read(%c) rc=%d\n",
                    captures[i].letter, (int)err);
        }
    }

    gpfn3_free_dma_memory(dev, kdma, ksize);
    gpfn3_close_device(dev);

    /* WD 指定時のみ event table を出力 */
    if (target_mask) {
        print_linux_info(stdout);
        printf("0xLL %llu %llu %llu\n",
               (long long unsigned int)event_load.wall_ns,
               (long long unsigned int)event_load.inst,
               (long long unsigned int)event_load.all_nop);
        printf("0xKK %llu %llu %llu\n",
               (long long unsigned int)event_kick.wall_ns,
               (long long unsigned int)event_kick.inst,
               (long long unsigned int)event_kick.all_nop);

        for (unsigned int i = 0; i < n_events; i++) {
            struct event *ep = &events[i];
            uint64_t hit_bit = ep->id_bit;
            for(unsigned int id = 0; id < MAX_WDS ; id++ ) {
                if ( hit_bit & 1 ) {
                    printf("0x%02x %llu %llu %llu\n", id,
                           (long long unsigned int)ep->wall_ns,
                           (long long unsigned int)ep->inst,
                           (long long unsigned int)ep->all_nop);
                }
                hit_bit >>= 1;
                if ( hit_bit == 0 ) {
                    break;
                }
            }
        }
    }

    /* --capture 指定時のみ Capture snapshot を出力 ('#' 接頭辞) */
    if (n_captures > 0) {
        for (int i = 0; i < n_captures; i++) {
            if (!caps_ok[i]) continue;
            struct gpfn3_capture_t* cap = &caps[i];
            printf("# Capture %c snapshot (threshold=%llu):\n",
                   captures[i].letter,
                   (unsigned long long)captures[i].threshold);
            printf("#   all_nop   = %llu\n",
                   (unsigned long long)cap->all_nop);
            printf("#   empty_nop = %llu\n",
                   (unsigned long long)cap->empty_nop);
            printf("#   power_nop = %llu\n",
                   (unsigned long long)cap->power_nop);
            for (int j = 1; j < 256; j++) {
                if (cap->tag[j] != 0) {
                    printf("#   tag[0x%02x] = %llu\n",
                           j, (unsigned long long)cap->tag[j]);
                }
            }
        }
    }

    return 0;
}
