# libmnc2 C API リファレンス (0.4.3)

vsm-linker / MNCL 層から呼ばれるホストフレームワークの C API 一覧。

**設計原則: バックエンドの隠蔽**

3 種類のバックエンド（実機・emu:process・emu:lib）を提供する。
emu 統合ビルドでは `-lmnc2 -lgpfn3` でリンクする。backend はリンクされた libgpfn3.so で決まる。
vsm-linker やアプリケーションコードはバックエンドの違いを意識しない。

```c
// すべてのバックエンドで同じ API
mnc2_device_t dev = mnc2_open(0);

mnc2_send(dev, buf, offset, size, send_wait_tag);
mnc2_exec_kernel(kernel);
mnc2_recv(dev, buf, offset, size, recv_wait_tag);  // emu では auto-flush
mnc2_close(dev);
```

## 層構造における位置

```
OpenACC / FORTRAN / C アプリケーション
  ↓
MNCL (_vsm + .param 生成)
  ↓
vsm-linker (fpass2 + assemble3 → .idma.dat)
  ↓ .idma.dat + データ + PDM0 アドレス (呼び出し側が指定)
libmnc2 (本 API) ← ここ
  ↓ バックエンド分岐
  ├─ [実機]        → MN-Core 2 デバイス
  ├─ [emu:process] → エミュレータ (外部プロセス)
  └─ [emu:lib]     → エミュレータ (ライブラリリンク)
```

## 3 つのバックエンド

| バックエンド | ライブラリ | 特徴 |
|-------------|-----------|------|
| 実機 | `libmnc2.a` (実機専用ビルド) | MN-Core 2 実機を直接制御 |
| emu:process | `libmnc2.a` (emu 統合ビルド) | エミュレータを外部プロセスとして起動 |
| emu:lib | `libmnc2.a` (emu 統合ビルド) | エミュレータをライブラリリンクで使用 |

SDK kit では emu 統合ビルドの `libmnc2.a` を提供する。
リンクは `-lmnc2 -lgpfn3 -lstdc++ -lrt -lm -fopenmp`。

## 環境変数

| 環境変数 | 必須 | 内容 |
|---------|------|------|
| `MNC2_EMU_CONFIG` | emu:process のみ | 下記参照 |

### MNC2_EMU_CONFIG の詳細

| バックエンド | 値 | 備考 |
|---|---|---|
| emu:process | エミュレータ設定 JSON のパス | `gpfn3_package_main -c` に渡される。`config_pdm0.json` 等 |
| emu:lib | (不要) | 設定しても使わない |
| 実機 | (不要) | 設定しても無視される |

emu:process では `gpfn3_package_main` が PATH に存在すること。

## DMA kick の sync model

| API | DMA 種別 | 動作 |
|---|---|---|
| `mnc2_send` | DDMA | kick + **完了 wait** (sync) |
| `mnc2_recv` | DDMA | kick + 完了 wait (sync) |
| `mnc2_exec_kernel` | IDMA | kernel の全片が積めるなら kick、積めなければ `MNC2_ERROR_BUSY` (**async**、待たない) |
| `mnc2_exec_kernel_blocking` | IDMA | 全片を kick し (キューが `idma_queue_limit` に達していれば空くのを待つ)、その後キューの残りが引数 `limit` 以下になるまで待つ (命令バッファの実行完了は待たない) |
| `mnc2_free_kernel` | IDMA | idle wait + buffer 解放 (sync) |

DDMA 系 (`send` / `recv`) は呼び出しごとに完了同期する。 連続 send で同じ buffer や regset を共有しても race にならない。

IDMA 系 (`exec_kernel`) は async で、 複数 exec を連続で打つと IDMA queue (depth 31) に積まれて並列処理される。 `free_kernel` で IDMA idle まで wait してから kernel buffer を解放する。

### 並列性のための明示 wait API

caller が完了タイミングを明示的に取りたい場合に使える:

```c
int mnc2_wait_ddma_idle(mnc2_device_t dev);
int mnc2_wait_idma_idle(mnc2_device_t dev);
```

- 戻り値: `MNC2_SUCCESS` / `MNC2_ERROR_TIMEOUT` / `MNC2_ERROR_PIO` / `MNC2_ERROR_DEVICE`
- timeout 上限は下記 max iter API で調整可

### caller 責任

`mnc2_send`:
- sync なので return 時には DMA 完了済 = caller は buffer をすぐ再利用してよい
- `send_wait_tag != 0` で kernel に消費させる pattern: kernel 側 `wait i_<tag>` がデータ到着待ちに使う
- `send_wait_tag == 0`: 拾う側無し、 純粋に host → PDM の sync 転送のみ

`mnc2_exec_kernel`:
- exec 後、 `free_kernel` を呼ぶまで kernel data を変更しない
- **キューが一杯なら `MNC2_ERROR_BUSY` が返る。ライブラリは待たない。** 連続 exec するときは
  この戻り値を見て、`mnc2_wait_idma_idle` で掃くか、`mnc2_get_idma_stat` でキュー長を見て
  空きができてから積み直す
- kernel が複数の片に分かれている場合 (下記「カーネル実行」)、`mnc2_exec_kernel` は全片が
  収まるときだけ積む。片数そのものが閾値を超える kernel は `MNC2_ERROR_QUEUE_LIMIT` を返す
  (やり直しても通らない)。そのような kernel は `mnc2_exec_kernel_blocking` で積む
- `mnc2_exec_kernel_blocking` はキューの空きと、戻るときのキューの残り数 (`limit`) を待つが、kernel の
  完了は待たない。kernel 内の `wait` が待つ `mnc2_send` は、この関数を呼ぶ前に済ませておく (後にすると、
  exec 内の待ちと send が互いを待つ。待ちの上限で `MNC2_ERROR_TIMEOUT` になる)
- **HW のキュー深さ 31 まで積んではいけない。** `n_dma` は 5 bit で 31 の次が 0 に戻り、
  そこまで積むと数え方が壊れて idle に戻らなくなることを実機で確認している。この状態に入ると
  `mnc2_wait_idma_idle` は成功しなくなる。ライブラリは既定 16 で断る (`mnc2_set_idma_queue_limit`
  で変更可)
- **効率的な待ち方**: `mnc2_wait_idma_idle` は queue が空になるまで待つため、連続 exec のたびに挟むと queue が毎回空になり、並列性が途切れて効率が落ちる。効率を保つには、`mnc2_get_idma_stat` で現在のキュー長を確認し、空きができ次第に次を積む。`mnc2_wait_idma_idle` は、実行順に依存関係があるときの同期や、最後にすべてを完了させる用途に使う
- 複数 kernel を並列 exec すると `free_kernel` は「全 IDMA が idle」 を待つので、 特定 kernel A の終了は保証しない (caller が直列化)

### 内部 max iter (PIO read 回数の上限)

