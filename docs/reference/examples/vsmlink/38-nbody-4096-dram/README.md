# 38-nbody-4096-dram: 他粒子を各グループの DRAM から @broadcast で受ける 3D N 体

`examples/28-nbody-3d` と同じ物理（leapfrog、softening 付き重力、全対全、rsqrt のニュートン補正）で、
**他粒子を `@broadcast` が読む場所だけを PDM から DRAM に変えた** example である。

- emu:lib は N=32（`ninja test-emu-lib`）。実機は N=4096（`ninja test-device`）
- 28 との違いは次の 3 点だけである
  1. accumulate の `@broadcast` の slot を `:place :group-dram` にした（`gen_accumulate.py`）
  2. ホストが PDM0 に送った粒子ごと並びを、全グループの DRAM へ置く kernel（stage kernel、`gen_stage.py`）を足した
  3. ホストが粒子ごと並びを送るときに、送信のタグを付けない（DRAM の slot に `:send_wait_tag` は書けない）
- 実機のデバイス番号は、ホストの引数 `--dev=<n>` で選ぶ（既定 0）。デバイス 1 を指定すると、2 枚目の MN-Core 2 の試験になる

## データの移動

**どのデータを、誰が動かすか**を、担当ごとに分けて書く。

| 担当 | 動くデータ | 向き | 場所と番地 |
|---|---|---|---|
| **host-dma の API**（`mnc2_send` / `mnc2_recv`） | 初期値、毎ステップの粒子ごと並び、更新後の位置と速度 | ホスト ↔ **PDM0** | 下の「PDM0 の配置」。番地は u64 単位（byte は × 8）。ホストが DRAM に直接送る API は無い |
| **`_vsm` に直接書いた `mvp`**（directive ではない。stage kernel） | 粒子ごと並び `[x,y,z,m] × N` | PDM0 → **全グループ（0〜3）の DRAM** | PDM0 の 20480 番地から、各グループの DRAM の 0 番地から。`$p20480@0` → `$d0@<g>` |
| **directive が自動で行う** | `@distribute`（init）: PDM0 → PE。`@broadcast`（init の定数）: PDM0 → PE。`@broadcast`（accumulate）: **DRAM** → L2BM → L1BM → PE。`@collect`: PE → PDM0 | 各 directive の仕様書の節 | slot の `:place` と `:addr`（下の「slot」） |

**PDM0 は、グループ 0 の PDM である**（dev manual の `$p<addr>@0`。`param-spec.md` の `:place`）。
DRAM はグループごとに別のメモリで、`$d<addr>@<g>` の `<g>` で選ぶ。stage kernel は、4 つのグループの DRAM に同じ値を置く。

1 ステップの流れ:

```
ホスト --mnc2_send(タグ 0)--> PDM0[20480..]  粒子ごと並び [x,y,z,m] x N
                                 | stage kernel: mvp x (4 グループ x 分割)
                                 v
               各グループの DRAM[0..]  (4 つとも同じ値)
                                 | accumulate の分割 kernel: @broadcast size 4 (slot 8 は :group-dram)
                                 v
                          全 PE に他粒子 (x,y,z,m) → 力を LM に累積
```

### PDM0 の配置（u64 単位。byte は × 8）

| 番地 | 大きさ | 内容 | 並び | 書く担当 | 読む担当 |
|---|---|---|---|---|---|
| 0 | 16384 | 初期の位置と質量（x,y,z,m） | 成分ごと（各 4096） | ホスト（1 回） | `@distribute`（init、slot 8） |
| 16384 | 64 | 定数（eps2、0.5、1.5、dt） | — | ホスト（1 回） | `@broadcast`（init、slot 16） |
| 20480 | 4N を 64 の倍数に切り上げ（N=32 で 128、N=4096 で 16384） | **粒子ごと並び** | 粒子ごと `[x,y,z,m]` | ホスト（毎ステップ） | **stage kernel の `mvp`** |
| 40960 | 12288 | 初期の速度（vx,vy,vz） | 成分ごと（各 4096） | ホスト（1 回） | `@distribute`（init、slot 24） |
| 131072 | 12288 | 更新後の位置 | 成分ごと（各 4096） | `@collect`（collect_pos、slot 8） | ホスト（`mnc2_recv`） |
| 143360 | 12288 | 更新後の速度 | 成分ごと（各 4096） | `@collect`（collect_vel、slot 8） | ホスト（`mnc2_recv`） |

### 各グループの DRAM の配置（u64 単位。4 つのグループで同じ）

| 番地 | 大きさ | 内容 | 書く担当 | 読む担当 |
|---|---|---|---|---|
| 0 | 4N を 64 の倍数に切り上げ | **粒子ごと並び** `[x,y,z,m] × N` | stage kernel の `mvp`（PDM0 の 20480 番地から） | `@broadcast`（accumulate、slot 8） |

accumulate の分割 kernel `g`（粒子 `128g`〜）の slot 8 は `:addr` が `4 × 128g` で、その kernel の実行ごとに、この番地から続きを読む。

### slot

