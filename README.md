[日本語版はこちら](README_ja.md)

# BTSerialBridge_RpiPicoW

A transparent Bluetooth SPP to UART bridge firmware for Raspberry Pi Pico W.  
Enables wireless serial communication between a PC and a target board via Bluetooth.

## Features

- Wireless serial communication via Bluetooth SPP (Serial Port Profile)
- Bidirectional data transfer between UART and Bluetooth
- DIP switch selectable UART presets
- Debug console via USB CDC (statistics, configuration)
- UART settings saved to / restored from flash
- LED status indication (pairing / connected / data transfer / error)

## System Diagram

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

Bluetooth pairing automatically creates a virtual COM port on the PC.  
Any existing serial terminal (TeraTerm, PuTTY, custom tools, etc.) works as-is.

### Supported OS

- Windows 11
- Android

Note: macOS / iOS are not supported as they do not support Bluetooth SPP.

## Hardware

- Raspberry Pi Pico W
- UART: GPIO16 (TX) / GPIO17 (RX)
- DIP switches: GPIO21 (SW1) / GPIO20 (SW2) (external pull-up)
- Debug GPIO: GPIO2, 3, 4

Note: Hardware flow control (RTS/CTS) is not supported.

### DIP Switch Settings

| SW2 | SW1 | UART Setting |
|-----|-----|--------------|
| OFF | OFF | 115200-8-N-1 |
| OFF | ON  | 9600-8-N-1 |
| ON  | OFF | 19200-8-O-1 |
| ON  | ON  | Use flash-saved settings |

## Build

### Prerequisites

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) v2.2.0 or later
- CMake 3.13 or later
- ARM GCC toolchain

## Debug Console

The following commands are available via USB CDC:

| Key | Function |
|-----|----------|
| s | Show statistics |
| r | Reset statistics |
| c | Show current UART configuration |
| u | Change UART configuration (interactive) |
| h | Show help |

## Performance Reference

The following results were measured under specific conditions: 115200 bps / 8-N-1 / 5V, UART TX-RX loopback.  
Actual performance may vary depending on the Bluetooth adapter, RF environment, power supply quality, etc. These values are provided as a reference only.

### Round-Trip Time (RTT)

PC → BT SPP → Pico W → UART → loopback → UART → Pico W → BT SPP → PC

| Data Length | Min (ms) | Avg (ms) | Max (ms) |
|-------------|----------|----------|----------|
| 1 byte      | 14.0     | 29.5     | 53.7     |
| 10 bytes    | 27.8     | 30.3     | 54.3     |
| 20 bytes    | 32.2     | 36.4     | 77.6     |

Details: [test/115200bps_5V_SW00_RTT.txt](test/115200bps_5V_SW00_RTT.txt) / Script: [test/test_roundtrip.py](test/test_roundtrip.py)

### Throughput (Stress Test)

Packet loss threshold measured by sweeping the send rate relative to the UART theoretical speed (11,520 B/s).

| Send Rate | Target B/s | Sent | Recv | Lost | Loss Rate | RTT avg (ms) | Result |
|-----------|-----------|------|------|------|-----------|-------------|--------|
| 50%       | 5,760     | 500  | 500  | 0    | 0.00%     | 25.8        | OK     |
| 80%       | 9,216     | 500  | 500  | 0    | 0.00%     | 26.1        | OK     |
| 95%       | 10,944    | 500  | 500  | 0    | 0.00%     | 37.7        | OK     |
| 99%       | 11,405    | 500  | 500  | 0    | 0.00%     | 67.1        | OK     |
| 100%      | 11,520    | 500  | 500  | 1    | 0.20%     | 80.8        | NG     |
| 105%      | 12,096    | 500  | 481  | 19   | 3.80%     | 106.7       | NG     |
| 120%      | 13,824    | 500  | 422  | 78   | 15.60%    | 113.4       | NG     |
| 150%      | 17,280    | 500  | 339  | 161  | 32.20%    | 118.8       | NG     |

Details: [test/115200bps_5V_STRESS.txt](test/115200bps_5V_STRESS.txt) / Script: [test/test_stress.py](test/test_stress.py)

### Power Consumption

Measured with power supplied from the target board to VSYS, no USB power. Values are peak.

| Voltage | Current |
|---------|---------|
| 3.3V    | ~100mA  |
| 5.0V    | ~62mA   |

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
