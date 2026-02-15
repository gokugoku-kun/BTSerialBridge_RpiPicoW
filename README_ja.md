[English version](README.md)

# BTSerialBridge_RpiPicoW

Raspberry Pi Pico W を使用した Bluetooth SPP ⇔ UART 透過ブリッジファームウェアです。  
PC などの Bluetooth 対応デバイスから、ターゲットボードの UART と無線で通信できます。

## 主な機能

- Bluetooth SPP (Serial Port Profile) による無線シリアル通信
- UART ⇔ Bluetooth 間の双方向データ転送
- DIP スイッチによる UART 設定プリセット切り替え
- USB CDC 経由のデバッグコンソール（統計表示・設定変更）
- フラッシュへの UART 設定保存・復元
- LED によるステータス表示（ペアリング待機 / 接続中 / データ転送中 / エラー）

## システム構成図

```
  PC                                   Pico W
 +--------------------+              +--------------------------+
 | TeraTerm / PuTTY   |              |     BTSerialBridge       |
 | etc.               |              |                          |
 +--------+-----------+              |  +------+  +---------+  |   +-----------+
          |                          |  |  BT  |  |  UART   |  |   |  Target   |
          | Serial I/O               |  |  SPP +->| Bridge  +----->|  Board    |
          v                          |  |      |  |         |  |   |           |
 +--------+-----------+    wireless  |  |      |<-+         |<----+           |
 | Virtual COM Port   +------------>|  +------+  +---------+  |   +-----------+
 | (auto-generated)   |<-----------+|                          |
 +--------------------+              +--------------------------+
  BT pairing creates
  a virtual COM port
```

PC から Bluetooth ペアリングすると仮想 COM ポートが自動生成されます。  
既存のシリアル通信アプリ（TeraTerm, PuTTY, 自作ツール等）からそのまま使えます。

### 動作確認済み OS

- Windows 11
- Android

※ macOS / iOS は Bluetooth SPP に対応していないため使用できません。

## ハードウェア

- Raspberry Pi Pico W
- UART: GPIO16 (TX) / GPIO17 (RX)
- DIP スイッチ: GPIO21 (SW1) / GPIO20 (SW2)（外部プルアップ）
- デバッグ GPIO: GPIO2, 3, 4

※ UART のハードウェアフロー制御 (RTS/CTS) には対応していません。

### DIP スイッチ設定

| SW2 | SW1 | UART 設定 |
|-----|-----|-----------|
| OFF | OFF | 115200-8-N-1 |
| OFF | ON  | 9600-8-N-1 |
| ON  | OFF | 19200-8-O-1 |
| ON  | ON  | フラッシュ保存値を使用 |

## ビルド

### 前提条件

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) v2.2.0 以降
- CMake 3.13 以降
- ARM GCC ツールチェーン

## デバッグコンソール

USB CDC 経由で以下のコマンドが使用できます:

| キー | 機能 |
|------|------|
| s | 統計情報を表示 |
| r | 統計をリセット |
| c | 現在の UART 設定を表示 |
| u | UART 設定を対話式で変更 |
| h | ヘルプを表示 |

## 性能参考データ

以下は 115200 bps / 8-N-1 / 5V 駆動、UART TX→RX ループバック接続という特定条件での測定結果です。  
PC の Bluetooth アダプタや通信環境、電源品質等により結果は変動するため、あくまで参考値としてご覧ください。

### ラウンドトリップ (RTT)

PC → BT SPP → Pico W → UART → ループバック → UART → Pico W → BT SPP → PC の往復時間です。

| データ長 | Min (ms) | Avg (ms) | Max (ms) |
|----------|----------|----------|----------|
| 1 byte   | 14.0     | 29.5     | 53.7     |
| 10 bytes | 27.8     | 30.3     | 54.3     |
| 20 bytes | 32.2     | 36.4     | 77.6     |

詳細: [test/115200bps_5V_SW00_RTT.txt](test/115200bps_5V_SW00_RTT.txt) / スクリプト: [test/test_roundtrip.py](test/test_roundtrip.py)

### スループット (過負荷試験)

UART 理論速度 (11,520 B/s) に対する送信レートを変化させ、ロスなしの限界を測定しました。

| 送信レート | 目標 B/s | 送信 | 受信 | ロス | ロス率 | RTT avg (ms) | 判定 |
|-----------|----------|------|------|------|--------|-------------|------|
| 50%       | 5,760    | 500  | 500  | 0    | 0.00%  | 25.8        | OK   |
| 80%       | 9,216    | 500  | 500  | 0    | 0.00%  | 26.1        | OK   |
| 95%       | 10,944   | 500  | 500  | 0    | 0.00%  | 37.7        | OK   |
| 99%       | 11,405   | 500  | 500  | 0    | 0.00%  | 67.1        | OK   |
| 100%      | 11,520   | 500  | 500  | 1    | 0.20%  | 80.8        | NG   |
| 105%      | 12,096   | 500  | 481  | 19   | 3.80%  | 106.7       | NG   |
| 120%      | 13,824   | 500  | 422  | 78   | 15.60% | 113.4       | NG   |
| 150%      | 17,280   | 500  | 339  | 161  | 32.20% | 118.8       | NG   |

詳細: [test/115200bps_5V_STRESS.txt](test/115200bps_5V_STRESS.txt) / スクリプト: [test/test_stress.py](test/test_stress.py)

### 消費電流

ターゲットボード側から VSYS へ給電、USB 給電なしの条件で測定しています。値はピーク値です。

| 電源電圧 | 消費電流 |
|----------|----------|
| 3.3V     | 約 100mA |
| 5.0V     | 約 62mA  |

## License

This project is licensed under the BSD 3-Clause License. See [LICENSE](LICENSE) for details.

## Dependencies & Licenses

This project depends on the following libraries (not included in this repository):

| Library | License | URL |
|---------|---------|-----|
| Raspberry Pi Pico SDK | BSD 3-Clause | https://github.com/raspberrypi/pico-sdk |
| BTstack (via Pico SDK) | BSD 3-Clause (when used with Pico SDK) | https://github.com/bluekitchen/btstack |
| CYW43 Driver | Cypress Permissive License | Included in Pico SDK |

Note: `pico_sdk_import.cmake` is a copy from the Pico SDK (BSD 3-Clause).
