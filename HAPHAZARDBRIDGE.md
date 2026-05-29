# HaphazardBridge

**ESP32-S3-ETH WiFi↔Ethernet bridge for connecting an SDR to ATAK — without the SDR needing WiFi.**

---

## What It Does

HaphazardBridge sits between an SDR (Ethernet) and an ATAK device (WiFi). It operates in two modes selectable at boot, and provides:

| Service | Description |
|---|---|
| **NAT (ETH→WiFi)** | Routes outbound SDR traffic onto the WiFi network transparently |
| **NAT (AP→ETH)** | Routes ATAK traffic to the SDR on any port — no port forwarding needed (Dismounted Mode) |
| **gRPC forward** | Bridges ATAK's plugin connection to the SDR's gRPC server |
| **CoT TCP relay** | Accepts TCP CoT connections and relays to a configured target |
| **CoT UDP relay** | Receives CoT UDP from the SDR and rebroadcasts to ATAK (Dismounted Mode) |
| **SDR web UI proxy** | Makes the SDR's web interface reachable from WiFi (5 concurrent connections) |
| **OTA updates** | Firmware updates over WiFi — no USB required after initial flash |

---

## Hardware

**Board:** Waveshare ESP32-S3-ETH (W5500 SPI Ethernet)

| W5500 Pin | GPIO |
|---|---|
| SCK | 13 |
| MISO | 12 |
| MOSI | 11 |
| CS | 14 |
| IRQ | 10 |
| RST | 9 |

---

## Operating Modes

### Mobility Mode
ESP32 connects to an upstream WiFi AP (e.g. a vehicle-mounted router) **and** simultaneously broadcasts its own **HaphazardNet** AP. ATAK can connect via either network.

```
HaphazardTAK AP (vehicle)
    ├── HaphazardBridge (STA + AP)  ←── SDR (ETH)
    │        └── HaphazardNet AP
    │                 └── ATAK (direct)
    └── ATAK (via vehicle AP)
```

### Dismounted Mode
No upstream WiFi. ESP32 creates the **HaphazardNet** AP. ATAK connects directly to the bridge. Fully standalone — no vehicle or infrastructure needed.

```
HaphazardNet AP
    ├── ATAK device (direct)
    └── HaphazardBridge  ←── SDR (ETH)
```

---

## Network Layout

### Mobility Mode
```
┌──────────────────────────────────────────────────────────┐
│               HaphazardTAK WiFi (192.168.10.x)           │
│  ┌───────────────────┐      ┌──────────────────────────┐ │
│  │   ATAK Device     │      │    HaphazardBridge       │ │
│  │  192.168.10.123   │      │  STA:  192.168.10.178    │ │
│  │ plugin → :8000 ───┼──────┼→ gRPC fwd → SDR:8000    │ │
│  │ CoT UDP ← :4242 ──┼──────┼← NAT ←────────────      │ │
│  │ SAPIENT ←─────────┼──────┼← NAT ←────────────      │ │
│  └───────────────────┘      │  AP:   192.168.4.1       │ │
└─────────────────────────────┤  ETH:  192.168.99.1      │─┘
                              └──────────┬───────────────┘
                                         │ Ethernet
                         ┌───────────────▼──────────────┐
                         │          SDR Device          │
                         │      192.168.99.234 (static) │
                         │      Gateway: 192.168.99.1   │
                         │  CoT UDP → 192.168.10.123:4242
                         └──────────────────────────────┘
```