| 名前 | 意味 | default | settable API |
|---|---|---|---|
| `ddma_kick_verify_max_cnt` | DDMA kick 反映確認 (= send / recv 内部) | 100 | `mnc2_{get,set}_ddma_kick_verify_max` |
| `ddma_complete_max_cnt` | DDMA 完了 (= send / recv / `mnc2_wait_ddma_idle`) | 2000 | `mnc2_{get,set}_ddma_complete_max` |
| `idma_complete_max_cnt` | IDMA idle (= free_kernel / `mnc2_wait_idma_idle`) | 5000 | `mnc2_{get,set}_idma_complete_max` |

これらは適当な値で、 将来「実機計測 → 倍にする」 or 「time-based」 への差し替えが望ましい。

### IDMA キューの上限 (`idma_queue_limit`)

| 名前 | 意味 | default | settable API |
|---|---|---|---|
| `idma_queue_limit` | IDMA キューに積んでよい片数の上限 (`n_dma` と比べる) | 16 | `mnc2_{get,set}_idma_queue_limit` |

上の表と違い、PIO read の回数ではなく `n_dma` の値そのものと比べる数である。
HW のキューは深さ 31 だが、HW は溢れを自分で防げず、31 を超えて積むと `n_dma` が一周して idle に戻らなくなる
(実機で確認)。そのためライブラリがキューの上限 `idma_queue_limit` を定めて、手前で止める。
既定の 16 は 31 に対して余裕を取った値で (kick 直後の `n_dma` の読み値が反映前で実際より少なく見えることへの
余裕を含む)、`mnc2_set_idma_queue_limit` で 1 から 31 の範囲で変更できる (範囲外は `MNC2_ERROR_PARAM` で値を変えない。
0.4.3 から。それ以前は検査していなかった)。

この値から次の 3 つが決まる。

- 待たない版 `mnc2_exec_kernel` が返すエラー: 片数がこの値を超える kernel は `MNC2_ERROR_QUEUE_LIMIT`、
  今のキューの数と片数の合計がこの値を超えるなら `MNC2_ERROR_BUSY`
- 待つ版 `mnc2_exec_kernel_blocking` が kick の途中で待つ条件: キューがこの値に達したら減るのを待つ
- 待つ版の引数 `limit` の範囲: 0 からこの値まで (`limit` の意味は下記「カーネル実行」)

## API 一覧

### デバイス管理

```c
mnc2_device_t mnc2_open(int device_number);
void mnc2_close(mnc2_device_t dev);
```

- `mnc2_open()`: リンクされた libgpfn3.so に対応する backend でデバイスをオープン
  - emu:process では `MNC2_EMU_CONFIG` (エミュレータ設定 JSON のパス) が設定されている必要がある。読むのは emu:process の `libgpfn3.so` で、libmnc2 自身は確認しない
  - 作業ディレクトリは `mkdtemp()` で自動生成され、`mnc2_close()` で自動削除される
- `mnc2_close()`: デバイスを閉じ、リソースを解放

```c
int mnc2_reset(mnc2_device_t dev);
```

- デバイス状態をリセット (`gpfn3_reset_device` を呼ぶ)
- 主用途: `mnc2_recv` の timeout 等で DMA queue に中途半端な DMA 設定が残った状態を解消し、次のテスト / 処理の前に clean state に戻す
- DMA タグの完了を記録する flag も消える (下記「DMA タグの指定」の「タグの規則」)。前のプログラムが flag を残した疑いがあるときに使う。自分が立てた flag の後始末の代わりには使わない
- emu backend では no-op で `MNC2_SUCCESS` を返す (state 引き継ぎが無い / emu:lib は現状 reset 相当の機能を公開していない)
- **PDM メモリ内容は保持される** (リセット対象は DMA queue と HW 制御レジスタ等、PDM コンテンツではない)
- 戻り値: `MNC2_SUCCESS` / `MNC2_ERROR_IO` (gpfn3_reset_device 失敗) / `MNC2_ERROR_PARAM` (dev が NULL)

```c
const char* mnc2_get_backend_name(mnc2_device_t dev);
```

- バックエンド名を文字列で返す: `"device"`, `"emu:process"`, `"emu:lib"`
- dev が NULL の場合は `"unknown"`
- 返すポインタは静的文字列リテラル（解放不要）

```c
int mnc2_get_backend(void);
```

- backend 種別を整数値で返す: `MNC2_BACKEND_DEVICE` / `MNC2_BACKEND_EMU_PROCESS` / `MNC2_BACKEND_EMU_LIB`
- libgpfn3.so がロードされていない場合は `MNC2_BACKEND_UNKNOWN` を返す (`mnc2_open()` がエラー終了する)
- dev ハンドル不要 (グローバル状態を読む)

```c
int mnc2_read_pio(mnc2_device_t dev, uint64_t addr, uint64_t* val_out);
int mnc2_write_pio(mnc2_device_t dev, uint64_t addr, uint64_t val);
```

- PIO レジスタの読み書き。**device backend 専用**。emu backend では `MNC2_ERROR_DEVICE` を返す
- `addr`: PIO アドレス (例: `0x038` = DDMA_STAT)
- 主用途: DDMA_STAT 読み出しによる DMA キュー状態の診断
- 戻り値: `MNC2_SUCCESS` / `MNC2_ERROR_DEVICE` (emu backend) / `MNC2_ERROR_PIO` (gpfn3 失敗) / `MNC2_ERROR_PARAM` (dev が NULL 等)

```c
int mnc2_wait_ddma_idle(mnc2_device_t dev);
int mnc2_wait_idma_idle(mnc2_device_t dev);
```

- DDMA / IDMA が idle になるまで wait。 caller の明示同期用
- 戻り値: `MNC2_SUCCESS` / `MNC2_ERROR_TIMEOUT` / `MNC2_ERROR_PIO` / `MNC2_ERROR_DEVICE`
- timeout の上限 iter は下記 `_complete_max` で調整可能

```c
int mnc2_get_ddma_kick_verify_max(mnc2_device_t dev);
int mnc2_set_ddma_kick_verify_max(mnc2_device_t dev, int cnt);
int mnc2_get_ddma_complete_max   (mnc2_device_t dev);
int mnc2_set_ddma_complete_max   (mnc2_device_t dev, int cnt);
int mnc2_get_idma_queue_limit(mnc2_device_t dev);
int mnc2_set_idma_queue_limit(mnc2_device_t dev, int limit);
int mnc2_get_idma_complete_max   (mnc2_device_t dev);
int mnc2_set_idma_complete_max   (mnc2_device_t dev, int cnt);
```

- 内部の max iter と閾値 (上記の表) の get / set
- `set_*` の戻り値は変更前の値、`get_*` の戻り値は現在値、`dev` が NULL のとき `-1`
- `mnc2_set_idma_queue_limit` は範囲 1 から 31 の外を `MNC2_ERROR_PARAM` で断り、値を変えない
- 主用途: タイムアウトが短すぎる場合の調整、 またはテストでの意図的タイムアウト誘発

