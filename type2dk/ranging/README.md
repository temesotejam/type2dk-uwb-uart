# Type2DK 測距・通信診断 UART v2

CoreS3 **1.4.0-range**、Type2DK **TYPE2DK_RANGE_UART_V2**。
19番が直接取得した19–21・19–22の距離と測距状態を送ります。
3台とも加速度の初期化・読み取りを行わず、加速度データを送信・表示しません。

## 更新する機器

**CoreS3とType2DKの19・21・22番をすべて更新してください。**
CoreS3 v1.4.0と19番v2は新しい96バイト形式です。旧v1との混在では受信できません。
21・22番もv2へ更新することで、3台とも加速度の処理を停止できます。
今回の版はビルド・ホスト試験済みですが、実機動作とnLosの判定性能は未確認です。

| 機器 | ファイル／書き込み方法 |
|---|---|
| CoreS3 | [書き込みページ](https://temesotejam.github.io/type2dk-uwb-uart/)の1.4.0-range |
| 19 | `2dk_range_node19_uart_v2.bin` |
| 21 | `2dk_range_node21_peer_v2.bin` |
| 22 | `2dk_range_node22_peer_v2.bin` |

Type2DKは既存のWindows書き込みGUIで、対応するCOMとBINを選び「書き込み・照合」を押します。
GUI・DK6Programmerの更新は不要です。信号線を外し、シリアルモニターを閉じて書き込みます。
書き込み後に各基板の電源を入れ直してください。

[全4台のBINセット](https://temesotejam.github.io/type2dk-uwb-uart/firmware/type2dk-uwb-uart-1.4.0.zip)
にはCoreS3統合BIN、Type2DKの3本、説明、SHA256、ライセンスを含みます。

## 配線とログ速度

配線はv1と同じです。電源を切って接続します。

| 19番Type2DK Rev.4.1 | CoreS3 PORT A |
|---|---|
| TP8の2番、PIO13/SWDIO | 黄、GPIO2/RX |
| TP8の3・5・9番のいずれか、GND | 黒、GND |
| PIO12 | 接続不要 |
| 接続しない | 白GPIO1・赤5V |

UARTは38400 bps・8N1、外付けプルアップ不要、CoreS3の内蔵プルアップも無効です。
4組の電源ジャンパは付けたまま、各基板をUSB給電します。
21・22番はCoreS3へ有線接続しません。

- **CoreS3のUSBログ：115200 bps**
- Type2DKのUSBログ：3000000 bps。起動時に `BOOT,TYPE2DK_RANGE_UART_V2` が出ます。

## CoreS3のログ

| 行 | 周期 | 内容 |
|---|---|---|
| `RANGE_DATA,v=2` | UART正常受信ごと、約5回/秒 | 2距離、鮮度、成功・失敗の累計 |
| `RANGE_LINK,v=2` | 同じ受信ごと、相手21・22の各1行 | nLos生値、測距状態、相手別カウンタ |
| `RANGE_STAT` | 約1回/秒 | UARTのCRCエラー、欠落、再起動、フレームエラー等 |
| `RANGE_HEALTH` | 約1回/秒 | UART送信時間・割り込み停止時間・周期超過 |

1秒あたり通常17行です。空行での分割や80文字での切り捨てを避け、ログをファイル保存してください。
`rx_ms`はCoreS3が受信処理した時刻、`tx_ms`は19番がスナップショットを作った時刻です。
時計は同期されていないので、この2値の引き算は伝送遅延を意味しません。
同じスナップショットの3行は `boot` と `seq` で対応付けます。
USBを開いている間の受信スナップショットが記録対象です。

### 距離とnLosの対応

| 項目 | 意味 |
|---|---|
| `valid` | bit0=19–21、bit1=19–22。両方有効なら **3**。遮蔽判定は含まない |
| `d19_21_cm` / `d19_22_cm` | 最後に成功した測距値。必ずvalidと合わせて使う |
| `age_at_tx_ms` | 最後の成功からスナップショットまでの経過時間、21/22の順 |
| `updates` | 相手ごとの成功累計、21/22の順 |
| `nlos_raw` | **最新の測距通知**の `phRangingMesr_t.nLos` をそのまま出した数値 |
| `saved_nlos_raw` | RANGE_DATAの**保存済み成功距離**と同じ通知に含まれていたnLos |
| `result_cm` | 最新通知の距離欄。失敗時の値を有効な距離として使わない |
| `result_seen` | その相手の通知を1回以上受け取ったか。0なら未受信 |
| `result_age_ms` | 最新通知からスナップショットまでの経過時間 |
| `uci_seq` | 最新通知のSDKシーケンス番号 `seq_ctr`。セッション再開で変化し得る |

**nLosの数値から「遮蔽あり／なし」を自動的に確定しません。**
このSDKにはnLosのフィールドと読み出し処理がありますが、SR040 v02.05.07が返す
値の有効性・意味・実際の遮蔽への追従は実機未確認です。
`result_seen=0`の初期値は255です。受信後も255やそれ以外の未確認値を加工せず残します。
`status=0x00`でも遮蔽や距離誤差がないことは保証されません。

RSSIの共通構造体フィールドは、このSDKのSR040用通常TWR通知では更新されません。
SR1XX向けの値を流用せず、`RANGE_HEALTH`に `rssi=UNAVAILABLE` と出します。
CIR・受信波形の収集やNLOS分類器はこの版には含みません。

### 通信・測距状態

| 項目 | 意味 |
|---|---|
| `result` | WAITING=未受信、STALE=古い/停止中、FAILED=状態コードが非0、NO_DISTANCE=成功コードでも距離65535、OK=新しい成功通知 |
| `status` | 最新測距通知の状態コード、生の16進数。0x00は成功コード |
| `ok` / `fail`（RANGE_LINK） | その相手の成功／失敗累計。0x00でも距離65535なら失敗扱い |
| `r_ok` / `r_bad` | 19番が処理した2距離の成功／失敗累計 |
| `max_gap_ms`（RANGE_LINK） | 成功した測距通知間の最大間隔。最初の成功は対象外。65535で飽和 |
| `session` / `reason` | セッション状態／理由コード。2=Active、3=Idle |
| `start_count` | 初回を含む開始コマンドの試行累計。再試行・再開で増え、65535で飽和 |
| `bad` / `missing`（RANGE_STAT） | UARTの破損／連番欠落。無線測距の失敗とは別 |
| `frame_error` / `overflow` / `parity` / `breaks` | CoreS3のUARTドライバが報告したエラー |
| `irq_us` | ソフトUARTが観測した最大の割り込み停止時間、概算µs |
| `tx_frame_ms` | 直前のUARTフレーム送信時間 |
| `tx_overruns` | 200ms送信周期を守れなかった累計、65535で飽和 |
| `max_gap_ms`（RANGE_STAT） | CoreS3が正常UART受信を処理した時刻の最大間隔 |

失敗通知が来ても保存済み距離の時刻やnLosを更新しません。最新の失敗はRANGE_LINKで確認します。
鮮度判定は送信時点の経過時間＋受信後の経過時間＋500msの余裕が3秒以内、かつUART受信間隔1秒以内です。
RANGE_DATAのvalidは受信した時点の判定です。その後通信が止まった場合はRANGE_STATの状態を確認します。
CLEARはCoreS3の統計だけをリセットし、Type2DKの測距累計はリセットしません。

### ログで取りこぼしを判断するとき

UARTは200msごとの**最新状態のスナップショット**です。UWBの200ms周期とは位相が異なるため、
同じ測距値が2回載る場合や、間の通知が個別には載らない場合があります。
各UWB通知をすべて保存するイベント列ではありません。解析ではupdatesやuci_seqも確認してください。
相手ごとの成功・失敗累計と最大成功間隔はType2DKのコールバックで更新するので、
スナップショット間に起きた失敗もfailの増分として残ります。ただし間の失敗コードやnLos生値すべては残りません。

## 試験手順

1. 4台を更新して電源を入れ直し、CoreS3の画面とRANGE_STATで `1.4.0-range` を確認します。
2. まず20秒ほど保存し、RANGE_DATA・RANGE_LINK（peer=21/22）・RANGE_STATを確認します。
3. 受信約5回/秒、UARTのエラー・欠落が増えないこと、相手ごとのokが増えることを確認します。
4. 基板を固定し、見通しがある状態と、人が間に入った状態を比較します。切り替え時刻を記録します。
5. nlos_rawが常に同じでも「遮蔽なし」とは判断せず、距離・status・fail・間隔と合わせて評価します。

画面には距離、最新通知のnLos生値・状態コード、成功／失敗回数を表示します。
未受信・古い状態のnLos欄は `--` になります。21–22の距離はCoreS3へ転送しません。

## 検証と再ビルド

v2は3台＋19番UART停止比較版のARMビルド、イメージヘッダ/CRC/SHA256検証、
加速度関数がリンクされていないことの確認を行います。
ホスト試験は通信形式、旧版拒否、破損からの復帰、連番・再起動・鮮度、
成功距離とnLosの対応、失敗通知、未知のnLos値、再接続を対象とします。
CoreS3はCIで実際にビルドします。実機でのnLos判定性能は未確認です。

SDK本体は含めません。ユーザー提供の `UWBIOT_SR040_v04.03.14_MCUx/uwbiot-top` と
xPack Arm GNU 13.2.1-1.1を使用します。

```sh
patch --batch --forward -d "$SDK_PATH" -p0 < type2dk/ranging/build/2dk_prebuilt_v04.03.14.patch
python type2dk/ranging/build/apply_sdk_changes.py "$SDK_PATH"
python type2dk/ranging/build/build.py --sdk "$SDK_PATH" --gcc-bin "$ARM_GCC_BIN" --node 19 --out build/range19
```

21・22は `--node` を変更します。19番の比較版は `--uart-off` を追加します。
Ch5、DS-TWR、SP1、SFD2、preamble10、slot2400、25 slots、interval200msを引き継ぎます。
UARTは優先度1、4バイトごとに1tick（5ms）待機し、96バイトを200msごとに送ります。
1バイトの開始＋8データビット中のみIRQを停止し、ストップビット中に再開します。
96バイトの送信所要時間と測距への影響は実機でも確認してください。

旧v1の実機確認記録とBINは固定して保存しています。[旧版の説明](https://github.com/temesotejam/type2dk-uwb-uart/blob/main/firmware/known-good/README-v1.md)。