### Dismounted Mode
```
┌─────────────────────────────────────────────────────────┐
│              HaphazardNet AP (192.168.4.x)               │
│  ┌────────────────┐      ┌────────────────────────────┐  │
│  │  ATAK Device   │      │      HaphazardBridge       │  │
│  │  192.168.4.x   │      │      AP: 192.168.4.1       │  │
│  │                │      │      ETH: 192.168.99.1     │  │
│  │ plugin → :8000─┼──────┼→ gRPC fwd → SDR:8000      │  │
│  │ CoT UDP ← :4242┼──────┼← UDP relay ←──────────    │  │
│  │                │      │                            │  │
│  │ direct access ─┼──────┼→ NAT (AP→ETH) → SDR:any  │  │
│  │ 192.168.99.234 │      │                            │  │
│  └────────────────┘      └──────────────┬─────────────┘  │
└─────────────────────────────────────────┼────────────────┘
                                          │ Ethernet
                          ┌───────────────▼──────────────┐
                          │          SDR Device          │
                          │      192.168.99.234 (static) │
                          │      Gateway: 192.168.99.1   │
                          │  CoT UDP → 192.168.99.1:4242 │
                          └──────────────────────────────┘
```

---

## IP Addresses

| Device | Interface | IP |
|---|---|---|
| HaphazardBridge | HaphazardNet AP | `192.168.4.1` (static) |
| HaphazardBridge | WiFi STA (Mobility) | `192.168.10.178` (DHCP — may change) |
| HaphazardBridge | Ethernet | `192.168.99.1` (static) |
| SDR | Ethernet | `192.168.99.234` (static) |
| ATAK device | HaphazardTAK (Mobility) | `192.168.10.123` |
| ATAK device | HaphazardNet (Dismounted) | `192.168.4.x` (DHCP) |

---

## Data Flows

### Mobility Mode

| Flow | Path |
|---|---|
| gRPC | ATAK → `192.168.10.178:8000` → ESP32 → SDR:8000 |
| CoT UDP | SDR → `192.168.10.123:4242` via NAT |
| SAPIENT | SDR → `192.168.10.123` outbound via NAT |
| SDR web UI | Browser → `http://192.168.10.178:8888` → SDR:80 |
| HaphazardNet gRPC | ATAK → `192.168.4.1:8000` → ESP32 → SDR:8000 |
| HaphazardNet web UI | Browser → `http://192.168.4.1:8888` → SDR:80 |

### Dismounted Mode

| Flow | Path |
|---|---|
| gRPC | ATAK → `192.168.4.1:8000` → ESP32 → SDR:8000 |
| CoT UDP | SDR → `192.168.99.1:4242` → ESP32 rebroadcasts → `192.168.4.255:4242` → ATAK |
| SAPIENT | SDR → ATAK outbound via NAT |
| SDR web UI (direct) | Browser → `http://192.168.99.234` — any port, via bidirectional NAT |
| SDR web UI (proxy) | Browser → `http://192.168.4.1:8888` → SDR:80 |

> **Bidirectional NAT in Dismounted Mode:** ATAK devices on HaphazardNet can reach the SDR directly at `192.168.99.234` on any port — web UI, WebSocket, gRPC, or otherwise. No explicit port forwarding needed. The ESP32 NATs AP (192.168.4.x) traffic to appear as 192.168.99.1 to the SDR, and routes responses back to ATAK transparently.

---

## Ports Reference

| Port | Interface | Purpose |
|---|---|---|
| `8000` | WiFi (inbound) | gRPC forward to SDR |
| `8087` | Both (inbound) | CoT TCP relay |
| `8888` | WiFi (inbound) | SDR web UI proxy (5 concurrent connections) |
| `4242` | ETH (inbound, Dismounted) | CoT UDP relay from SDR |
| `3232` | WiFi | OTA update (ArduinoOTA) |

---

## BOOT Button Reference

| Action | When | Result |
|---|---|---|
| **Tap** (< 1.5s) | Within 3s of boot | Cycle Mobility ↔ Dismounted |
| **Hold** (1.5s+) | Within 3s of boot | Factory reset all config |
| **Hold** (3s+) | During normal operation | Open config portal |

### Switching modes step by step

1. Press **RESET** on the board
2. Within **3 seconds**, tap and release **BOOT**
3. Watch your WiFi list — the AP name changes to confirm the new mode:
   - **HaphazardNet** = Dismounted Mode active
   - **HaphazardBridge-Setup** = Mobility Mode, searching for upstream AP

