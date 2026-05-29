# HaphazardBridge

**ESP32-S3-ETH firmware that bridges an SDR to an ATAK device — without the SDR needing WiFi.**

Operates in two modes switchable at boot. Works with or without an upstream WiFi AP.

---

## Modes

### Mobility Mode
ESP32 connects to a vehicle or infrastructure AP **and** simultaneously broadcasts its own **HaphazardNet** AP. ATAK can connect via either network.

### Dismounted Mode
No upstream AP needed. ESP32 creates **HaphazardNet** standalone. ATAK connects directly to the bridge. Fully self-contained.

---

## Architecture

### Mobility Mode
```
HaphazardTAK AP (vehicle/infrastructure)
    ├── HaphazardBridge STA (192.168.10.178) ←── SDR (192.168.99.234)
    │        └── HaphazardNet AP (192.168.4.1)
    └── ATAK (192.168.10.123)
```

### Dismounted Mode
```
HaphazardNet AP (192.168.4.1)
    ├── ATAK (192.168.4.x)
    └── HaphazardBridge ETH (192.168.99.1) ←── SDR (192.168.99.234)
```

| Service | Mobility | Dismounted |
|---|---|---|
| **gRPC** | ATAK → `192.168.10.178:8000` → SDR | ATAK → `192.168.4.1:8000` → SDR |
| **CoT UDP** | SDR → ATAK via NAT | SDR → `192.168.99.1:4242` → ESP32 → AP broadcast |
| **SDR web UI** | `http://192.168.10.178:8888` | `http://192.168.99.234` direct (any port) |
| **SAPIENT** | SDR → ATAK via NAT | SDR → ATAK via NAT |

> **Dismounted Mode — bidirectional NAT:** ATAK can reach the SDR directly at `192.168.99.234` on any port. No port forwarding needed — the ESP32 NATs AP↔ETH transparently in both directions.

---

## Hardware

[Waveshare ESP32-S3-ETH](https://www.waveshare.com/esp32-s3-eth.htm) — ESP32-S3 with onboard W5500 SPI Ethernet.

---

## BOOT Button

| Action | When | Result |
|---|---|---|
| **Tap** (< 1.5s) | Within 3s of boot | Cycle Mobility ↔ Dismounted |
| **Hold** (1.5s+) | Within 3s of boot | Factory reset |
| **Hold** (3s+) | Normal operation | Open config portal |

**To switch modes:** press RESET, wait ~1 second, tap BOOT. Watch your WiFi list — `HaphazardNet` = Dismounted, `HaphazardBridge-Setup` = Mobility searching for upstream AP.

---

## Quick Start

### 1. Flash
```bash
git clone https://github.com/HaphazardLabs/HaphazardBridge.git
cd HaphazardBridge
~/.platformio-venv/bin/pio run -e haphazardbridge -t upload
```
Requires [PlatformIO](https://platformio.org/).

### 2. Configure the SDR

**Mobility Mode:**
| Setting | Value |
|---|---|
| IP | `192.168.99.234` (static) |
| Gateway | `192.168.99.1` |
| CoT UDP target | `192.168.10.123:4242` |
| SAPIENT target | `192.168.10.123` |
| Static location | Lat `38.2362` Lon `-78.3603` *(if no GPS)* |

**Dismounted Mode:**
| Setting | Value |
|---|---|
| IP | `192.168.99.234` (static) |
| Gateway | `192.168.99.1` |
| CoT UDP target | `192.168.99.1:4242` *(relayed to ATAK)* |
| SAPIENT target | ATAK's IP on HaphazardNet |
| Static location | Lat `38.2362` Lon `-78.3603` *(if no GPS)* |

### 3. Configure ATAK

**Mobility Mode:**
| Setting | Value |
|---|---|
| SDR plugin socket | `192.168.10.178:8000` |
| CoT UDP input | port `4242` |

**Dismounted Mode:**
| Setting | Value |
|---|---|
| SDR plugin socket | `192.168.4.1:8000` |
| CoT UDP input | port `4242` |

> **Version compatibility:** ATAK SDR plugin and SDR software must be on matching versions or you'll get "incompatible version" on the socket address.

---

## Reconfiguring WiFi / TAK Target

Hold **BOOT** for 3 seconds during normal operation → `HaphazardBridge-Setup` AP opens → connect and browse to `192.168.4.1`.

---

## Full Documentation

See **[HAPHAZARDBRIDGE.md](HAPHAZARDBRIDGE.md)** for:
- Detailed network diagrams for both modes
- All ports and IPs
- Serial monitor usage and expected boot output
- Build, flash, and OTA instructions
- Firmware internals (lwIP crash fix, multi-connection proxy, non-blocking WiFiManager)

---

## License

Do whatever you want with it.
