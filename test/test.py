import serial
import time
import threading

# ===== シリアル設定 =====
PORT = "COM5"
BAUDRATE = 115200
SEND_INTERVAL = 0.001  # 送信間隔 1ms
# ========================

# 停止フラグ
stop_flag = threading.Event()

ser = serial.Serial(
    port=PORT,
    baudrate=BAUDRATE,
    bytesize=serial.EIGHTBITS,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    timeout=0.1,  # 受信タイムアウトを短く
    xonxoff=False,
    rtscts=False,
    dsrdtr=False
)

def send_data():
    """送信スレッド"""
    try:
        for i in range(1, 10000):
            if stop_flag.is_set():
                break
            data = f"UART:{i:015d}\n"
            ser.write(data.encode("ascii"))
            #print("送信:", data.strip())
            time.sleep(SEND_INTERVAL)
    except Exception as e:
        print(f"送信エラー: {e}")

def receive_data():
    """受信スレッド"""
    try:
        while not stop_flag.is_set():
            if ser.in_waiting > 0:
                received_data = ser.readline().decode("ascii", errors="ignore")
                if received_data.strip():
                    print("受信:", received_data.strip())
            time.sleep(0.001)  # 1ms間隔でチェック
    except Exception as e:
        print(f"受信エラー: {e}")

print("送受信開始...")
time.sleep(2)  # 接続安定待ち

try:
    # 送信と受信スレッドを開始
    send_thread = threading.Thread(target=send_data)
    receive_thread = threading.Thread(target=receive_data)
    
    send_thread.daemon = True  # デーモンスレッドに設定
    receive_thread.daemon = True  # デーモンスレッドに設定
    
    send_thread.start()
    receive_thread.start()
    
    # メインスレッドでCtrl+Cを待機
    while not stop_flag.is_set():
        time.sleep(0.1)

except KeyboardInterrupt:
    print("\nCtrl+Cが押されました。停止中...")
    stop_flag.set()

finally:
    ser.close()
    print("送受信終了、COMポートを閉じました")