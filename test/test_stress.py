"""
BT-SPP 過負荷試験スクリプト (UART理論速度比スイープ版)

テストフロー:
  ① PC → BT送信 (シリアルポート経由でパケットを連続送信)
  ② デバイス側 UART ループバック (TX→RX 直結)
  ③ BT受信 → PC (受信データを検証)

考え方:
  BT SPP の実効帯域 > UART ボーレート の場合、PC側が速く送りすぎると
  デバイス側の BT受信→UART送信 でバッファオーバーフローが発生しロスする。
  UART理論速度 (baudrate / 10 bytes/sec @8N1) に対して
  送信レートを 10%〜150% までスイープし、ロスなしの限界を探る。

前提:
  - デバイス側の UART TX と RX がループバック接続されていること
  - Bluetooth SPP で PC と接続済みであること (COMポートが見えている状態)

使い方:
  python test_stress.py [COMポート] [ボーレート]
  例: python test_stress.py COM5 115200
"""

import serial
import time
import sys
import threading
import statistics
from dataclasses import dataclass, field

# ===== 設定 =====
DEFAULT_PORT = "COM5"
DEFAULT_BAUDRATE = 115200

# UART フレーム構成 (8-N-1: start=1 + data=8 + stop=1 = 10 bits/byte)
BITS_PER_BYTE = 10

# パケットサイズ (改行込み)
PACKET_SIZE = 64

# 各レートで送信するパケット数
PACKETS_PER_RATE = 500

# UART理論速度に対する送信レート (%)
# 低い方から段階的に上げて限界を探る
RATE_PERCENTAGES = [50,60,70,80] + list(range(90, 111)) + [120,150,200]

RECV_TIMEOUT_SEC = 15.0   # 全受信完了までの最大待ち時間
STABILIZE_SEC = 2.0       # 接続安定待ち
INTER_SCENARIO_SEC = 3.0  # シナリオ間のクールダウン
# =================


@dataclass
class ScenarioResult:
    """1シナリオの結果"""
    rate_pct: int = 0
    target_bps: float = 0.0
    packet_size: int = 0
    total_sent: int = 0
    total_received: int = 0
    lost: int = 0
    duplicated: int = 0
    out_of_order: int = 0
    corrupted: int = 0
    loss_rate_pct: float = 0.0
    send_duration_sec: float = 0.0
    recv_duration_sec: float = 0.0
    actual_send_bps: float = 0.0
    actual_recv_bps: float = 0.0
    rtt_ms: list = field(default_factory=list)
    passed: bool = False


def calc_uart_theoretical_bps(baudrate: int) -> float:
    """UART理論スループット (bytes/sec) を算出する"""
    return baudrate / BITS_PER_BYTE


def calc_send_interval(baudrate: int, rate_pct: int, packet_size: int) -> float:
    """
    指定レート(%)でパケットを送るための送信間隔(sec)を算出する。
    rate_pct=100 なら UART理論速度ちょうど。
    """
    uart_bps = calc_uart_theoretical_bps(baudrate)  # bytes/sec
    target_bps = uart_bps * (rate_pct / 100.0)      # 目標 bytes/sec
    if target_bps <= 0:
        return 1.0
    interval = packet_size / target_bps
    return interval


def make_packet(seq: int, payload_size: int) -> bytes:
    """
    シーケンス番号付きパケットを生成する。
    フォーマット: SEQ:NNNNNN:PAYLOAD\n
    """
    header = f"SEQ:{seq:06d}:"
    header_bytes = header.encode("ascii")
    fill_len = max(0, payload_size - len(header_bytes) - 1)
    fill = bytes([0x41 + (i % 26) for i in range(fill_len)])
    return header_bytes + fill + b"\n"


def parse_packet(line: str) -> int | None:
    """受信行からシーケンス番号を抽出する"""
    try:
        if line.startswith("SEQ:"):
            parts = line.split(":", 2)
            if len(parts) >= 2:
                return int(parts[1])
    except (ValueError, IndexError):
        pass
    return None


def flush_rx(ser: serial.Serial) -> None:
    """受信バッファを空にする"""
    ser.reset_input_buffer()
    deadline = time.time() + 0.5
    while time.time() < deadline:
        if ser.in_waiting > 0:
            ser.read(ser.in_waiting)
        time.sleep(0.01)


