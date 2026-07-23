# CI ベースイメージ mncore2-emuenv-base

`ghcr.io/nobuto-ura-hpc-hub/mncore2-emuenv-base` のビルド定義。
`.github/workflows/test-emulator.yml` がこのイメージをコンテナとして使い、
リリース済み kit の CI テスト(`ci/test-sdk.sh`)を実行する。

## 経緯

このビルド定義は元々どこにも記録が残っておらず(open-mncore2-sdk / packaging /
release / PFCP のいずれにも Dockerfile・ビルド手順書が存在しなかった)、
ローカルに残っていた稼働中イメージ(`:20240826`)の `docker history --no-trunc`
からビルド手順を復元して作成した(2026-07-23)。

管轄・移管経緯が未文書化だった問題自体は別件として残っている
(`../../packaging/docs/handover.md` 参照)。今後はこのディレクトリを一次情報とする。

## 中身

- Ubuntu イメージに `libgomp1` を追加(PFN バイナリの実行時依存)
- PFN 公式配布のエミュレータバイナリ2種を `/usr/local/bin` に配置:
  - `assemble3` — MN-Core 2 アセンブラ
  - `gpfn3_package_main` — MN-Core 2 パッケージエミュレータ

## タグ

| タグ | ベース OS | 用途 |
|------|-----------|------|
| `20240826` | Ubuntu 22.04 | 旧版。GCC 11 系で `GLIBCXX_3.4.32` を提供できず、libmnc2-kit 同梱の `libgpfn3.so` とリンクエラーになる(ABI 不一致)。**削除しない**(タグを消すと CI・ユーザ環境が壊れるため) |
| `20240826-ubuntu24.04` | Ubuntu 24.04 | 現行。実機(りょうすさんの pod、Ubuntu 24.04.1 + GCC 13.3.0)に合わせて ABI 不一致を解消したもの |

タグ名の `20240826` は PFN エミュレータ配布物の元バージョン(日付)、`ubuntu24.04` はベース OS を示す。

## PFN バイナリの入手元

PFN 公式配布物のためライセンス上の理由でリポジトリに同梱しない(`.gitignore` 済み)。
ビルド前にビルドコンテキストへ配置する。

- 正式な入手先: https://projects.preferred.jp/mn-core/#resources
- 手元にある場所(2026-07-23 時点): `../../pfn_public/distfiles/mncore2_emuenv_20240826.tar.xz`
- 期待 SHA256: `0fe4f84bab5dec7132228c710c391dd4e7e6f3a84baf32acff47e95947cbefe2`
  (`mncore2-emuenv-kit` の `install.sh` が検証に使う値と同一)

## ビルド手順

```bash
cd ci/emuenv-base

# 1. PFN tar.xz を配置し、SHA256 を確認
cp ../../../pfn_public/distfiles/mncore2_emuenv_20240826.tar.xz .
sha256sum mncore2_emuenv_20240826.tar.xz
# 期待値: 0fe4f84bab5dec7132228c710c391dd4e7e6f3a84baf32acff47e95947cbefe2 と一致すること

# 2. ビルド
docker build -t ghcr.io/nobuto-ura-hpc-hub/mncore2-emuenv-base:20240826-ubuntu24.04 .
```

## 検証

```bash
# バイナリが動くか(スモークテスト、ci/test-sdk.sh と同じ内容)
docker run --rm ghcr.io/nobuto-ura-hpc-hub/mncore2-emuenv-base:20240826-ubuntu24.04 bash -c "
  echo 'd get \$lm0n0c0b0m0p0 1' > /tmp/s.vsm
  assemble3 /tmp/s.vsm > /tmp/s.asm
  gpfn3_package_main -i /tmp/s.asm -d /tmp/d.txt
  grep DEBUG-LM0 /tmp/d.txt
"

# ABI が解消したか(22.04 版では GLIBCXX_3.4.32 が出力されなかった)
docker run --rm ghcr.io/nobuto-ura-hpc-hub/mncore2-emuenv-base:20240826-ubuntu24.04 \
  bash -c "strings /usr/lib/x86_64-linux-gnu/libstdc++.so.6 | grep GLIBCXX_3.4.32"
```

## push

```bash
gh auth token | docker login ghcr.io -u <github-user> --password-stdin
docker push ghcr.io/nobuto-ura-hpc-hub/mncore2-emuenv-base:20240826-ubuntu24.04
```

`:20240826` と同じく **private** のまま公開しないこと(PFN バイナリを含むため)。

## 今後の方針(未着手・次回イメージを作り直す際に反映する)

**この節は申し送りであり、現時点(`20240826-ubuntu24.04` を含むこの Dockerfile)には
まだ反映していない。** 現状は、ビルドツール(`build-essential` 等)をイメージに含めず、
CI ワークフロー(`test-emulator.yml`)側で毎回 `apt-get install` する方式のままである。

次回イメージを作り直す際は、ビルドツールもこの Dockerfile でバージョンを明示して
インストールし、イメージに焼き込む方式に変更する。

理由: ワークフロー側で都度 `apt-get install` する方式は、実行のたびに Ubuntu の
APT リポジトリからその時点で得られる gcc が入るだけで、実機の gcc バージョンへの
追従を保証するものではない。むしろ再現性が下がる。狙ったバージョンに確実に合わせ
たいなら、イメージのビルド時に固定する方が筋が良い(2026-07-23、りょうすさんとの
やりとりより)。

## 関連

- CI ワークフロー: `../../.github/workflows/test-emulator.yml`
- CI テスト本体: `../test-sdk.sh`
- ABI 不一致の調査記録: `../../../packaging/docs/handover.md`
