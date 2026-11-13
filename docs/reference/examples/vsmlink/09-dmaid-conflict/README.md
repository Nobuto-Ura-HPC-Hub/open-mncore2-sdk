# examples/09-dmaid-conflict

DMA タグ衝突の挙動検証 example。

## 何を検証するか

ホストが、 vsmlink の予約タグ (kernel の中の転送の待ちに vsmlink が使うタグ) と同じ値の dmaid で送ると、 タグが衝突して失敗することを確かめる。 「衝突が起きたら失敗する」 ことを期待した negative test。

ホストは 2 ケースを実行する。

1. dmaid=0x10 (`.param` の `:send_wait_tag` と対応) → 成功することを期待する
2. dmaid=予約タグ → 失敗することを期待する

予約タグの値は `.param` の `:reserved_wait_tag(s)` で決まる (0.7.2 で外部化。 この example の `roundtrip.param` では `#x3f`)。 vsmlink は `.param` の中での衝突を E101 エラーで検出する。 本 example が再現しているのは **ホストのコードが予約タグと同じ dmaid で送信するパス** であり、 vsmlink からは見えない領域。

### ホストの値と `.param` の値のつながり

以前のホストは予約タグの値 (`0x23`) を数値で直接書いており、 予約タグが `.param` に移った後も `0x23` のままだった。 いまの kernel は `0x3f` を使うので、 衝突を再現していなかった。

そこで値の出どころを `roundtrip.param` だけにした。 `build.ninja` が `:reserved_wait_tag` の値を読んで `_build/reserved_wait_tag.h` (`RESERVED_WAIT_TAG`) を生成し、 ホストはそれを include する。 `.param` の予約タグを変えれば、 ホストの値も build で変わる。

### 前提: kernel が予約タグで待っていること

衝突が起きるのは、 kernel が中の転送で予約タグを待つときだけである。 いまの生成物では、 `@distribute` の PDM -> L2BM の転送の後と、 `@collect` の途中の転送の後に `wait i3f` がある (`@collect` の最後の転送は受信のタグ `0x1e` で待つ)。 `build.ninja` は、 生成した `_build/roundtrip.vsm` に `wait i<予約タグ>` があるかを確かめ、 無ければ build-e2e / test-device の前に止める。

## backend 別の状態

| backend | 期待挙動 | 実状 | lit マーキング |
|---|---|---|---|
| device (実機) | ケース 2 が失敗 → テスト通り PASS | 未確認 (ケース 2 を今の予約タグに直した後、 実機で確かめていない) | `# REQUIRES: device, assemble3` |
| emu:lib | (動作対象外) | emu:lib は async wait tag の衝突を検出しない | `# UNSUPPORTED: true` |

emu:lib は async wait tag の衝突を検出しない実装上の制約があり、 device 側は厳密に検出する。

build.ninja からも `test-emu-lib` target を削除済み (`ninja test-emu-lib` は unknown target で即時失敗)。

## なぜ emu:lib では UNSUPPORTED か

emu:lib の HW シミュレーションが async dmaid 衝突を検出しないという **真の backend 制約** に該当するため。 テストは走らせれば成功扱いされてしまい意味がないので、 lit に走らせない (UNSUPPORTED)。 隠蔽ではなく、 「この backend では動作しない」 を正直に表現している。
