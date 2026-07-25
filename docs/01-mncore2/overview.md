# MN-Core2 の概要

MN-Core2 は、PFN（Preferred Networks）が開発した高性能計算向けのプロセッサおよびシステムです。

## 特徴

（詳細記載予定）

## MN-Core2 SDK について

MN-Core2 SDK は、MN-Core2 を使用したアプリケーション開発に必要なツール、ライブラリ、ドキュメントを一式提供します。

### 構成要素

SDK は以下の複数の kit で構成されています：

- **sdk-base-kit** — 基本的なツールチェーンと環境
- **mncore2-emuenv-kit** — エミュレーション環境
- **mnc2-inspect-kit** — システムの詳細情報を取得するツール
- **libmnc2-kit** — MN-Core2 のプログラミングライブラリ（このチュートリアルの対象）
- **vsmlink-kit** — コンポーネント間の連携ツール
- **mncl-kit** — コンパイラおよび関連ツール
- **openacc-c-kit** — OpenACC コンパイラ

各 kit は依存関係を持つため、インストール時の順序に注意が必要です。詳細は[SDK のインストール](../02-sdk/installation.md)を参照してください。

## 次のステップ

準備ができたら、[SDK について](../02-sdk/)に進んでください。