---

## ATAK Configuration

### Mobility Mode
| Setting | Value |
|---|---|
| SDR plugin socket address | `192.168.10.178:8000` *(or `192.168.4.1:8000` via HaphazardNet)* |
| CoT UDP input port | `4242` |
| SAPIENT client target | `192.168.10.123` (ATAK's own IP) |

### Dismounted Mode
| Setting | Value |
|---|---|
| SDR plugin socket address | `192.168.4.1:8000` |
| CoT UDP input port | `4242` |
| SAPIENT client target | `192.168.10.123` (unchanged — NAT handles routing) |
| SDR web UI | Browse directly to `http://192.168.99.234` (preferred over proxy) |

> **Version compatibility:** The ATAK SDR plugin and SDR software must be on matching versions. A mismatch shows "incompatible version" and clears the socket address.

---

## SDR Configuration

### Mobility Mode
| Setting | Value |
|---|---|
| IP address | `192.168.99.234` (static) |
| Subnet mask | `255.255.255.0` |
| Gateway | `192.168.99.1` |
| CoT UDP target | `192.168.10.123:4242` |
| SAPIENT client target | `192.168.10.123` |
| Static location (no GPS) | Lat `38.2362`, Lon `-78.3603` (Ruckersville, VA) |

### Dismounted Mode
| Setting | Value |
|---|---|
| IP address | `192.168.99.234` (static) |
| Subnet mask | `255.255.255.0` |
| Gateway | `192.168.99.1` |
| CoT UDP target | `192.168.99.1:4242` *(ESP32 ETH — relayed to ATAK)* |
| SAPIENT client target | `192.168.10.123` *(unchanged — NAT routes through ESP32)* |
| Static location (no GPS) | Lat `38.2362`, Lon `-78.3603` (Ruckersville, VA) |

---

## WiFi Configuration

### Changing WiFi credentials or TAK target
Hold **BOOT** for 3 seconds during normal operation → portal AP `HaphazardBridge-Setup` appears → connect and browse to `192.168.4.1`.

### Hardcoded credentials (firmware)
```cpp
WiFi.begin("HaphazardTAK", "H@phazard!");
```

---

## Build & Flash

### Prerequisites
- [PlatformIO](https://platformio.org/) (pioarduino espressif32 53.03.10 / Arduino-ESP32 3.1.0)

### Normal flash (USB)
```bash
cd ~/Documents/HaphazardBridge
~/.platformio-venv/bin/pio run -e haphazardbridge -t upload
```

### If the board is crash-looping
Enter ROM bootloader manually:
1. Hold **BOOT**
2. Press and release **RESET**
3. Release **BOOT**
4. Run the flash command
5. Press **RESET** after flash completes to start the app

### OTA flash (no USB)
Edit `platformio.ini`:
```ini
upload_protocol = espota
upload_port = 192.168.10.178
upload_flags =
    --auth=H@phazard!
    --timeout=60
```

---

## Serial Monitor

```python
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
s.dtr = True
start = time.time()
while time.time() - start < 60:
    chunk = s.read(512)
    if chunk:
        print(chunk.decode('utf-8', errors='replace'), end='', flush=True)
s.close()
"
```

> **Note:** Toggling DTR low→high reboots the board. Keep `dtr=True` only for passive monitoring.

### Expected boot output — Mobility Mode
```
╔══════════════════════════════╗
║    HaphazardBridge  v1.6     ║
║  Mode: Mobility              ║
╚══════════════════════════════╝
[Mode] Mobility — tap BOOT within 3s to switch
[ETH] Static 192.168.99.1/255.255.255.0
[WiFi] Mobility — connecting to upstream AP + HaphazardNet
[AP] HaphazardNet up — 192.168.4.1
[WiFi] STA IP: 192.168.10.178
[Proxy] SDR UI   → 192.168.10.178:8888
[CoT]   Relay    → port 8087
[gRPC]  Forward  → 192.168.10.178:8000 → SDR
[OTA] Ready  hostname:HaphazardBridge  port:3232
[ETH] Link up
[Bridge] NAT active  ETH(192.168.99.x) → WiFi
```

### Expected boot output — Dismounted Mode
```
╔══════════════════════════════╗
║    HaphazardBridge  v1.6     ║
║  Mode: Dismounted            ║
╚══════════════════════════════╝
[Mode] Dismounted — tap BOOT within 3s to switch
[ETH] Static 192.168.99.1/255.255.255.0
[WiFi] Dismounted — starting HaphazardNet AP only
[AP] HaphazardNet up — 192.168.4.1
[Proxy] SDR UI   → 192.168.4.1:8888
[CoT]   Relay    → port 8087
[gRPC]  Forward  → 192.168.4.1:8000 → SDR
[CoT UDP] Relay  → ETH:4242 → AP broadcast
[Config] SDR CoT UDP target: 192.168.99.1:4242
[Config] ATAK plugin socket: 192.168.4.1:8000
[Config] SDR direct access:  192.168.99.234
[OTA] Ready  hostname:HaphazardBridge  port:3232
[ETH] Link up
[Bridge] NAT active  ETH(192.168.99.x) → WiFi
[Bridge] NAT active  AP(192.168.4.x) → ETH
```

---

## Linux Host Notes (Ubuntu Core Desktop)

### ModemManager conflict
```bash
sudo systemctl stop ModemManager
```

### udev rule
`/etc/udev/rules.d/99-esp32.rules` grants world read/write to `303a:1001` — no `sudo` needed for flash or monitor.

---

## Firmware Internals

### lwIP core lock crash fix

Arduino network event callbacks do **not** hold the lwIP core lock. Calling `ip_napt_enable`, `NetworkServer::begin`, or `ETH.config` from a callback triggers:
```
assert failed: sys_timeout — Required to lock TCPIP core functionality!
```

**Fix:** callbacks only set flags; `loop()` does the actual work. `ip_napt_enable` is dispatched to the lwIP task via `tcpip_callback()`.

### `configEthStatic()` called only from `ETH_START`

`ETH.config()` crashes if called from `ETH_CONNECTED` (lwIP is fully running by then). Static IP persists through link reconnects so one-time setup in `ETH_START` is sufficient.

### Bidirectional NAT (Dismounted Mode)

In Dismounted Mode, NAT is enabled in both directions:

- **ETH→WiFi NAT** (both modes): SDR traffic out to the WiFi network
- **AP→ETH NAT** (Dismounted only): ATAK traffic to the SDR on any port

The AP NAT translates ATAK's source address (192.168.4.x) to 192.168.99.1 before forwarding to the SDR. The SDR routes responses to 192.168.99.1 (its gateway), and the ESP32 unNATs and delivers back to ATAK. This makes the full SDR — web UI, WebSocket, gRPC, any port — accessible directly at `192.168.99.234` without explicit port forwarding.

### Multi-connection proxy

The SDR web UI proxy maintains a pool of 5 concurrent `(browser, sdr)` connection pairs. Modern browsers open multiple simultaneous connections to load CSS, JS, and images — a single-connection proxy causes partial loads and drops. In Dismounted Mode, direct access to `192.168.99.234` via bidirectional NAT is preferred over the proxy.

### WiFiManager non-blocking mode

`wm.setConfigPortalBlocking(false)` keeps `loop()` running during the portal. This ensures the BOOT button 3-second startup window is processed promptly regardless of WiFiManager state.

### Idle timeouts

| Service | Timeout |
|---|---|
| SDR web UI proxy | 60s |
| CoT TCP relay | 60s |
| gRPC forward | 300s |

---

## OTA Credentials

| Field | Value |
|---|---|
| Hostname | `HaphazardBridge` |
| Password | `H@phazard!` |
| Port | `3232` |
