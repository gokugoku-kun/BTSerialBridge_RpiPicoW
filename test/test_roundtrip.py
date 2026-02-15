"""
BT-SPP ラウンドトリップ性能計測スクリプト (データ長スイープ版)

計測フロー:
  ① PC → BT送信 (シリアルポート経由)
  ② デバイス側 UART ループバック (TX→RX 直結)
  ③ BT受信 → PC

送信データ長を 1〜20 文字でスイープし、各長さで 100 回計測。
min / max / ave を算出する。

前提:
  - デバイス側の UART TX と RX がループバック接続されていること
  - Bluetooth SPP で PC と接続済みであること (COMポートが見えている状態)
"""

import serial
import time
import sys
import statistics

# ===== 設定 =====
PORT = "COM4"
BAUDRATE = 115200
TRIAL_COUNT = 300
TIMEOUT_SEC = 5.0        # 1回あたりの受信タイムアウト
SEND_INTERVAL_SEC = 0.05 # 試行間のインターバル
MIN_DATA_LEN = 1         # 最小データ長 (文字数、改行除く)
MAX_DATA_LEN = 20        # 最大データ長 (文字数、改行除く)
# =================


def flush_rx(ser: serial.Serial) -> None:
    """受信バッファを空にする"""
    ser.reset_input_buffer()
    while ser.in_waiting > 0:
        ser.read(ser.in_waiting)
        time.sleep(0.01)


def make_payload(length: int, trial: int) -> str:
    """
    指定文字数のペイロードを生成する (改行は含まない)。
    先頭から 'A'〜'Z' の繰り返しで埋め、末尾に改行を付与して返す。
    """
    fill = "".join(chr(ord("A") + (j % 26)) for j in range(length))
    return fill + "\n"


def run_roundtrip_test(ser: serial.Serial, payload: str) -> float | None:
    """
    1回分のラウンドトリップ計測。
    戻り値: 往復時間 (秒)。タイムアウト/不一致時は None。
    """
    payload_bytes = payload.encode("ascii")
    expected = payload.strip()

    flush_rx(ser)

    t_start = time.perf_counter()
    ser.write(payload_bytes)
    ser.flush()

    ser.timeout = TIMEOUT_SEC
    received = ser.readline()
    t_end = time.perf_counter()

    if not received:
        return None

    received_str = received.decode("ascii", errors="ignore").strip()
    if received_str != expected:
        return None

    return t_end - t_start


def main() -> None:
    port = PORT
    baudrate = BAUDRATE
    count = TRIAL_COUNT

    if len(sys.argv) >= 2:
        port = sys.argv[1]
    if len(sys.argv) >= 3:
        baudrate = int(sys.argv[2])
    if len(sys.argv) >= 4:
        count = int(sys.argv[3])

    print(f"=== BT-SPP ラウンドトリップ性能計測 (データ長スイープ) ===")
    print(f"ポート     : {port}")
    print(f"ボーレート : {baudrate} bps")
    print(f"試行回数   : 各 {count} 回")
    print(f"データ長   : {MIN_DATA_LEN}〜{MAX_DATA_LEN} 文字 (+改行)")
    print()

    ser = serial.Serial(
        port=port,
        baudrate=baudrate,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=TIMEOUT_SEC,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    )

    print("接続安定待ち (2秒)...")
    time.sleep(2)
    flush_rx(ser)

    # 全データ長の結果を保持
    summary: list[dict] = []

    for data_len in range(MIN_DATA_LEN, MAX_DATA_LEN + 1):
        payload = make_payload(data_len, 0)
        total_bytes = len(payload.encode("ascii"))  # 改行込みバイト数

        print(f"\n--- データ長 {data_len} 文字 ({total_bytes} bytes 改行込み) ---")

        results_ms: list[float] = []
        fail_count = 0

        for i in range(1, count + 1):
            elapsed = run_roundtrip_test(ser, payload)

            if elapsed is None:
                fail_count += 1
                print(f"  [{i:3d}/{count}] FAIL")
            else:
                elapsed_ms = elapsed * 1000.0
                results_ms.append(elapsed_ms)
                print(f"  [{i:3d}/{count}] {elapsed_ms:8.2f} ms")

            time.sleep(SEND_INTERVAL_SEC)

        # この長さの集計
        entry: dict = {"length": data_len, "bytes": total_bytes, "ok": len(results_ms), "fail": fail_count}
        if results_ms:
            entry["min"] = min(results_ms)
            entry["max"] = max(results_ms)
            entry["avg"] = statistics.mean(results_ms)
            entry["med"] = statistics.median(results_ms)
            entry["std"] = statistics.stdev(results_ms) if len(results_ms) >= 2 else 0.0
        summary.append(entry)

    ser.close()

    # ===== 総合サマリー =====
    print("\n")
    print("=" * 78)
    print("  総合サマリー")
    print("=" * 78)
    header = f"{'Len':>4s} {'Bytes':>5s} {'OK':>4s} {'Fail':>4s} {'Min(ms)':>9s} {'Max(ms)':>9s} {'Avg(ms)':>9s} {'Med(ms)':>9s} {'Std(ms)':>9s}"
    print(header)
    print("-" * 78)

    for e in summary:
        if "min" in e:
            print(
                f"{e['length']:4d} {e['bytes']:5d} {e['ok']:4d} {e['fail']:4d} "
                f"{e['min']:9.2f} {e['max']:9.2f} {e['avg']:9.2f} {e['med']:9.2f} {e['std']:9.2f}"
            )
        else:
            print(
                f"{e['length']:4d} {e['bytes']:5d} {e['ok']:4d} {e['fail']:4d} "
                f"{'---':>9s} {'---':>9s} {'---':>9s} {'---':>9s} {'---':>9s}"
            )

    print("=" * 78)


if __name__ == "__main__":
    main()
