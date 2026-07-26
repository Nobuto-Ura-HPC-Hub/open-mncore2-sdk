# examples/09-dmaid-conflict

DMA タグ衝突の挙動検証 example。

## 何を検証するか

ホスト側が `dmaid=0x23` (= vsmlink 内部の予約 wait タグと同値) を使うと、 タグが衝突して recv が失敗することを確かめる。 「衝突が起きたら失敗する」 ことを期待した negative test。

vsmlink 内部で予約していた wait タグは 0.7.2 で `.param` の `:reserved_wait_tag(s)` として外部化された。 vsmlink は `.param` 入力レベルでの衝突を E101 エラーで検出する。 本 example が再現しているのは **ホストコードが衝突する dmaid で送信するパス** であり、 `.param` 入力チェックではカバーされない領域。

## backend 別の状態

| backend | 期待挙動 | 実状 | lit マーキング |
|---|---|---|---|
| device (実機) | 衝突を検出して recv 失敗 → テスト通り PASS | device pod で PASS 確認済み | `# REQUIRES: device, assemble3` |
| emu:lib | (動作対象外) | emu:lib は async wait tag の衝突を検出しない | `# UNSUPPORTED: true` |

emu:lib は async wait tag の衝突を検出しない実装上の制約があり、 device 側は厳密に検出する。

build.ninja からも `test-emu-lib` target を削除済み (`ninja test-emu-lib` は unknown target で即時失敗)。

## なぜ emu:lib では UNSUPPORTED か

emu:lib の HW シミュレーションが async dmaid 衝突を検出しないという **真の backend 制約** に該当するため。 テストは走らせれば成功扱いされてしまい意味がないので、 lit に走らせない (UNSUPPORTED)。 隠蔽ではなく、 「この backend では動作しない」 を正直に表現している。