def run_scenario(ser: serial.Serial, baudrate: int, rate_pct: int,
                 packet_size: int, count: int) -> ScenarioResult:
    """1つのレートシナリオを実行する"""
    uart_bps = calc_uart_theoretical_bps(baudrate)
    target_bps = uart_bps * (rate_pct / 100.0)
    interval = calc_send_interval(baudrate, rate_pct, packet_size)

    result = ScenarioResult(
        rate_pct=rate_pct,
        target_bps=target_bps,
        packet_size=packet_size,
        total_sent=count,
    )

    received_seqs: list[int] = []
    recv_buf = b""
    recv_lock = threading.Lock()
    recv_done = threading.Event()
    send_times: dict[int, float] = {}

    def receiver():
        nonlocal recv_buf
        while not recv_done.is_set():
            try:
                if ser.in_waiting > 0:
                    chunk = ser.read(ser.in_waiting)
                    with recv_lock:
                        recv_buf += chunk
                        while b"\n" in recv_buf:
                            line_bytes, recv_buf = recv_buf.split(b"\n", 1)
                            line = line_bytes.decode("ascii", errors="ignore").strip()
                            if line:
                                seq = parse_packet(line)
                                t_recv = time.perf_counter()
                                if seq is not None:
                                    received_seqs.append(seq)
                                    if seq in send_times:
                                        rtt = (t_recv - send_times[seq]) * 1000.0
                                        result.rtt_ms.append(rtt)
                                else:
                                    result.corrupted += 1
                else:
                    time.sleep(0.0005)
            except Exception:
                break

    flush_rx(ser)

    recv_thread = threading.Thread(target=receiver, daemon=True)
    recv_thread.start()

    # === 送信フェーズ ===
    t_send_start = time.perf_counter()
    for seq in range(1, count + 1):
        pkt = make_packet(seq, packet_size)
        send_times[seq] = time.perf_counter()
        ser.write(pkt)
        if interval > 0.0001:
            # 高精度スリープ: 短い間隔はビジーウェイトで精度を上げる
            target_time = t_send_start + seq * interval
            while time.perf_counter() < target_time:
                pass
    ser.flush()
    t_send_end = time.perf_counter()

    result.send_duration_sec = t_send_end - t_send_start

    # === 受信待ちフェーズ ===
    deadline = time.time() + RECV_TIMEOUT_SEC
    while time.time() < deadline:
        if len(received_seqs) >= count:
            break
        time.sleep(0.05)

    time.sleep(0.5)
    recv_done.set()
    recv_thread.join(timeout=2.0)

    t_recv_end = time.perf_counter()
    result.recv_duration_sec = t_recv_end - t_send_start

    # === 結果分析 ===
    result.total_received = len(received_seqs)

    sent_set = set(range(1, count + 1))
    recv_set = set(received_seqs)
    result.lost = len(sent_set - recv_set)

    seen = set()
    for s in received_seqs:
        if s in seen:
            result.duplicated += 1
        seen.add(s)

    for i in range(1, len(received_seqs)):
        if received_seqs[i] < received_seqs[i - 1]:
            result.out_of_order += 1

    result.loss_rate_pct = (result.lost / count * 100.0) if count > 0 else 0.0
    result.passed = (result.lost == 0 and result.corrupted == 0)

    total_bytes = packet_size * count
    if result.send_duration_sec > 0:
        result.actual_send_bps = total_bytes / result.send_duration_sec
    if result.recv_duration_sec > 0:
        result.actual_recv_bps = (packet_size * result.total_received) / result.recv_duration_sec

    return result


def print_scenario_result(r: ScenarioResult) -> None:
    """シナリオ結果を表示"""
    verdict = "OK" if r.passed else "NG"
    print(f"    送信: {r.total_sent}  受信: {r.total_received}  "
          f"ロス: {r.lost} ({r.loss_rate_pct:.2f}%)  [{verdict}]")
    print(f"    重複: {r.duplicated}  順序異常: {r.out_of_order}  破損: {r.corrupted}")
    print(f"    実送信速度: {r.actual_send_bps:.0f} B/s  "
          f"(目標: {r.target_bps:.0f} B/s)")
    print(f"    送信時間: {r.send_duration_sec:.3f}s  "
          f"受信完了: {r.recv_duration_sec:.3f}s")
    if r.rtt_ms:
        print(f"    RTT  min: {min(r.rtt_ms):.1f}ms  "
              f"max: {max(r.rtt_ms):.1f}ms  "
              f"avg: {statistics.mean(r.rtt_ms):.1f}ms  "
              f"med: {statistics.median(r.rtt_ms):.1f}ms")


