# type2dk-uwb-uart

Murata Type2DKのUWB測距結果を、PIO13から送信専用UARTでM5Stack CoreS3へ渡すプロジェクトです。
**19–21・19–22の距離とnLos・通信診断情報を、38400 bpsで毎秒5回送信します。**

[書き込み・ダウンロードページ](https://temesotejam.github.io/type2dk-uwb-uart/) ·
[配線・書き込みの詳しい手順](type2dk/ranging/README.md) ·
[実機確認記録](validation/hardware/README.md)

## 現在の版：1.4.0-range / Type2DK v2

距離に加え、相手ごとのnLos生値・測距結果コード・失敗累計・最大更新間隔・
セッション状態・開始試行回数を記録します。加速度の初期化・読み取り・転送・表示は削除しました。
CoreS3はUART受信ごとに約5回/秒ログを出します。

| 機器 | ファームウェア | 役割 |
|---|---|---|
| 19番Type2DK | `2dk_range_node19_uart_v2.bin` | 2距離と診断情報をUART送信 |
| 21番Type2DK | `2dk_range_node21_peer_v2.bin` | 19・22との測距 |
| 22番Type2DK | `2dk_range_node22_peer_v2.bin` | 19・21との測距 |
| M5Stack CoreS3 | `1.4.0-range` | UART受信・距離と通信状態の表示 |

**4台とも更新してください。** CoreS3 v1.4.0と19番v2は96バイト形式の組み合わせです。
nLosは生値で記録し、遮蔽の確定判定には使いません。SR040での判定性能は実機未確認です。
RSSIは通常測距通知から取得していないため、未取得と明示します。
21–22の距離はCoreS3へ転送しません。COM番号は環境に応じて確認してください。

[新しい4台分のBINセット](https://temesotejam.github.io/type2dk-uwb-uart/firmware/type2dk-uwb-uart-1.4.0.zip)

旧v1（CoreS3 1.3.0-range）は2026-09-17に実機確認済みです。提示された32秒間で
UART正常受信160回、2距離の成功各160回、新たなエラー・欠落は0回でした。
その記録と固定BINは下記に残しています。v2の実機確認結果ではありません。

## 配線

19番Type2DK EVK Rev.4.1だけをCoreS3へ接続します。接続作業は電源を切って行ってください。

| Type2DK・TP8 | CoreS3・PORT A |
|---|---|
| 2番：PIO13 / SWDIO（TX） | 黄：GPIO2（RX） |
| 3・5・9番のいずれか：GND | 黒：GND |
| PIO12 | 接続不要 |
| 接続しない | 白：GPIO1、赤：5 V |

38400 bps・8N1・フロー制御なし。外付けプルアップ不要、CoreS3側の内蔵プルアップも無効です。
各基板をUSB給電し、2DKの4組の電源ジャンパは取り付けたままにします。
PIO13をGPIOとして使うソフトウェアUARTなので、同じピンでI2Cを同時使用しません。

## 書き込みとログ

CoreS3は[書き込みページ](https://temesotejam.github.io/type2dk-uwb-uart/)から書き込めます。
2DKは対応するBINをダウンロードし、[Windows用GUI](https://temesotejam.github.io/type2dk-uwb-uart/tools/dk6-gui/type2dk-programmer-gui-v1.0.0.zip)で
「DK6Programmer.exe・COMポート・任意のBIN」を選んで「書き込み・照合」を押します。
既存のDK6Programmerフォルダを指定して使います。Pythonやコマンド入力は不要です。
[GUIの使い方・検証範囲](tools/dk6-gui/README.md)を参照してください。

コマンドから実行する場合は次の指定です。

```powershell
.\DK6Programmer.exe -V 0 -P 1000000 -s COM19 -Y -v -p .\2dk_range_node19_uart_v2.bin
.\DK6Programmer.exe -V 0 -P 1000000 -s COM21 -Y -v -p .\2dk_range_node21_peer_v2.bin
.\DK6Programmer.exe -V 0 -P 1000000 -s COM22 -Y -v -p .\2dk_range_node22_peer_v2.bin
```

| 通信 | 速度 |
|---|---:|
| 2DK → CoreS3 | 38400 bps |
| CoreS3 USBログ | 115200 bps |
| 2DK測距版 USBログ | **3000000 bps** |

CoreS3は`RANGE_STAT`と`RANGE_DATA`、19番は`HEALTH`と`RANGE_UART`を出力します。
測定できていない値や古くなった値は画面で`--`と表示します。

## 保存したBIN

- [動作確認時のBINセット](https://temesotejam.github.io/type2dk-uwb-uart/firmware/type2dk-uwb-uart-tested-1.3.0.zip)
- [動作確認時のCoreS3統合BIN](firmware/known-good/cores3-range-merged.bin)
- [動作確認時の版・移行元・SHA256](firmware/known-good/snapshot.json)
- [2DK測距版のSHA256](type2dk/ranging/firmware/SHA256SUMS.txt)

2DKのBINは`.bin.b64`として保存し、公開時にSHA256と起動ヘッダを検証して復元します。
動作確認時のBINセットは固定した4本と手順・ライセンスを含みます。
通常の書き込みボタンはこのリポジトリのコードからビルドしたCoreS3版を使用します。

## ソースと再ビルド

| パス | 内容 |
|---|---|
| `src/range_main.cpp` | CoreS3の実測値受信・表示 |
| `include/range_frame.h`、`include/range_stream.h` | 96バイトの通信形式・受信処理 |
| `type2dk/ranging/source/` | 2DKの測距・通信診断・UART送信 |
| `type2dk/ranging/build/` | SDKへの変更・GNU Armでのビルド |
| `type2dk/src/`、`src/uart_main.cpp` | 単独UART診断版（20バイト・50回/秒） |
| `validation/hardware/` | 実機の原文ログ・集計結果 |
| `tools/dk6-gui/` | 任意のBINを選べるWindows用書き込みGUI |
| `.github/workflows/build.yml` | テスト・CoreS3ビルド・BIN検証・ページ公開 |

CoreS3はPlatformIO 6.1.18を使用します。

```sh
pip install platformio==6.1.18
pio run
python scripts/package_firmware.py --sha local
```

2DK測距版は、ユーザー所有の`UWBIOT_SR040_v04.03.14_MCUx` SDKとxPack Arm GNU 13.2.1-1.1を使用します。
[再ビルド手順](type2dk/ranging/README.md#再ビルド)を参照してください。
SDK本体は含めていません。CIではCoreS3をビルドし、2DKは保存済みBINを検証します。

診断版は[UART診断ページ](https://temesotejam.github.io/type2dk-uwb-uart/uart.html)と
[診断版の説明](type2dk/UART_README.md)を参照してください。
測距への影響を比較するための`2dk_range_node19_no_uart_v2.bin`も残しています。

## 移行元とライセンス

[M5stackCORES3I2CdemoUWBの3a336da](https://github.com/temesotejam/M5stackCORES3I2CdemoUWB/tree/3a336da2c2b6b4a0712ef994446019071b0dca4b)
からUART部分を独立させました。旧版の実機用BINは固定して残しています。現在はv2の診断機能を追加しています。

Type2DK用BINにはNXP SDKなどのコンポーネントを含み、それぞれのライセンスが適用されます。
[構成・ライセンス通知](type2dk/ranging/licenses/)を参照してください。
