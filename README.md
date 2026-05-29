# HaphazardBridge

**ESP32-S3-ETH firmware that bridges an SDR to an ATAK device over WiFi.**

The SDR plugs into the board's Ethernet port. The board joins your WiFi network. ATAK sees the SDR as if it were on the same network — without the SDR needing WiFi.

---

## Architecture

```
ATAK (192.168.10.123)                    SDR (192.168.99.234)
        │                                         │
        │  gRPC ──→ ESP32:8000 ──────────────→ SDR:8000
        │                                         │
        │  CoT UDP ←─────────────── NAT ←── SDR:4242
        │                                         │
        │  SAPIENT ←──────────────── NAT ←── SDR outbound
        │
   ESP32 WiFi: 192.168.10.178  (HaphazardTAK)
   ESP32 ETH:  192.168.99.1
```

| Service | What it does |
|---|---|
| **NAT** | Routes all SDR traffic onto the WiFi network |
| **gRPC forward** | `ESP32:8000` → `SDR:8000` so ATAK plugin can connect to SDR's gRPC server |
| **CoT UDP** | SDR sends tracks to ATAK via NAT |
| **SAPIENT** | SDR registers itself with ATAK plugin via NAT |
| **SDR web UI proxy** | `ESP32:8888` → `SDR:80` |
| **OTA** | Firmware updates over WiFi, no USB needed |

---

## Hardware

[Waveshare ESP32-S3-ETH](https://www.waveshare.com/esp32-s3-eth.htm) — ESP32-S3 with onboard W5500 SPI Ethernet.

---

## Quick Start

### 1. Flash the firmware

```bash
git clone https://github.com/HaphazardLabs/HaphazardBridge.git
cd HaphazardBridge
~/.platformio-venv/bin/pio run -e haphazardbridge -t upload
```

Requires [PlatformIO](https://platformio.org/).

### 2. Configure the SDR

| Setting | Value |
|---|---|
| IP | `192.168.99.234` (static) |
| Gateway | `192.168.99.1` |
| CoT UDP target | `192.168.10.123:4242` |
| SAPIENT client target | `192.168.10.123` |
| Static location | Lat `38.2362` Lon `-78.3603` *(if no GPS)* |

### 3. Configure ATAK

| Setting | Value |
|---|---|
| SDR plugin socket address | `192.168.10.178:8000` |
| CoT UDP input | UDP port `4242` |

> The ESP32 WiFi IP (`192.168.10.178`) is DHCP-assigned and may change. Check serial output on boot for the current address.

---

## Reconfiguring WiFi

Hold **BOOT** for 3 seconds → opens AP `HaphazardBridge-Setup` → browse to `192.168.4.1`.

---

## Full Documentation

See **[HAPHAZARDBRIDGE.md](HAPHAZARDBRIDGE.md)** for complete details:
- Build & flash instructions (including crash-loop recovery)
- Serial monitor usage
- All ports and IPs
- Firmware internals and the lwIP crash fix
- OTA update setup
- Linux host notes

---

## License

Do whatever you want with it.