def print_summary(results: list[ScenarioResult], baudrate: int) -> None:
    """全シナリオの総合サマリーを表示"""
    uart_bps = calc_uart_theoretical_bps(baudrate)

    print("\n")
    print("=" * 105)
    print(f"  過負荷試験 総合サマリー  (UART理論速度: {uart_bps:.0f} B/s @ {baudrate} baud 8-N-1)")
    print("=" * 105)

    header = (f"{'Rate%':>6s} {'目標B/s':>9s} {'実送信B/s':>10s} {'送信':>5s} {'受信':>5s} "
              f"{'ロス':>5s} {'ロス率':>7s} {'重複':>4s} {'順序':>4s} {'破損':>4s} "
              f"{'RTT avg':>8s} {'RTT max':>8s} {'判定':>4s}")
    print(header)
    print("-" * 105)

    max_pass_rate = 0

    for r in results:
        verdict = " OK" if r.passed else " NG"
        rtt_avg = f"{statistics.mean(r.rtt_ms):.1f}" if r.rtt_ms else "---"
        rtt_max = f"{max(r.rtt_ms):.1f}" if r.rtt_ms else "---"

        if r.passed:
            max_pass_rate = r.rate_pct

        print(f"{r.rate_pct:5d}% {r.target_bps:9.0f} {r.actual_send_bps:10.0f} "
              f"{r.total_sent:5d} {r.total_received:5d} "
              f"{r.lost:5d} {r.loss_rate_pct:6.2f}% {r.duplicated:4d} {r.out_of_order:4d} {r.corrupted:4d} "
              f"{rtt_avg:>8s} {rtt_max:>8s} {verdict}")

    print("=" * 105)

    # ロスなし限界の判定
    if max_pass_rate > 0:
        print(f"\n  >>> ロスなし最大レート: UART理論速度の {max_pass_rate}% "
              f"({uart_bps * max_pass_rate / 100.0:.0f} B/s) <<<")
    else:
        print(f"\n  >>> 全レートでロス発生 <<<")

    # 境界付近の詳細
    for i in range(len(results) - 1):
        if results[i].passed and not results[i + 1].passed:
            print(f"  >>> 限界境界: {results[i].rate_pct}% (OK) → "
                  f"{results[i + 1].rate_pct}% (NG, ロス率 {results[i + 1].loss_rate_pct:.2f}%) <<<")
            break


def main() -> None:
    port = DEFAULT_PORT
    baudrate = DEFAULT_BAUDRATE

    if len(sys.argv) >= 2:
        port = sys.argv[1]
    if len(sys.argv) >= 3:
        baudrate = int(sys.argv[2])

    uart_bps = calc_uart_theoretical_bps(baudrate)

    print("=" * 60)
    print("  BT-SPP 過負荷試験 (UART理論速度比スイープ)")
    print("=" * 60)
    print(f"  ポート         : {port}")
    print(f"  ボーレート     : {baudrate} bps")
    print(f"  UART理論速度   : {uart_bps:.0f} bytes/sec (8-N-1)")
    print(f"  パケットサイズ : {PACKET_SIZE} bytes")
    print(f"  パケット数/レート: {PACKETS_PER_RATE}")
    print(f"  テストレート   : {RATE_PERCENTAGES} %")
    print()

    ser = serial.Serial(
        port=port,
        baudrate=baudrate,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    )

    print(f"接続安定待ち ({STABILIZE_SEC}秒)...")
    time.sleep(STABILIZE_SEC)
    flush_rx(ser)

    results: list[ScenarioResult] = []

    for i, rate_pct in enumerate(RATE_PERCENTAGES, 1):
        target_bps = uart_bps * (rate_pct / 100.0)
        interval = calc_send_interval(baudrate, rate_pct, PACKET_SIZE)

        print(f"\n{'='*60}")
        print(f"  [{i}/{len(RATE_PERCENTAGES)}] UART理論速度の {rate_pct}%")
        print(f"  目標: {target_bps:.0f} B/s  送信間隔: {interval*1000:.2f} ms")
        print(f"{'='*60}")

        r = run_scenario(ser, baudrate, rate_pct, PACKET_SIZE, PACKETS_PER_RATE)
        results.append(r)
        print_scenario_result(r)

        if i < len(RATE_PERCENTAGES):
            print(f"\n  クールダウン ({INTER_SCENARIO_SEC}秒)...")
            flush_rx(ser)
            time.sleep(INTER_SCENARIO_SEC)

    ser.close()
    print_summary(results, baudrate)


if __name__ == "__main__":
    main()
