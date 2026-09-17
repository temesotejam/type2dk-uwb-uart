# Type2DK 実測UWB → CoreS3 UART v1

CoreS3 `1.3.0-range`、Type2DK `TYPE2DK_RANGE_UART_V1`。
19番が直接測定した19–21・19–22の距離と、19番自身の加速度を送信します。
21–22の距離と21・22の加速度は、この版ではCoreS3に転送しません。

以前の3ノード版v3の測距・加速度・再接続処理を基に、失敗していた
`UwbApi_SendData`の周期呼び出しと、その成否に依存した測距再起動を外しました。
無線のアプリデータ転送機能の修正版ではありません。
このリポジトリで配布する3台分のBINを組み合わせてください。

## 書き込み

CoreS3と2DKをつなぐ信号線を外し、各機器を書き込みます。
COM番号はPCで確認してください。19・21・22は今回割り当てる基板の役割です。

| 基板 | BIN | 役割 |
|---|---|---|
| 19 | `2dk_range_node19_uart_v1.bin` | 測距結果をPIO13から送信 |
| 21 | `2dk_range_node21_peer_v1.bin` | 19・22への測距開始側 |
| 22 | `2dk_range_node22_peer_v1.bin` | 19への開始側、21への応答側 |
| CoreS3 | 書き込みページの1.3.0-range | GPIO2で受信して表示 |

```powershell
.\DK6Programmer.exe -V 0 -P 1000000 -s COM19 -Y -v -p .\2dk_range_node19_uart_v1.bin
.\DK6Programmer.exe -V 0 -P 1000000 -s COM21 -Y -v -p .\2dk_range_node21_peer_v1.bin
.\DK6Programmer.exe -V 0 -P 1000000 -s COM22 -Y -v -p .\2dk_range_node22_peer_v1.bin
```

## 配線

成功したUART診断版と同じ配線です。電源を切って接続してください。

| 19番Type2DK Rev.4.1 | CoreS3 PORT A |
|---|---|
| TP8の2番、PIO13/SWDIO | 黄、GPIO2/RX |
| TP8の3・5・9番のいずれか、GND | 黒、GND |
| PIO12 | 接続不要 |
| 接続しない | 白GPIO1・赤5V |

UARTは38400 bps・8N1。外付けプルアップ不要、CoreS3側の内蔵プルアップも無効です。
基板の4組の電源ジャンパはそのまま、各基板をUSB給電します。
21番・22番はCoreS3へ有線接続しません。

## 実機試験

1. 初めは19番と21番だけを起動し、22番は電源を切ります。CoreS3を起動します。
2. UWB初期化終了後、CoreS3がRECEIVINGとなり、19–21の距離とupdatesが更新されるか確認します。
   19–22が `--.-- m` なのは、この段階では正常です。
3. 21番を少し動かし、表示距離が追従するか確認します。CLEAR後に60秒観測します。
4. 22番も起動し、19–22も更新されるか確認します。
5. CoreS3の `RANGE_STAT`・`RANGE_DATA` と、19番の `HEALTH`・`RANGE_UART` を共有してください。

**CoreS3のUSBログは115200 bps。2DKのUSBログは3000000 bpsです。**
2DKのUSB速度は、UART診断版の115200 bpsから変わります。
2DKのSW1を押すと `BOOT,TYPE2DK_RANGE_UART_V1` が出ます。

目安は受信約5フレーム/秒、BAD・missing・frame_error・overflowが増えず、
接続した相手のupdatesとr_okが継続増加することです。
測距の200 ms設定と結果送信の200 ms周期は別なので、距離の実際の更新率は
updatesの増分で確認してください。RF条件によるr_badとUARTのbadは別の指標です。

## 表示・ログ

- 距離はcm単位の実測値をmに変換して表示します。無効な値を0 mにしません。
- `valid`のbit0=19–21、bit1=19–22、bit2=19番加速度。
- `age_at_tx_ms`は送信スナップショット時点の測定経過時間です。
  CoreS3は受信後の経過時間と500 msの転送余裕を加え、3秒を超える値を非表示にします。
  UART正常受信が1秒途絶えた場合も非表示になります。
