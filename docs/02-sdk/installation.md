# SDK のインストール

MN-Core2 SDK をシステムにインストールします。

## 前提条件

- Linux システム（Rocky Linux 8 以降推奨）
- 必要なディスク容量：（容量記載予定）
- 管理者権限または sudo アクセス

## インストール手順

### 1. SDK の取得

MN-Core2 SDK は GitHub Releases から各 kit の tarball をダウンロードできます。

```bash
# GitHub Releases から必要な kit をダウンロード
cd /path/to/install/directory
```

### 2. 依存順でのインストール

各 kit には依存関係があります。[SDK の概要](./overview.md)に示す順序でインストールしてください。

各 kit の詳細なインストール方法はそれぞれのドキュメントを参照してください。

### 3. 環境変数の設定

インストール完了後、以下のように環境変数を設定します：

```bash
# SDK のルートディレクトリ
export MN_CORE2_SDK=/path/to/sdk

# パスの設定
export PATH=$MN_CORE2_SDK/bin:$PATH
```

## インストール確認

インストールが正常に完了したことを確認します：

```bash
# バージョン確認
mnc2-inspect --version
```

## トラブルシューティング

（トラブルシューティング情報は記載予定）

## 次のステップ

インストール完了後は、[libmnc2 を使った開発](../03-libmnc2/)に進んでください。
