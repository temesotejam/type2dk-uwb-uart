# Type2DKファームウェア

- [測距＋UART版](ranging/README.md)：19番の2距離と加速度を毎秒5回送信します。
- [単独UART診断版](UART_README.md)：測距を行わず試験データを毎秒50回送信します。

いずれもPIO13 → CoreS3 GPIO2、38400 bps / 8N1です。
USBログは測距版3000000 bps、診断版115200 bpsで、速度が異なります。