### DMA バッファ管理

```c
void* mnc2_alloc_host_buffer(mnc2_device_t dev, size_t size);
void  mnc2_free_host_buffer(mnc2_device_t dev, void* buf, size_t size);
```

ホスト側の DMA バッファを確保する。バックエンドに応じた適切なメモリ確保が行われる。

### データ転送 (Host ↔ PDM0)

```c
int mnc2_send(mnc2_device_t dev, const void* buf,
              uint64_t pdm_offset, size_t size, unsigned int send_wait_tag);

int mnc2_recv(mnc2_device_t dev, void* buf,
              uint64_t pdm_offset, size_t size, unsigned int recv_wait_tag);
```

- **単位はすべてバイト**
- `pdm_offset`: PDM0 上のバイトオフセット。**8 の倍数**であること
- `size`: 転送サイズ (バイト)。**8 の倍数**かつ **> 0** であること
- `send_wait_tag` / `recv_wait_tag`: DMA wait タグ (done_flags / wait 用)。**0〜63** の範囲
- 戻り値: 0 = 成功、`MNC2_ERROR_PARAM` = パラメータ不正、その他負値 = 実行時エラー
- **エンディアン変換は `mnc2_set_endian_ctrl` で設定した値** (下記) に従う。呼び出し側で手動変換は不要
- **emu では recv 時に自動で flush される**。明示的な flush 呼び出しは不要

#### sync model (重要)

