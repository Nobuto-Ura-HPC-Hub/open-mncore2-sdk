# PFCP で使う

PFCP（Preferred Computing Platform）では、SDK を入れたコンテナイメージから pod を作り、
MN-Core 2 の実機で動かせます。インストールは要りません。

イメージは AWS の ECR に置いてあり、PFCP から取得できます。

```
333975443171.dkr.ecr.ap-northeast-1.amazonaws.com/open-mncore2-sdk:v0.6
```

## Kubernetes と pod（Docker を使ったことがある人へ）

PFCP の実行環境は Kubernetes（k8s）です。Docker と同じ形式のコンテナイメージを動かしますが、
使い方と言葉が少し違います。

- **pod**: 実行時の実体（最小の単位）です。pod を作って中に入れば、Docker のコンテナと同じように
  bash で作業できます。
- **Deployment**: pod を作り、消えたら作り直す役目です。このページでは、Deployment を通して pod を
  作ります。
- **イミュータブル**: pod は、作り直されると中身が元のイメージに戻ります。pod の中で作ったファイル
  （`/root` など）は消えます。残したいファイルは、pod の外へ取り出してください。
- **ServiceAccount**: pod が ECR からイメージを取得するときに使う身分です。
- **kubectl**: Kubernetes を操作するコマンドです。

| したいこと | Docker | kubectl |
|------------|--------|---------|
| 起動する | `docker run` | `kubectl apply -f deployment.yaml` |
| 状態を見る | `docker ps` | `kubectl get pods` |
| 中に入る | `docker exec -it <コンテナ> bash` | `kubectl exec -it <pod 名> -- bash` |
| ファイルを取り出す | `docker cp` | `kubectl cp` |
| 止める | `docker stop` | `kubectl scale deployment <名前> --replicas=0` |

## イメージに入っているもの

| もの | 場所・版 |
|------|----------|
| OS | Ubuntu 24.04 |
| Open MN-Core 2 SDK v0.6 | `/opt/_mncore2-sdk-v0.6`（`/opt/mncore2-sdk` から参照） |
| PFN の `mncore-sdk` | 0.8（`assemble3`、`gpfn3_package_main` を含む） |
| PFN の `libgpfn3-0` / `libgpfn3-dev` / `gpfn3-smi` | 1.0-1 / 1.0-1 / 1.1-1 |
| ビルドの道具 | `build-essential`、`ninja-build`、`xxd`、`gawk`、`pkgconf`、lit 23.1.5 |

## 1. ServiceAccount を作る

PFCP がこのイメージを ECR から取得するための ServiceAccount を、自分の名前空間に作ります。
取得に使う認証情報は、PFCP の仕組み（Image pull secrets provisioner）が自動で作ります。

```yaml
apiVersion: v1
kind: ServiceAccount
metadata:
  name: open-mncore2-sdk
  namespace: <自分の名前空間>
  annotations:
    imagepullsecrets.preferred.jp/registry: "333975443171.dkr.ecr.ap-northeast-1.amazonaws.com"
    imagepullsecrets.preferred.jp/audience: "sts.amazonaws.com"
    imagepullsecrets.preferred.jp/aws-role-arn: "arn:aws:iam::333975443171:role/open-mncore2-sdk-pull"
```

```bash
kubectl apply -f serviceaccount.yaml
```

## 2. pod を作る

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: <名前>-open-mncore2-sdk
  namespace: <自分の名前空間>
spec:
  strategy:
    type: Recreate
  selector:
    matchLabels:
      app: <名前>-open-mncore2-sdk
  template:
    metadata:
      labels:
        app: <名前>-open-mncore2-sdk
    spec:
      priorityClassName: shared-best-effort
      serviceAccountName: open-mncore2-sdk
      containers:
      - name: app
        image: 333975443171.dkr.ecr.ap-northeast-1.amazonaws.com/open-mncore2-sdk:v0.6
        workingDir: /root
        command: ["sleep", "infinity"]
        resources:
          limits:
            preferred.jp/mncore2: "1"
            memory: 60Gi
          requests:
            preferred.jp/mncore2: "1"
            memory: 60Gi
```

利用者ごとに変える項目:

| 項目 | 書く場所 |
|------|----------|
| 名前空間 | `metadata.namespace` |
| Deployment の名前 | `metadata.name` |
| pod のラベル | `selector` と `template` の `app:`（2 か所を同じ値にする） |
| 優先度クラス | `priorityClassName`（自分の名前空間で使えるもの） |
| MN-Core 2 の台数 | `resources` の `preferred.jp/mncore2` |
| メモリ | `resources` の `memory`（自分の名前空間のクォータの範囲で） |

```bash
kubectl apply -f deployment.yaml
kubectl get pods -n <自分の名前空間>      # Running になるのを待つ
```

## 3. pod に入って動かす

```bash
kubectl exec -it <pod 名> -n <自分の名前空間> -- bash

# pod の中で
source /opt/mncore2-sdk/bin/activate
sdk-versions
sdk-examples libmnc2 ~/work
cd ~/work/libmnc2-*/01-nop
ninja
ninja test-device       # 実機で実行
```

サンプルの種類と、エミュレータでの実行は [道具の流れとサンプル](tools.md) を参照してください。

## 注意

- **`/root` は pod を作り直すと消えます。** コンテナの再起動、pod の作り直し（イメージの変更、
  台数を 0 にする等）で中身がなくなります。残したいファイルは pod の外へ取り出してください。

  ```bash
  kubectl cp <自分の名前空間>/<pod 名>:/root/work ./work
  ```

- **使わないときは止めます。** pod は、何もしていなくても MN-Core 2 とメモリを確保し続けます。

  ```bash
  kubectl scale deployment <名前>-open-mncore2-sdk --replicas=0 -n <自分の名前空間>   # 止める
  kubectl scale deployment <名前>-open-mncore2-sdk --replicas=1 -n <自分の名前空間>   # 再開
  ```
