# SDK の概要

**Open MN-Core2 SDK** は、MN-Core2 を使用したアプリケーション開発に必要なすべてのコンポーネントを提供します。これは Preferred Networks, Inc. の公式配布ではない非公式プロジェクトです。

## SDK の構成

SDK は複数の kit（パッケージ）で構成されています。各 kit は特定の機能を担当します。

### 依存関係

各 kit には依存関係があります。インストール時は以下の順序に従う必要があります：

1. **sdk-base-kit** — 基盤となるツール
2. **mnc2-inspect-kit** — ハードウェア情報取得ツール
3. **libmnc2-kit** — プログラミングライブラリ
4. **vsmlink-kit** — コンポーネント連携ツール
5. **mncl-kit** — コンパイラ

エミュレータ（`assemble3`、`gpfn3_package_main`）は、kit ではなく PFN の apt パッケージ
`mncore-sdk` から入れます。openacc-c-kit は当面提供しません（v0.6.0 から）。

### 各 kit の役割

（各 kit の詳細説明は記載予定）

## システム要件

（システム要件の詳細は記載予定）

## 次のステップ

実際のインストール手順は[SDK のインストール](./installation.md)を参照してください。