`mnc2_send` / `mnc2_recv` は両方とも **sync** (DMA 物理完了まで wait してから return)。 詳細は「[DMA kick の sync model](#dma-kick-の-sync-model)」 セクション参照。

**caller 注意 (`mnc2_send`):**
- return 時には DMA 完了済 = `buf` はすぐ再利用してよい
- `send_wait_tag != 0` のとき: kernel 側 `wait i_<tag>` がこのデータの到着を sync 待ちに使う pattern
- `send_wait_tag == 0` のとき: 純粋に host → PDM の sync 転送のみ

**caller 注意 (`mnc2_recv`):**
- `recv_wait_tag` の値に関わらず、 return 時には DMA が物理的に完了して `buf` にデータが入っている

### ENDIAN_CTRL 制御 (PIO 0x40 相当)

```c
int mnc2_set_endian_ctrl(mnc2_device_t dev, int endian_ctrl);
int mnc2_get_endian_ctrl(const mnc2_device_t dev);
```

- `endian_ctrl`: **0..4** の範囲。`mnc2_open` デフォルトは **4** (= host が PE レジスタ値をそのまま観測する設定)
- 全 3 バックエンド (device / emu:process / emu:lib) で対応済
- 戻り値: set は `MNC2_SUCCESS` / 負値、get は 0..4 / 負値

ENDIAN_CTRL 値の意味:

| 値 | 主な用途 | 備考 |
|----|---------|------|
| 0  | 特殊 (64-byte block 全 byte reverse) | 通常は使わない |
| 1  | identity (変換なし) | 生バイト列をそのまま転送したい場合 |
| 2  | f16 / bf16 | u16 単位のエンディアン変換 |
| 3  | f32 | u32 単位のエンディアン変換 |
| 4  | f64 (デフォルト) | u64 単位のエンディアン変換 |

デフォルト `4` では host から f64 値を送受信したとき、PE と host の間の
バイト順差分が自動で吸収される。他のデータ型を使うときはデータ転送前に
`mnc2_set_endian_ctrl` で切り替える。

### カーネル実行

```c
mnc2_kernel_t mnc2_load_kernel(mnc2_device_t dev, const char* path);
void           mnc2_free_kernel(mnc2_kernel_t kernel);
int            mnc2_exec_kernel(mnc2_kernel_t kernel);
int            mnc2_exec_kernel_blocking(mnc2_kernel_t kernel, int limit);
size_t         mnc2_get_kernel_piece_size(mnc2_device_t dev);
int            mnc2_set_kernel_piece_size(mnc2_device_t dev, size_t bytes);
int            mnc2_kernel_get_piece_count(mnc2_kernel_t kernel);
```

- `mnc2_load_kernel()`: カーネルバイナリをファイルから読み込む。**デバイス非依存** (OpenCL の `clCreateKernel` と同様の設計)
- `path`: `.idma.dat` (バイナリ) に統一
- **emu:process バックエンド**: `path` が `.idma.dat` で終わる場合、対応する `.asm` ファイルを自動検索して読み込む。`.asm` が見つからなければ WARNING を出して NULL を返す。emu:lib 選択時はこの変換をスキップし `.idma.dat` をそのまま読む
- **片への分割 (実機と emu:lib)**: `mnc2_load_kernel()` はカーネルバイナリを `mnc2_get_kernel_piece_size()` の大きさ (既定 4 MiB) の片に分け、片ごとに連続した DMA 用メモリを確保する。片の境界は 128 byte (1024 bit 命令語) の倍数で、命令語の境界ならどこで分けてもよい (根拠は開発者マニュアル 3.6.1.15、3.6.5、3.6.6 の命令キューとフォワーディングの記述)。片の数は `mnc2_kernel_get_piece_count()` で分かる。片の大きさは `mnc2_set_kernel_piece_size()` で変えられる (128 以上の 2 の冪乗。それ以外は `MNC2_ERROR_PARAM`。他の `set_*` と違い戻り値は `MNC2_SUCCESS` / エラーで、旧値は get で読む)。emu:process は分割せず常に 1 片
- **確保失敗**: 片 1 つ分の連続領域が確保できないと NULL を返す。片 1 つに確保できる上限は 4 MiB (2026-11-08 に実機で確認。8 MiB 以上は確保できない)。既定の片 4 MiB はこの上限に合わせたもの。空き状況によっては 4 MiB 以下でも失敗しうる。失敗の理由は stderr には出さず、`mnc2_get_exec_stats()` の `load_alloc_failures` / `last_alloc_fail_bytes` / `last_alloc_fail_piece` に残す
- `mnc2_free_kernel()`: `exec_kernel` 後であればいつでも呼べる (内部で IDMA idle まで wait してから全片を解放するので race なし)。カーネルオブジェクトのライフサイクルはデバイスと独立

#### `mnc2_exec_kernel()` の戻り値 (待たない版)

| 戻り値 | 意味 |
|---|---|
| `MNC2_SUCCESS` | 全片を IDMA に積んだ (完了は待っていない) |
| `MNC2_ERROR_QUEUE_LIMIT` | 片数が `idma_queue_limit` を超えていて、キューに収まらない。やり直しても通らない。`mnc2_exec_kernel_blocking` を使う |
| `MNC2_ERROR_BUSY` | IDMA キューの空きに全片が収まらない。何も積んでいない。掃けてから呼び直す |
| `MNC2_ERROR_PIO` | キューの空き確認 (IDMA_STAT の読み出し) に失敗 |
| `MNC2_ERROR_DMA` | kick 自体に失敗 (失敗した片より前の片は積まれている) |
| `MNC2_ERROR_KERNEL` | kernel ハンドルが不正 |
| `MNC2_ERROR_DEVICE` | device ハンドルが不正 |

**`MNC2_ERROR_BUSY` は失敗ではなく「今は積めない」という通知である。** 掃けてから同じ
カーネルで呼び直せばよい。片が 1 つの kernel では 0.4.2 までと同じ振る舞いになる。

**`mnc2_exec_kernel` は `MNC2_ERROR_TIMEOUT` (-5) を返さない。** 0.4.1 までは kick の後に
IDMA が busy になるのを観測して「確かに開始した」を確認する仕組みがあり、観測できないと
-5 を返していた。この確認は、観測した busy がどの kick に由来するか区別できず、また命令列が
短いと正常なのに失敗を返すため 0.4.2 で廃止した。

#### `mnc2_exec_kernel_blocking(kernel, limit)` の戻り値 (待つ版)

全片を kick し、そのあと IDMA キューの残り (IDMA_STAT の `n_dma`) が `limit` 以下になるまで待って戻る。
`limit` は「戻るときにキューに残っていてよい片数」で、0 から `idma_queue_limit` (既定 16) まで。0 はキューが
空になるまで待つ。kick の途中でキューが `idma_queue_limit` に達したら、減るのを待ってから続きを kick する
(待たない版が `MNC2_ERROR_BUSY` を返す場面で待つ)。`limit` が `idma_queue_limit` に等しいなら、最後の kick の
後は待たない。

例: `limit` = 1 で 3 片の kernel を、キューが空の状態から exec すると、3 片を続けて kick し (16 に届かない)、
片 1 と片 2 の転送が終わって `n_dma` が 1 になったら戻る。片 3 はキューに残っている。

キューが空になっても kernel の実行完了ではない (命令バッファに残っている分を PE が実行中でありうる)。
実行完了を待つのは `mnc2_wait_idma_idle` (`n_dma` と `inst_in_ibuf` の両方が 0 になるまで待つ)。

| 戻り値 | 意味 |
|---|---|
| `MNC2_SUCCESS` | 全片を IDMA に積み、キューの残りが `limit` 以下になった (実行完了は待っていない) |
| `MNC2_ERROR_PARAM` | `limit` が負か `idma_queue_limit` より大きい。何も積んでいない |
| `MNC2_ERROR_TIMEOUT` | 途中の空き待ちか最後の残り待ちが `idma_complete_max` (PIO read 回数) に達した。積んだ片はそのまま残る。積み残しがあるかは統計の `pieces_kicked` の差で分かる |
| `MNC2_ERROR_PIO` | IDMA_STAT の読み出しに失敗 |
| `MNC2_ERROR_DMA` | kick 自体に失敗 |
| `MNC2_ERROR_KERNEL` | kernel ハンドルが不正 |
| `MNC2_ERROR_DEVICE` | device ハンドルが不正 |

積んである片数の見積りには、`n_dma` の読み値と「直前の見積り + 1」の大きい方を使う
(kick 直後の読み値は反映前で少なく見えることがあるため)。待ちに入って 1 回以上 poll した
あとは読み値を信じる。最後の残り待ちに入るかどうかも同じ見積りで決める。これは kick の反映に
PIO 1 往復あれば足りるという仮定で、未検証である。

**kernel 内の `wait` が待つ `mnc2_send` は、この関数を呼ぶ前に済ませる。** 後にすると、先頭の片が
send を待って進まず、exec は空きを待ち、互いに待つ形になる (待ちの上限で `MNC2_ERROR_TIMEOUT`)。

#### 実行の統計 (`mnc2_exec_stats_t`)

```c
#define MNC2_EXEC_STATS_VERSION 1
typedef struct {
    uint32_t version;                 /* = MNC2_EXEC_STATS_VERSION。先頭固定 */
    uint64_t exec_calls;              /* mnc2_exec_kernel の呼び出し回数 */
    uint64_t exec_busy;               /* mnc2_exec_kernel が MNC2_ERROR_BUSY を返した回数 */
    uint64_t exec_queue_limit;        /* mnc2_exec_kernel が MNC2_ERROR_QUEUE_LIMIT を返した回数 */
    uint64_t exec_blocking_calls;     /* mnc2_exec_kernel_blocking の呼び出し回数 */
    uint64_t exec_blocking_waited;    /* blocking で待ちに入った回数 (kick 前の空き待ちか kick 後の残り待ち。呼び出し単位) */
    uint64_t wait_polls;              /* 待ちで IDMA_STAT を読んだ回数の累計 */
    uint64_t exec_blocking_timeouts;  /* 待ちが上限に達した回数 */
    uint64_t pieces_kicked;           /* kick した片の累計 (両 API) */
    uint32_t max_pieces_per_exec;     /* 1 回の exec で積んだ片の最大数 (両 API) */
    uint32_t max_pieces_per_kernel;   /* load した kernel の片数の最大 */
    uint64_t load_alloc_failures;     /* load_kernel で連続メモリの確保に失敗した回数 */
    uint64_t last_alloc_fail_bytes;   /* 直近の失敗時の kernel 全体の大きさ (bytes) */
    uint64_t last_alloc_fail_piece;   /* 直近の失敗時に確保しようとした片の大きさ (bytes) */
} mnc2_exec_stats_t;

int mnc2_get_exec_stats(mnc2_device_t dev, mnc2_exec_stats_t* out);
int mnc2_clear_exec_stats(mnc2_device_t dev);
```

- デバイスごとの記録。`mnc2_open` から `mnc2_close` までの累計で、`mnc2_clear_exec_stats` で
  0 に戻せる (`version` は保つ)
- `mnc2_get_exec_stats` はコピーを `out` に書く。`MNC2_SUCCESS` / `MNC2_ERROR_DEVICE` /
  `MNC2_ERROR_PARAM` (`out` が NULL)
- exec と load_kernel は stderr に出力しない。何が起きたかは exec の後にこの API で読んで出す

#### sync model (重要)

`mnc2_exec_kernel` は **async** (kick 前にキューの空きを 1 回確認するだけで
kernel 完了は待たない、並列性維持)。空きが無ければ積まずに `MNC2_ERROR_BUSY`
を返し、掃けるのを待つことはしない。`mnc2_free_kernel` は **sync** (内部で 0x018
IDMA_STAT を idle まで wait してから `gpfn3_free_dma_memory`)。詳細は
「[DMA kick の sync model](#dma-kick-の-sync-model)」セクション参照。

**caller 注意 (`mnc2_exec_kernel`):**
- exec 後、`mnc2_free_kernel` を呼ぶまで kernel data を変更しない (現状の
  contract、変更なし)。
- **複数 kernel を並列に exec した場合の `free_kernel` タイミングは caller
  責任**。`free_kernel` の内部 idle wait は「全 IDMA が idle」を待つので、
  別 kernel が走り出せば「特定 kernel A の終了」を保証するわけではない。
  並列性が必要なら caller 側で kernel A → kernel A の free を直列に
  並べる (sequential exec) のが安全。

**caller 注意 (`mnc2_free_kernel`):**
- 内部で IDMA idle wait してから free するので、`exec_kernel` 直後に呼んでも
  kernel buffer が device に読まれてる最中の解放にはならない (= safe)。
- timeout (`idma_complete_max_cnt` iter 経過) 時は warn を stderr に出して
  free 続行 (abort しない、caller 責任)。

### デバッグメモリアクセス (エミュレータ専用)

```c
int mnc2_debug_read(mnc2_device_t dev, int mem_region,
                    const mnc2_loc_t* loc, int addr, int count,
                    uint64_t* out);

int mnc2_debug_write(mnc2_device_t dev, int mem_region,
                     const mnc2_loc_t* loc, int addr, int count,
                     const uint64_t* data);
```

- エミュレータ内部のメモリ階層に u64 (64bit) 単位で直接アクセスする
- `loc`: PE 位置 (`mnc2_loc_t`)。NULL の場合 `{0,0,0,0,0}` として扱う
- `addr`: u64 アドレス (64bit ワード単位)
- `count`: u64 数 (> 0)
- `mnc2_debug_read`: カーネル実行後のメモリ内容を読み出す。emu:process では呼び出しごとにカーネルを再実行する
- `mnc2_debug_write`: カーネル実行前にメモリ内容を書き込む。emu:process では `d set` コマンドを `asm_buf` に追記する（次の flush 時に実行される）
- 戻り値: 0 = 成功、負値 = エラー

#### 対応メモリ領域 (バックエンド別)

| `mem_region` | emu:lib | emu:process | device (実機) |
|---|---|---|---|
| `MNC2_MEM_PDM` | read/write 可 | read は既知バグ (下記)、write は未対応 (`MNC2_ERROR_PARAM`) | **未サポート** (`MNC2_ERROR_DEVICE`) |
| `MNC2_MEM_DRAM` | read/write 可 | read は対応、write は未対応 (`MNC2_ERROR_PARAM`) | **未サポート** (`MNC2_ERROR_DEVICE`) |
| `MNC2_MEM_L2BM` / `L1BM` / `LM0` / `LM1` / `GRF0` / `GRF1` | read/write 可 | read/write 可 | **未サポート** (`MNC2_ERROR_DEVICE`) |

`loc->chip` で Group (0〜3) を選択する。例えば `loc->chip = 1` は PDM1 / DRAM1 を指す (emu バックエンドのみ)。

#### 実機バックエンドで PDM / DRAM を非サポートとする根拠

`mnc2_debug_read` / `mnc2_debug_write` は **診断・テスト目的の補助 API** であり、
実機バックエンドでは意図的に PDM / DRAM を非サポート (`MNC2_ERROR_DEVICE`) とする。
これはバグや未実装ではなく、以下の理由に基づく設計判断である。

1. **正規経路との役割分離**
   PDM / DRAM に対するホスト ↔ デバイス間のデータ転送には、
   `mnc2_send` / `mnc2_recv` という正規経路が存在する。
   `mnc2_debug_read` / `mnc2_debug_write` が PDM / DRAM をサポートすると、
   本来 `mnc2_send` / `mnc2_recv` で明示すべき転送が診断 API 経由で行われ、
   データフローの見通しが失われる。診断 API は PE 内部メモリ
   (L2BM / L1BM / LM / GRF) のように **ホストから直接転送できない領域** の
   観測に限定することで、正規経路の単純性と予測可能性を保つ。

2. **Group 1〜3 の PDM / DRAM は原理的にホスト直読み不可**
   GPFN3 は Group 0 のみがホストと PCIe で接続されており、
   Group 1〜3 の PDM / DRAM へはホストから DDMA で直接アクセスできない。
   これらを読み出すには Group 0 の PDM0 を中継バッファとして
   経由する必要があり、診断 API でこれを行うと副作用として
   **PDM0 の内容が破壊される**。破壊的副作用を隠す診断 API は
   呼び出し側の期待する不変条件を壊すため、提供しない方が安全である。

したがって、実機で PDM / DRAM の内容を確認したい場合は:
- PDM (Group 0): `mnc2_recv` を使う
- DRAM: 直接の読み出し手段は提供しない。必要であれば PE 内部メモリへ
  ロードしてから `mnc2_debug_read` で読む (これはエミュレータ限定)

#### エミュレータ固有の既知バグ (emu:process, PDM read)

`mnc2_debug_read` で `MNC2_MEM_PDM` を指定すると、emu:process バックエンドでは
内部のエミュレータ (`gpfn3_package_main`) が PDM ではなく DRAM の内容を返す
バグがある。現状の skeleton はこれを避けるため emu:process で PDM read を
明示的に `MNC2_ERROR_PARAM` で弾く。PDM の正確な読み出しには `mnc2_recv` を
使用すること。emu:lib バックエンドはこのバグの影響を受けない
(`get_piu_direct` 経由で正しく読める)。

### DMA タグ検証

```c
int mnc2_kernel_get_dma_tags(mnc2_kernel_t kernel,
                              uint8_t* tags_wd, uint8_t* tags_mv,
                              int max_tags,
                              int* out_wd_count, int* out_mv_count);
```

- カーネルデータから DMA タグ (`tag_wd` / `mvid`) を抽出する
- `tags_wd` / `tags_mv` に最大 `max_tags` 個のタグが格納される
- `mnc2_send` / `mnc2_recv` はどのカーネルと組み合わせて使われるか知りようがないため、タグ衝突の自動検出は原理的にできない。この API で事前にカーネルの使用タグを確認し、衝突しないタグを選ぶのが呼び出し側の責任
- 戻り値: 0 = 成功、負値 = エラー

### DMA ステータス取得

PIO レジスタを読むだけの読み取り専用 API。送受信 API の動作には影響しない。

```c
typedef struct {
    int active;     /* DMA 動作中 (1) / idle (0) */
    int regset;     /* 処理中レジスタセット番号 (0-63) */
    int overflow;   /* キューオーバーフロー履歴 */
    int n_dma;      /* キュー内 DMA 数 (0-31) */
} mnc2_ddma_ch_stat_t;

typedef struct {
    mnc2_ddma_ch_stat_t host_to_pdm[2];  /* [0]=CH0, [1]=CH1 */
    mnc2_ddma_ch_stat_t pdm_to_host[2];
} mnc2_ddma_stat_t;

typedef struct {
    int n_dma;         /* キュー内 DMA 数 */
    int inst_in_ibuf;  /* 命令バッファ内の命令数 */
    int overflow;      /* 命令バッファの overflow */
} mnc2_idma_stat_t;

int mnc2_get_ddma_stat(mnc2_device_t dev, mnc2_ddma_stat_t* out);
int mnc2_get_idma_stat(mnc2_device_t dev, mnc2_idma_stat_t* out);
int mnc2_get_wdbit_status(mnc2_device_t dev, uint64_t* wait_mask);
int mnc2_clear_wdbit(mnc2_device_t dev, unsigned int wd);
```

- `mnc2_get_ddma_stat`: データ DMA の状態を 4 チャネル (host → PDM 2 本、PDM → host 2 本) に分解して返す
- `mnc2_get_idma_stat`: 命令 DMA のキュー長と命令バッファの滞留数を返す
- `mnc2_get_wdbit_status`: 各 bit が WD i の wait 状態 (1 = wait 中)。**実機専用**で、エミュレータでは意味のある値を返さない
- `mnc2_clear_wdbit`: 指定した WD (1-63) の wait を強制解除する
- 戻り値: `MNC2_SUCCESS` / `MNC2_ERROR_PIO` / `MNC2_ERROR_PARAM` (出力先が NULL)

### 命令 capture / profiling

チップの命令カウンタを取得する。libgpfn3 の capture 機能の薄いラッパ。

```c
enum mnc2_capture_sel {
    MNC2_CAPTURE_A = 0,  /* 即時のカウンタ取得 */
    MNC2_CAPTURE_B = 1,  /* B から E は命令カウンタを trigger に snapshot */
    MNC2_CAPTURE_C = 2,
    MNC2_CAPTURE_D = 3,
    MNC2_CAPTURE_E = 4,
};

typedef struct {
    uint64_t all_nop;
    uint64_t empty_nop;
    union {
        uint64_t power_nop;
        uint64_t tag[256];   /* tag[0] は nop の総数 */
    };
} mnc2_capture_t;

int mnc2_capture_get_inst(mnc2_device_t dev, uint64_t* inst);
int mnc2_capture_set_trigger(mnc2_device_t dev, enum mnc2_capture_sel sel,
                             uint64_t inst_counter);
int mnc2_capture_read(mnc2_device_t dev, enum mnc2_capture_sel sel,
                      mnc2_capture_t* out);
```

- `mnc2_capture_get_inst`: 現在の命令カウンタを取得する
- `mnc2_capture_set_trigger`: B/C/D/E に snapshot のトリガとなる命令カウンタ値を設定する。その値に達した時点で snapshot が取られる
- `mnc2_capture_read`: snapshot の結果を読み出す

### PDM Tag の pool と完了監視

PDM Tag (1-63) を pool から払い出し、転送の完了を監視する。

```c
int mnc2_tag_alloc(mnc2_device_t dev, unsigned int* tag);
int mnc2_tag_free(mnc2_device_t dev, unsigned int tag);
int mnc2_tag_set_size(mnc2_device_t dev, unsigned int tag, uint32_t size_bytes);
int mnc2_tag_check_done(mnc2_device_t dev, unsigned int tag, int* done);
int mnc2_tag_wait(mnc2_device_t dev, unsigned int tag);
int mnc2_tag_clear(mnc2_device_t dev, unsigned int tag);
```

- `mnc2_tag_alloc` / `mnc2_tag_free`: pool からの払い出しと返却。枯渇時は `MNC2_ERROR_BUSY`
- `mnc2_tag_set_size`: 転送サイズ (バイト単位、23 bit) を設定して tag を有効にする
- `mnc2_tag_check_done`: 完了しているかを `done` に 0/1 で返す (待たない)
- `mnc2_tag_wait`: 完了まで待つ。上限に達した場合は `MNC2_ERROR_TIMEOUT`
- `mnc2_tag_clear`: tag を無効化する

### WD の pool

WD slot (1-63) を pool から払い出す。

```c
int mnc2_wd_alloc(mnc2_device_t dev, unsigned int* wd);
int mnc2_wd_free(mnc2_device_t dev, unsigned int wd);
int mnc2_wd_wait(mnc2_device_t dev, unsigned int wd);
int mnc2_wd_clear(mnc2_device_t dev, unsigned int wd);
```

- `mnc2_wd_alloc` / `mnc2_wd_free`: pool からの払い出しと返却。枯渇時は `MNC2_ERROR_BUSY`
- `mnc2_wd_wait`: 該当 WD の wait が解除されるまで待つ。上限に達した場合は `MNC2_ERROR_TIMEOUT`
- `mnc2_wd_clear`: wait を強制解除する (`mnc2_clear_wdbit` と同じ)

### regset の pool

DMA が使うアドレスレジスタ (0-63) を pool から払い出す。

```c
int mnc2_regset_alloc(mnc2_device_t dev, unsigned int* regset);
int mnc2_regset_free(mnc2_device_t dev, unsigned int regset);
```

- 送受信 API と非同期 DMA の内部で使われる。呼び出し側が直接使うこともできる
- 枯渇時は `MNC2_ERROR_BUSY`

### 非同期 DMA

kick だけ行って完了を待たずに戻る転送。handle を通じて完了を確認する。

```c
typedef struct mnc2_dma_handle* mnc2_dma_handle_t;

mnc2_dma_handle_t mnc2_async_send(mnc2_device_t dev, const void* buf,
                                  uint64_t pdm_offset, size_t size,
                                  unsigned int send_wait_tag);
mnc2_dma_handle_t mnc2_async_recv(mnc2_device_t dev, void* buf,
                                  uint64_t pdm_offset, size_t size,
                                  unsigned int recv_wait_tag);
int  mnc2_async_poll(mnc2_dma_handle_t handle, int* done);
int  mnc2_async_wait(mnc2_dma_handle_t handle);
void mnc2_async_release(mnc2_dma_handle_t handle);
```

- `mnc2_async_send` / `mnc2_async_recv`: 転送を kick して handle を返す。失敗時は NULL
- `mnc2_async_poll`: 完了しているかを `done` に 0/1 で返す (待たない)
- `mnc2_async_wait`: 完了まで待つ。上限に達した場合は `MNC2_ERROR_TIMEOUT`
- `mnc2_async_release`: handle を解放する。完了を確認した後に必ず呼ぶこと
- 完了の検知は同期版の `mnc2_send` / `mnc2_recv` と同じ DDMA idle で行う
- 転送に使うアドレスレジスタは pool から払い出されるので、複数の転送を同時に走らせられる
- 完了するまで送信元 / 受信先のバッファを解放・再利用してはいけない (同期版と異なり、return 時点では転送が終わっていない)

## エラーコード

| 定数 | 値 | 意味 |
|------|----|------|
| `MNC2_SUCCESS` | 0 | 成功 |
| `MNC2_ERROR_DEVICE` | -1 | デバイスオープン失敗 |
| `MNC2_ERROR_DMA` | -2 | DMA 転送失敗 |
| `MNC2_ERROR_PIO` | -3 | PIO 読み書き失敗 |
| `MNC2_ERROR_IO` | -4 | ファイル I/O 失敗 |
| `MNC2_ERROR_TIMEOUT` | -5 | タイムアウト |
| `MNC2_ERROR_ENV` | -6 | 環境変数未設定 |
| `MNC2_ERROR_PARAM` | -7 | パラメータ不正 |
| `MNC2_ERROR_KERNEL` | -8 | カーネル不正 (magic check 失敗等) |
| `MNC2_ERROR_BUSY` | -9 | pool の枯渇 (PDM Tag / WD / regset)、IDMA キューの空き不足 (積んでいない) |
| `MNC2_ERROR_QUEUE_LIMIT` | -10 | kernel の片数が `idma_queue_limit` を超え、キューに収まらない (やり直しても通らない) |

## バックエンド定数

C コードから backend を識別するための定数 (`mnc2_get_backend()` の戻り値、`mnc2_device_t` 内部状態)。

| 定数 | 値 | 意味 |
|------|----|------|
| `MNC2_BACKEND_UNKNOWN` | -1 | 不明 |
| `MNC2_BACKEND_DEVICE` | 0 | 実機 |
| `MNC2_BACKEND_EMU_PROCESS` | 1 | エミュレータ (外部プロセス) |
| `MNC2_BACKEND_EMU_LIB` | 2 | エミュレータ (ライブラリリンク) |

## DMA タグの指定

### send の send_wait_tag

`mnc2_send` の `send_wait_tag` には、カーネルの `wait iXX` 命令と対応するタグを指定する。
vsm-linker が生成するカーネルでは通常 `0x10` を使用する（カーネル内の `wait i10` に対応）。

複数回の send を行う場合:
- **最後の send のみ** にトリガー用タグ (`send_wait_tag=0x10`) を指定する
- それ以外の send は `send_wait_tag=0` (非トリガー) にする

**タグ衝突の自動検出について**: `mnc2_send` / `mnc2_recv` はどのカーネルと組み合わせて
使われるか知りようがないため、タグ衝突の自動検出は原理的にできない。
呼び出し側が `mnc2_kernel_get_dma_tags` でカーネルの使用タグを事前に確認し、
衝突しないタグを選ぶ必要がある。

### recv の recv_wait_tag

`mnc2_recv` の `recv_wait_tag` には、カーネルの collect 完了を示すタグを指定する。
具体的な値は `mnc2_kernel_get_dma_tags` でカーネルから取得する:

- **`recv_wait_tag=0`**: 待ちなし。カーネル完了前にデータを読む可能性があるため、通常は使用しない

**emu:process では `recv_wait_tag` は無視される** (PDM の写しを即座に読む)。emu:lib はタグを待ち、消費前の再利用を
`double asserted PIU-DT done flag` のエラーで止める。


### タグの規則

タグの完了は device の中の flag で記録される。守らないと、待ちがすり抜けて結果が壊れるか、DMA がキューに残って以後の DMA が
時間切れになる。

1. タグの完了は **立っているか否か** で記録され、回数は数えない。消費される前に同じタグを再び立てても 1 つしか残らない。
   emu:lib は recv の時点で `double asserted PIU-DT done flag` のエラーで止め、実機は 2 回目の待ちが始まらず DMA がキューに
   残って以後の DMA が時間切れになる (復旧は `mnc2_reset`)。
2. 立てる側と消費する側の対応。`mnc2_send(tag X)` が立てたもの (dma) は kernel の `wait iXX` が消費する。kernel のタグ付き MV
   (タグを付けた mv 系命令) は kernel 自身の `wait iYY` が消費するもの (dt) を立て、PDM や L2BM が絡む MV はさらに
   `mnc2_recv(tag Y)` が消費するもの (piu_dt) を立てる。両方で同じ番号を待っても干渉しない。立った flag は時間が経っても
   消えず、`mnc2_close` / `mnc2_open` やプロセスの境界をまたいでも残る。
3. 同じタグの再利用は、その経路で消費した後なら可。recv のタグは MV 1 回に recv 1 回 (`exec → recv → exec → recv` は可)。
   1 本の kernel の中で、recv が消費する前に同じタグの MV を 2 回出してはならない (別のタグにするか、間に host との
   待ち合わせを入れる)。
4. **立てた flag は、そのプログラムが終わる前にすべて消費する。** (a) `mnc2_send(tag X)` を出したら、その kernel の `wait iXX` で
   消費させる (残すと、次の kernel の `wait iXX` が自分の send を待たずに通る)。(b) タグ付き MV を出したら、そのタグを
   `mnc2_recv` で消費する。kernel の内部同期にしか使わないタグ (MV の直後に kernel 自身が `wait` するもの) も、PDM や L2BM が絡む MV なら
   recv 側の分 (piu_dt) が残るので、終わる前にそのタグの最小の recv (読む場所は回収済みの領域でよい) を出して消費する。
   残した flag は、次に同じ device を使うプログラムの待ちをすり抜けさせ、結果を壊す。ライブラリは残った flag を検出しない。
5. 前のプログラムが flag を残した疑いがあるときの手段は `mnc2_reset` (flag は消える。PDM の中身は消えない)。
   自分の後始末の代わりには使わない。
6. kernel の内部同期にしか使わないタグは、本体の recv には使わない (vsm-linker は `:reserved_wait_tag` で予約する)。
   使うと 3 の形 (消費前の 2 回目) になりうる。


## vsm-linker からの典型的な呼び出しパターン

```c
// --- 初期化 ---
mnc2_device_t dev = mnc2_open(0);
mnc2_kernel_t kernel = mnc2_load_kernel(dev, "stencil.idma.dat");

void* sendbuf = mnc2_alloc_host_buffer(dev, nx * ny * nz * 8);
void* recvbuf = mnc2_alloc_host_buffer(dev, nx * ny * nz * 8);

// --- タイムステップループ ---
for (int step = 0; step < nsteps; step++) {
    // データ準備
    memcpy(sendbuf, field_data, size);

    // Host → PDM0 (エンディアン変換は ENDIAN_CTRL で自動処理)
    // send_wait_tag はカーネルの wait タグと一致させる (vsm-linker 標準: 0x10)
    mnc2_send(dev, sendbuf, pdm_offset, size, 0x10);

    // カーネル実行 (キューに空きが無ければ MNC2_ERROR_BUSY。戻り値を見る)
    mnc2_exec_kernel(kernel);

    // PDM0 → Host (emu では auto-flush、エンディアン変換も自動)
    // recv_wait_tag はカーネルの collect 完了タグ (mnc2_kernel_get_dma_tags で取得)
    mnc2_recv(dev, recvbuf, recv_pdm_offset, recv_size, recv_tag);

    // 結果取得
    memcpy(result_data, recvbuf, size);

    // halo 交換 (ホスト側で隣接ドメインの境界コピー)
    exchange_halos(result_data, ...);
}

// --- 後始末 ---
// カーネルの内部同期のタグ (PDM や L2BM が絡む MV のうち recv に使わなかったもの) の flag は、
// ここでそのタグの最小の recv を 1 回ずつ出して消費する (「タグの規則」4)。読む場所は回収済みの領域でよい
mnc2_free_host_buffer(dev, sendbuf, size);
mnc2_free_host_buffer(dev, recvbuf, size);
mnc2_free_kernel(kernel);
mnc2_close(dev);
```

## 備考

- `exit()` は使わない。すべてエラーコードで返す
- グローバル変数は使わない。状態は `mnc2_device_t` ハンドルに格納
- スレッドセーフではない（同一デバイスに対する並行呼び出し不可）

### c 行（データ DMA 行）の責務

emu:process バックエンドでは、カーネル `.asm` 内の c 行（データ DMA 行）は
`mnc2_load_kernel` が自動的に除去する。データ DMA は `mnc2_send` が c 行を
自前で生成するため、カーネル側に c 行を含める必要はない。

vsm-linker が出力する `.asm` に c 行が含まれていても、libmnc2 側で処理されるため
上流チームが c 行の有無を気にする必要はない。

## 既知の問題 (実機)

現時点で既知の実機固有の問題はない。

## 変更履歴

- **0.4.3**:
  - `mnc2_load_kernel` が kernel を片 (既定 4 MiB、128 以上の 2 の冪乗) に分けて連続 DMA 用メモリを確保するようになった (実機と emu:lib。emu:process は分割しない)
  - `mnc2_exec_kernel` は全片が積めるときだけ積む。片数が `idma_queue_limit` を超える kernel には `MNC2_ERROR_QUEUE_LIMIT` (-10) を返す
  - `mnc2_exec_kernel_blocking(kernel, limit)` を追加 (全片を kick し、キューの残りが `limit` 以下になるまで待つ。完了は待たない。待ちの上限で `MNC2_ERROR_TIMEOUT`)
  - rc1 にあった `mnc2_get_exec_blocking_limit` / `mnc2_set_exec_blocking_limit` (device 単位の設定値、既定 16、kick を止めるキューの上限) は rc2 で削除し、引数 `limit` (戻るときの残り数) に置き換えた (`limit` の上限は rc2 では 31、rc3 から `idma_queue_limit`)
  - `mnc2_get_kernel_piece_size` / `mnc2_set_kernel_piece_size` / `mnc2_kernel_get_piece_count` を追加
  - `mnc2_get_exec_stats` / `mnc2_clear_exec_stats` と `mnc2_exec_stats_t` を追加 (exec と load_kernel の記録。確保失敗の理由はここに残し stderr には出さない)
  - `mnc2_set_idma_queue_limit` が範囲 1 から 31 の外を `MNC2_ERROR_PARAM` で断るようになった
  - 「DMA タグの指定」に「タグの規則」の節を追加 (flag はビットで回数を数えない、立てた flag はすべて消費して終わる、消費前に同じタグを 2 回立てない)。emu の `recv_wait_tag` の扱いを emu:process と emu:lib で分けて記述
  - 文書: 冒頭と「典型的な呼び出しパターン」の例の `mnc2_exec_kernel` の引数を実際の signature (`kernel` のみ) に直し、後始末に内部同期のタグの flag の消費を書いた。配布物に含まれない内部文書への参照を除いた
- **0.4.2**:
  - `mnc2_exec_kernel` が IDMA キューに空きが無いとき `MNC2_ERROR_BUSY` を返すようになった
  - `mnc2_exec_kernel` が `MNC2_ERROR_TIMEOUT` を返さなくなった
  - kick 後に IDMA が busy になるのを観測して開始を保証する仕組みを廃止
  - `mnc2_get_idma_kick_verify_max` / `mnc2_set_idma_kick_verify_max` を削除
  - `mnc2_get_idma_queue_limit` / `mnc2_set_idma_queue_limit` を追加
  - ツール u02-run-idma に repeat-idma と gen_nop.py を追加
- **0.4.1**:
  - `mnc2_get_ddma_stat` / `mnc2_get_idma_stat` / `mnc2_get_wdbit_status` / `mnc2_clear_wdbit` を追加
  - 命令 capture の `mnc2_capture_*` 3 関数を追加
  - PDM Tag / WD / regset の pool を扱う `mnc2_tag_*` 6 関数、`mnc2_wd_*` 4 関数、`mnc2_regset_*` 2 関数を追加
  - 非同期 DMA の `mnc2_async_*` 5 関数を追加
  - エラーコード `MNC2_ERROR_BUSY` を追加
  - `MNC2_SEND_TAG_DISTRIBUTE` を削除
  - 長い VSM の実行で recv が完了しない場合に実機で timeout を返すよう変更
  - examples 追加: 60-67
- **0.4.0**:
  - `mnc2_send` / `mnc2_recv` から `int elem_size` 引数を削除
  - エンディアン制御を `mnc2_set_endian_ctrl` / `mnc2_get_endian_ctrl` で行う
  - `mnc2_send` を sync に変更
  - `mnc2_reset` を追加
  - `mnc2_wait_ddma_idle` / `mnc2_wait_idma_idle` を追加
  - DMA 待ち上限の get/set 8 関数を追加
  - `mnc2_change_ddma_wait_time` を削除
  - `mnc2_open` 時に DMA キューの残留を警告する
  - DMA タイムアウト時の診断出力を追加
  - examples 追加: 50-59
  - ツール u02-run-idma を追加
- **0.3.10**:
  - 統一 `.a` (3 バックエンド対応)。実機バックエンド対応。実機エンディアン設定修正。u01-mmap ツール追加
- **0.3.9**:
  - reduce-pe examples 追加 (48, 49)
- **0.3.6**:
  - `mnc2_debug_write` API 追加 (emu:lib + emu:process 対応)
- **0.3.5**:
  - emu:lib で collect 複数回 (同一 wait タグ再利用) 時のエラーを回避。collect 系 examples 追加
- **0.3.4**:
  - examples 追加。`uninstall.sh` バグ修正
- **0.3.3**:
  - `mnc2_debug_read` バグ修正 (GRF/LM アドレス変換)。`mnc2_debug_read` API 追加
- **0.3.2**:
  - 環境変数によるバックエンド切り替えに対応
- **0.3.1**:
  - `mnc2_get_backend_name` API 追加
- **0.2.0**:
  - API 統一リリース（0.1.x との互換性なし）。バイト単位統一
- **0.1.x**:
  - 初期版
