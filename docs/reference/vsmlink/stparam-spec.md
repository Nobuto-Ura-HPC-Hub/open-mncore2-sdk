# .stparam 仕様

構造パラメタファイル。S 式形式。`;` 以降は行コメント。
フィールド名・シンボル値はキーワード記法（`:` 接頭辞）で記述する。

## フィールド一覧

| フィールド | 型 | 必須 | 説明 |
|-----------|-----|------|------|
| `:version` | keyword | **yes** | 形式バージョン。先頭エントリ。現在は `:0.1` |
| `:ndim` | int | yes | 問題空間の次元数 (1, 2, 3) |
| `:pe_shape` | int list | yes | PE トポロジの分割形状。積 = 4096 |
| `:pe_local` | int list | yes | 1 PE あたりの格子点数（次元ごと）。現状は全次元 1 |
| `:data_size` | int list | no | 問題全体のデータ形状（次元ごと）。`data_edge` 判定に使用 |
| `:data_offset` | int list | no | このカーネルのデータ開始位置（次元ごと）。`data_edge` 判定に使用 |
| `:data_layout` | keyword | no | データの PE への配置パターン。省略時 `:any` |
| `:boundary` | keyword list | no | 各次元の境界条件 |
| ~~`:variables`~~ | list | no | **deprecated**。パーサから削除予定 |

## フィールド詳細

### :version

形式バージョン。先頭エントリに必須。

```lisp
(:version :0.0)
```

### :ndim

問題空間の次元数。pe_shape, pe_local, boundary の要素数はこれに一致する。

### :pe_shape

4096 PE をどういう形状に見るかの指定。積は必ず 4096。

```lisp
(:pe_shape (4096))        ; 1D
(:pe_shape (64 64))       ; 2D
(:pe_shape (16 16 16))    ; 3D
```

形状が決まると、各軸が対応する HW 階層（PE/MAB/L1B/L2B）は物理トポロジから導出される。

### :pe_local

1 PE あたりの格子点数。VPE が動くまでは全次元 1。

```lisp
(:pe_local (1))           ; 1D
(:pe_local (1 1))         ; 2D
```

### :data_size

問題全体のデータ形状。次元ごとの要素数。

```lisp
(:data_size (16384))          ; 1D: 16384 要素
(:data_size (8192 512))       ; 2D: 8192×512
```

pe_shape と data_size から各次元のチャンク数が決まる: N[d] = data_size[d] / pe_shape[d]。

### :data_offset

このカーネルのデータ開始位置。次元ごと。

```lisp
(:data_offset (0))            ; 1D: 先頭チャンク
(:data_offset (4096))         ; 1D: 2 番目のチャンク
(:data_offset (2048 0))       ; 2D: dim0 は 2 番目、dim1 は先頭
```

vsmlinker は data_size, data_offset, pe_shape から cross_chip 境界を判定する:
- 左 = :boundary  if data_offset[d] == 0
- 右 = :boundary  if data_offset[d] + pe_shape[d] == data_size[d]
- それ以外 = 隣あり（cross_chip ビットをセットしない）

### :data_layout

データを PE にどう配置するかのパターン名。

- `:any` — 配置に依存しない（独立計算）。省略時のデフォルト
- `:native` — MN-Core ネイティブ配置（将来追加予定。ステンシル等で必要）

### :boundary

各次元の境界条件。次元ごとに異なる条件を指定できる。

- （指定なし）— アプリが edge を自分で処理する。`@get_neighbor` は edge PE で不定値を返す
- `:clamp` — 端の PE の値を返す（範囲外は端で止まる）
- `:periodic` — 反対端の PE の値を返す（ラップアラウンド）

**廃止:**
- ~~`:fixed`~~ — 廃止。固定値が汎用的に決まらない
- ~~`:mirror`~~ — 廃止

2D/3D では次元ごとに異なる境界条件を指定する意味がある。
例: 円筒状の流体シミュレーションで x 方向は `:periodic`、y 方向は `:clamp`。

現在の実装状況: `:clamp`, `:periodic` は未実装。

`:boundary` は `:data_size` で指定した問題全体の端に適用される。
`:data_offset` と `:pe_shape` から、このカーネルがデータの端にいるかどうかを vsmlink が判定する。

```lisp
(:boundary (:periodic))            ; 1D: periodic
(:boundary (:clamp :clamp))       ; 2D: 両方 clamp
(:boundary (:periodic :clamp))    ; 2D: x=periodic, y=clamp
```


## 廃止フィールド (0.7.2 時点)

過去に存在したが現在のパーサから削除されたフィールド。古い `.stparam`
を流用するときの参考として記録する。

- `:offsets` — 廃止。アクセスパターンは `_vsm` 側の `@access_pattern`
  ディレクティブで宣言する方式に移行した。
- `dim_map` — 廃止。PE トポロジの分割形状は `:pe_shape`、1 PE あたりの
  格子点数は `:pe_local` で指定する。


## 例

### 1D vecadd（独立計算）

```lisp
;; 4096 PE を一列に使用。ステンシルなし。
((:version :0.0)
 (:ndim 1)
 (:pe_shape (4096))
 (:pe_local (1))
 (:data_layout :any))
```

### 2D vecadd（独立計算、pe_shape 変更）

```lisp
;; 同じ vecadd カーネルを 64x64 の 2D 配置で実行。
((:version :0.0)
 (:ndim 2)
 (:pe_shape (64 64))
 (:pe_local (1 1))
 (:data_layout :any))
```

### 3D vecadd（独立計算、pe_shape 変更）

```lisp
;; 同じ vecadd カーネルを 16x16x16 の 3D 配置で実行。
((:version :0.0)
 (:ndim 3)
 (:pe_shape (16 16 16))
 (:pe_local (1 1 1))
 (:data_layout :any))
```

独立計算（ステンシルなし）では pe_shape の選択は論理的な問題空間の見方のみに影響し、
カーネルの動作や data_layout には影響しない。
OpenACC C から利用する場合、アプリケーションの配列次元に合わせて pe_shape を設定する。

### 1D 3-point stencil

```lisp
;; 1D 3-point stencil
;; アクセスパターンは _vsm の @access_pattern で宣言
((:version :0.0)
 (:ndim 1)
 (:pe_shape (4096))
 (:pe_local (1))
 (:boundary (:periodic)))
```

### 2D 5-point stencil (jacobi)

```lisp
((:version :0.0)
 (:ndim 2)
 (:pe_shape (64 64))
 (:pe_local (1 1))
 (:boundary (:periodic :clamp)))
```

## SDK examples 構成

対象: 社外の OpenACC C 実装者向けチュートリアル。

| # | 名称 | pe_shape | 内容 |
|---|------|----------|------|
| 01 | roundtrip | (4096) | nop いってかえって。.param アドレス解決の最小検証 |
| 02 | vecadd | (4096) | 1D dvadd E2E (send a,b → exec → recv c → 検証) |
| 03 | vecadd-2d | (64 64) | 同一カーネル、2D pe_shape。data_layout 不要の例 |
| 04 | vecadd-3d | (16 16 16) | 同一カーネル、3D pe_shape。data_layout 不要の例 |
| 05 | stencil1d | (4096) | @stencil + halo 交換 E2E |
| 06 | jacobi2d | (64 64) | 2D 5-point .stparam パース確認 |
| 07 | box2d | (64 64) | 2D 9-point .stparam パース確認 |
| 08 | himeno3d | (16 16 16) | 3D 7-point .stparam パース確認 |

01〜05: E2E（host-dma 連携、emu:lib で実行可能）
06〜08: .stparam パース確認のみ（カーネル未対応）