- `d19_21_cm`等は最後の保存値です。ログ解析でも必ずvalidと組み合わせてください。
- `updates`は各距離の成功回数。`r_ok/r_bad`は19番で処理した成功/失敗回数。
- `session/reason`は2セッションの状態/理由コード。2がActive、3がIdle。
- `irq_us`は前回までの送信で観測した最大の割り込み停止区間の概算。
  通常は約235 µsで、命令オーバーヘッドを数µs含みます。外部測定値ではありません。
- `tx_frame_ms`は直前のフレーム送信所要時間、`tx_overruns`は200 ms周期を守れなかった累計。
- `max_gap_ms`はCoreS3アプリが正常受信を処理した時刻の最大間隔です。
  電線上のビット間隔や測距間隔そのものではありません。
- CLEARはCoreS3の受信統計をリセットします。2DKの測距累計はリセットしません。
- 送信元の乱数boot IDで再起動を検出します。同一boot IDでの重複・逆順フレームでは表示の鮮度を更新しません。

## 測距との共存

FreeRTOSのSysTickと既存の周辺タイマー設定を変更せず、DWTのCPUサイクルカウンタを利用します。
UARTは優先度1の別タスク、アプリ測距処理は優先度4です。
1バイトの開始・データ8ビットのみIRQを停止し、ストップビット中に再開します。
4バイトごとに1 tick（このSDKでは5 ms）待ち、64バイトを200 msごとに送ります。
途中で長く待たされ、スナップショットから400 msを超えたフレームは送信を打ち切ります。
DWTが動かなければ送信を中止し、USBのRANGE_UARTにfaultを出します。
測距コールバックは状態更新だけを行い、UART送信や待機はしません。

同条件でUARTなしとの比較が必要な場合は、19番だけを
`2dk_range_node19_no_uart_v1.bin`へ変更します。
測距設定は同じで送信タスクを作りません。CoreS3は受信停止となります。
USBのHEALTHで同じ時間のR_OK/R_BADの増分を比較し、終わったらUART版に戻します。

## 検証範囲

全4種類の2DK BINのビルド・起動ヘッダ/CRC/サイズ検証、
Cの測距コールバック/回復処理のホスト試験、C++受信器の破損・挿入・削除・
連番周回・再起動・鮮度判定試験を行っています。
既存ログを用いた再生試験は過去の測距処理の確認であり、新版の実機試験ではありません。
**2026-09-17に測距併用版も実機確認しました。** 提示された32秒間では、UART正常受信160回、
2距離の測距成功が各160回、新たな通信エラー・欠落・測距失敗は0回でした。
[原文ログと集計](../../validation/hardware/README.md)を保存しています。

## 再ビルド

ユーザー提供の `UWBIOT_SR040_v04.03.14_MCUx/uwbiot-top` を別ディレクトリへ展開します。
SDK本体・SDKのプリビルドライブラリはこのリポジトリに含めていません。
この版はxPack Arm GNU 13.2.1-1.1、newlib-nanoでビルドしました。

```sh
patch --batch --forward -d "$SDK_PATH" -p0 < type2dk/ranging/build/2dk_prebuilt_v04.03.14.patch
python type2dk/ranging/build/apply_sdk_changes.py "$SDK_PATH"
python type2dk/ranging/build/build.py --sdk "$SDK_PATH" --gcc-bin "$ARM_GCC_BIN" --node 19 --out build/range19
```

21・22は`--node`を変更します。19番の比較版は`--uart-off`を追加します。
SDKの無線設定はCh5、DS-TWR、SP1、SFD2、preamble10、slot2400、25 slots、interval200 ms。
SDKのRadioConfig/GroupDelayとSR040更新の初期化を引き継いでいます。
付属のlicenses/SCR.txt・EULA.pdfと各コンポーネントのライセンスが適用されます。
