# 29-broadcast-collect: broadcast + collect の往復

`@broadcast` で全 PE に配った struct を各 PE で加工し、`@collect` で PDM に集めて回収する
最小の往復 example。集める側 (collect) のレイアウトとハードウェア経路の説明は
`docs/broadcast-collect-layout.md` にある (この example の設計メモ)。

## やること

1. ホストが `struct {x, y, z, m}` を 1 個作り、PDM の放送領域に置く。
2. `@broadcast size 4` で その 4 u64 を全 4096 PE に同じ値として配る (GRF0 に着地)。
3. 各 PE が `m` を `@distribute` した per-PE の id (0..4095) で差し替える。
4. `@collect size 4` で 各 PE の `{x, y, z, id}` を PDM に集める (成分ごと SoA)。
5. ホストが成分ごと出力を struct に組み直し、`x,y,z` は broadcast 値 (全 PE 同一)、
   `m` は per-PE の id、を確認する。

要点は 4 の collect = per-PE データを PDM に集める部分。これは `@collect` が
`l1bmd` (PE -> L1BM) -> `l2bm@` (L1BM -> L2BM) -> `mvp` (L2BM -> PDM) に展開して行い、
出力は成分ごと (SoA) `[x×4096][y×4096][z×4096][m×4096]` に固定される (選べない)。
broadcast の源は AoS (struct 連続 4 u64) なので、往復は AoS -> SoA でレイアウトが変わる。
詳細は `docs/broadcast-collect-layout.md`。

## ファイル構成

| ファイル | 役割 |
|---------|------|
| `broadcast_collect._vsm` | `@broadcast size 4` + `lpassa` で collect 源に {x,y,z,id} を並べる + `@collect size 4` |
| `broadcast_collect.param` | slot 8 (broadcast 源)、16 (distribute id)、24 (collect 出力) の PDM 番地と wait tag |
| `broadcast_collect.stparam` | `:pe_shape (4096)` `:pe_local (4)` |
| `test_broadcast_collect.c` | struct を送信 (broadcast 非トリガー、id をトリガー) -> exec -> recv -> 成分ごと出力を照合 |
| `build.ninja` | vsmlink + assemble3 + host ビルド + test target |

## メモリレイアウト (PDM、u64 単位)

| 番地 | 内容 | 並び |
|---|---|---|
| 0 | broadcast 源 (x,y,z,m の 1 struct) | 連続 4 u64 (AoS) |
| 4096 | distribute 源 (per-PE の id) | `[id × 4096]` (成分ごと) |
| 8192 | collect 出力 | `[x×4096][y×4096][z×4096][m×4096]` (成分ごと SoA) |

## ビルド・実行

```
source scripts/overlay
ninja -C examples/29-broadcast-collect test-emu-lib   # emu:lib で往復検証
ninja -C examples/29-broadcast-collect test-device    # 実機で検証
```

## 設計メモ

- broadcast は GRF0 の +2 刻みに着地 (`$lr16`=x `$lr18`=y `$lr20`=z `$lr22`=m)。distribute は
  LM0 (`$lm0`) に着地。collect は 1 つのバンクの連続レジスタ (+2 刻み) から成分を読むので、
  `lpassa` で `{x, y, z, id}` を LM1 の `$ln4088` から並べ直してから `@collect size 4` する。
- 2 段 send: broadcast 源は非トリガー send、distribute 源をトリガー send (`#x10`) にして
  両ディレクティブの wait を同時に解除する (両領域が PDM に揃った状態で kernel が進む)。
- (おまけ) m を distribute でなく PE 自身の global_id から作る案は
  `docs/broadcast-collect-layout.md` の「おまけ」節を参照 (itof 命令が無いのでマジックナンバー法)。