| kernel | slot | `:place` | `:addr`（u64） | タグ |
|---|---|---|---|---|
| init | 8（位置の `@distribute`） | `:pdm0` | 0 | 送信 `#x10` |
| init | 16（定数の `@broadcast`） | `:pdm0` | 16384 | — |
| init | 24（速度の `@distribute`） | `:pdm0` | 40960 | — |
| accumulate の分割 `g` | 8（他粒子の `@broadcast`） | **`:group-dram`** | `4 × 128g` | **付けない**（付けると E277） |
| collect_pos | 8（`@collect`） | `:pdm0` | 131072 | 受信 `#x1e` |
| collect_vel | 8（`@collect`） | `:pdm0` | 143360 | 受信 `#x1f` |
| stage | なし（directive を使わない） | — | — | `.param` の予約タグ `#x3f`（vsmlink 用）と `#x3e`（`mvp` の完了待ち） |

## stage kernel

`gen_stage.py` が生成する `_build/stage_n<N>._vsm` は、`mvp` を直接書いた `_vsm` である。1 回の `mvp` は 4096 u64 以下に分ける。

```
	mvp/n4096i3e $p20480@0 $d0@0      # PDM0 の 20480 番地から、グループ 0 の DRAM の 0 番地へ
	nop/3
	nop; wait i3e
	nop/5
	...                               # グループ 0〜3、4096 u64 ごとに繰り返す
```

- タグ付きの `mvp` には、同じタグ（`i3e`）の `wait` を置く。`wait` がそのタグの完了の印を消費するので、続けて同じタグを使える。
  `wait` を置かないタグ付きの MV 命令があると、印が残り、あとの同じタグの `wait` が転送の終わる前に抜ける
- `wait` の後の `nop/5` は、host-dma の examples（`mv-p10.vsm` など）と同じ形である
- stage kernel は、ホストの `mnc2_send` が終わってから exec する。`mnc2_send` は転送が終わってから戻るので、送信のタグは要らない

## 使い方

```bash
# SDK を有効にし、この example のディレクトリで実行する（examples の README.md の「動かし方」）

ninja                             # vsmlink が accumulate 分割 0（N=32）と stage kernel を生成する
ninja verify                      # それらの出力と expected.vsm / expected_stage.vsm の diff
ninja build-e2e                   # + assemble3 + ホストのビルド（N=32）
ninja test-emu-lib                # emu:lib で N=32、3 ステップ（値照合）
ninja compare-28                  # 28-nbody-3d と、位置と速度の出力がビット単位で一致すること（emu:lib）
ninja test-device                 # 実機で N=4096、3 ステップ
ninja test-device-dev1            # 実機のデバイス 1 で N=4096（手動）
```

ホストは単体でも動かせる。

```
_build/test_nbody_dram_n4096 [<ic.bin>] [--dev=<n>]
```

| 引数 | 意味 |
|---|---|
| `<ic.bin>` | 初期条件（`gen_ic.py` が作る struct 形バイナリ。1 粒子 8 double）。省略時は `_build/ic_n<N>.bin` |
| `--dev=<n>` | `mnc2_open` に渡すデバイス番号。省略時は 0 |

出力は `_result_n<N>/` に、`pos_<時刻>_<番号>.bin` と `vel_<時刻>_<番号>.bin`（粒子ごとに `x,y,z` の double）と、実行のマニフェスト `run_<時刻>.json` が書かれる。

## 検証

| 確認 | 内容 |
|---|---|
| `ninja test-emu-lib`（N=32） | 実機の 3 ステップを、ホストの倍精度の leapfrog と照合（成分ごとの相対誤差 `1e-6` 以下）。エネルギーのずれと系の最大半径も見る |
| `ninja compare-28`（N=32） | 28-nbody-3d（他粒子を PDM から読む）と同じ IC・同じ刻みで、位置と速度のスナップショットがバイト単位で一致する |
| `ninja test-device`（N=4096） | 実機で同じ照合 |

DRAM から読む経路が値を変えていれば、28 との比較が一致しない。

**確かめた範囲:** emu:lib の N=32（値照合と 28 との一致）。N=4096 のカーネルのビルド（分割 32 個と stage kernel の assemble3）。
実機のデバイス 0 の N=4096、3 タイムステップ（ホストの倍精度との照合。最大相対誤差 5.850e-13、エネルギーのずれ 1.008e-07）。
**確かめていない範囲:** 実機のデバイス 1（`ninja test-device-dev1`）。emu:lib の N=4096（1 ステップに数十分から数時間かかる見込み）。

## 書くときに知っておくこと

- `@broadcast` の slot を `:group-dram` にするときは、`.param` に `:send_wait_tag` を書かない（E277）。DRAM へ送るのはホストではなく stage kernel だからである
- `:group-dram` の slot を使える directive は `@broadcast` だけである。`@distribute` や `@collect` の slot は `:pdm0` にする
- 各グループの DRAM に置く値は、グループごとに違ってよい。`@broadcast` は、各 PE に**自分のグループの DRAM の値**を渡す。
  この example は 4 つのグループに同じ値を置くので、全 PE が同じ他粒子を受け取る
- 1 つの accumulate の分割 kernel は 128 粒子を扱い、`.idma.dat` は約 1.44 MB（28 と同じ）。全 N を 1 kernel にすると実機で load できない
