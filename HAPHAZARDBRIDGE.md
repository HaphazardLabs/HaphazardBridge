# HaphazardBridge

**ESP32-S3-ETH WiFi↔Ethernet bridge for connecting an SDR to ATAK over a WiFi network the SDR can't join directly.**

---

## What It Does

HaphazardBridge sits between an SDR (connected via Ethernet) and an ATAK device (connected via WiFi). It provides:

| Service | Description |
|---|---|
| **NAT** | Routes all outbound SDR traffic onto the WiFi network transparently |
| **gRPC forward** | Bridges ATAK's plugin connection to the SDR's gRPC server |
| **CoT TCP relay** | Accepts TCP CoT connections and relays to a configured target |
| **SDR web UI proxy** | Makes the SDR's web interface reachable from WiFi |
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

## Network Layout

```
┌──────────────────────────────────────────────────────────┐
│               HaphazardTAK WiFi (192.168.10.x)           │
│                                                          │
│  ┌───────────────────┐      ┌──────────────────────────┐ │
│  │   ATAK Device     │      │    HaphazardBridge       │ │
│  │  192.168.10.123   │      │  WiFi: 192.168.10.178    │ │
│  │  callsign WOXOF   │      │  ETH:  192.168.99.1      │ │
│  │                   │      │                          │ │
│  │ plugin → :8000 ───┼──────┼→ gRPC fwd → SDR:8000    │ │
│  │ CoT UDP ← :4242 ──┼──────┼← NAT ←─────────────     │ │
│  │ SAPIENT ←─────────┼──────┼← NAT ←─────────────     │ │
│  └───────────────────┘      └──────────┬───────────────┘ │
└─────────────────────────────────────────┼────────────────┘
                                          │ Ethernet cable
                          ┌───────────────▼──────────────┐
                          │          SDR Device          │
                          │      192.168.99.234 (static) │
                          │      Gateway: 192.168.99.1   │
                          │                              │
                          │  gRPC server   port 8000     │
                          │  CoT UDP out → 192.168.10.123:4242
                          │  SAPIENT client → 192.168.10.123
                          └──────────────────────────────┘
```

---

## IP Addresses

| Device | Interface | IP |
|---|---|---|
| HaphazardBridge | WiFi (HaphazardTAK) | `192.168.10.178` (DHCP) |
| HaphazardBridge | Ethernet | `192.168.99.1` (static) |
| SDR | Ethernet | `192.168.99.234` (static) |
| ATAK device | WiFi | `192.168.10.123` |

The ESP32's WiFi IP is assigned by DHCP and may change. Check serial output on boot for the current address.

---

## Data Flows

### 1. gRPC (ATAK plugin → SDR sensor stream)
- ATAK plugin connects to `192.168.10.178:8000`
- ESP32 forwards the connection to `192.168.99.234:8000` (SDR gRPC server)
- Sensor data streams back to ATAK through the same connection

### 2. CoT UDP (SDR → ATAK map)
- SDR sends UDP CoT packets to `192.168.10.123:4242`
- NAT routes these through the ESP32's WiFi interface transparently
- ATAK displays tracks on the map

### 3. SAPIENT (SDR → ATAK plugin registration)
- SDR SAPIENT client connects outbound to ATAK (`192.168.10.123`)
- NAT handles the routing — no port forwarding needed
- This is what populates the sensor entry in the ATAK plugin

### 4. SDR Web UI Proxy
- Browse to `http://192.168.10.178:8888/` from any WiFi device
- ESP32 proxies the connection to `192.168.99.234:80`

---

## Ports Reference

| Port | Interface | Purpose |
|---|---|---|
| `8000` | WiFi (inbound) | gRPC forward to SDR |
| `8087` | Both (inbound) | CoT TCP relay |
| `8888` | WiFi (inbound) | SDR web UI proxy |
| `3232` | WiFi | OTA update (ArduinoOTA) |

---

## Build & Flash

### Prerequisites
- [PlatformIO](https://platformio.org/) (pioarduino espressif32 53.03.10 / Arduino-ESP32 3.1.0)
- Project at `/home/haphazardlabs/Documents/HaphazardBridge/`

### Normal flash (USB)
```bash
cd ~/Documents/HaphazardBridge
~/.platformio-venv/bin/pio run -e haphazardbridge -t upload
```

### If the board is crash-looping (can't flash normally)
Enter ROM bootloader manually:
1. Hold **BOOT** button
2. Press and release **RESET**
3. Release **BOOT**
4. Run the flash command above
5. Press **RESET** once after flash completes to start the app

### OTA flash (no USB, over WiFi)
Edit `platformio.ini` — change:
```ini
upload_protocol = espota
upload_port = 192.168.10.178
upload_flags =
    --auth=H@phazard!
    --timeout=60
```
Then run the normal flash command.

---

## Serial Monitor

The board uses native USB CDC. To read serial output:

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

> **Note:** Toggling DTR low then high (`dtr=False` → `dtr=True`) triggers a USB reset and reboots the board. Keep DTR high only for passive monitoring.

### Expected boot output
```
╔══════════════════════════╗
║    HaphazardBridge v1.5  ║
╚══════════════════════════╝
[Config] TAK 192.168.10.123:8000 saved
[Config] TAK 192.168.10.123:8000
[ETH] Static 192.168.99.1/255.255.255.0
[WiFi] Connecting...
[WiFi] IP: 192.168.10.178
[Proxy] SDR UI  → port 8888
[CoT]   Relay   → port 8087 → 192.168.10.123:8000
[gRPC]  Forward → port 8000 → 192.168.99.234:8000
[OTA] Ready  hostname:HaphazardBridge  port:3232
[ETH] Link up
[Bridge] NAT active  ETH(192.168.99.x) → WiFi
```

---

## WiFi Configuration

WiFi credentials are hardcoded in `setup()`:
```cpp
WiFi.begin("HaphazardTAK", "H@phazard!");
```

### Reconfiguring WiFi via captive portal
- **During normal operation:** Hold **BOOT** for 3 seconds → ESP32 opens AP `HaphazardBridge-Setup`
- **At power-on:** Hold **BOOT** while powering on → clears all saved WiFi + TAK config, resets to defaults

Connect to `HaphazardBridge-Setup` and browse to `192.168.4.1` to configure:
- WiFi network + password
- TAK Server IP and port

---

## ATAK Configuration

### SDR Plugin
| Setting | Value |
|---|---|
| Socket Address | `192.168.10.178:8000` |

> The socket address must use the ESP32's current WiFi IP. If the DHCP lease changes, update this.

> **Version compatibility:** The ATAK SDR plugin and the SDR software must be on compatible versions. A mismatch produces "incompatible version" and clears the socket address.

### CoT UDP Input
Add a UDP CoT stream in ATAK (**Settings → Network → CoT Streams → +**):
| Setting | Value |
|---|---|
| Type | UDP |
| Port | `4242` |

### SAPIENT
Enable the SAPIENT client in the SDR software and point it at the ATAK device's IP (`192.168.10.123`). NAT handles the routing — no port forwarding needed.

---

## SDR Configuration

| Setting | Value |
|---|---|
| IP address | `192.168.99.234` (static) |
| Subnet mask | `255.255.255.0` |
| Gateway | `192.168.99.1` |
| CoT UDP client target | `192.168.10.123:4242` |
| SAPIENT client target | `192.168.10.123` |
| Static location (no GPS) | Lat `38.2362`, Lon `-78.3603` (Ruckersville, VA) |

---

## Linux Host Notes (Ubuntu Core Desktop)

### ModemManager conflict
ModemManager grabs `/dev/ttyACM0` on USB connect and blocks serial access:
```bash
sudo systemctl stop ModemManager
```

### udev rule (already in place)
`/etc/udev/rules.d/99-esp32.rules` grants world read/write to the ESP32's USB device (`303a:1001`) so no `sudo` is needed for flashing or monitoring.

---

## Firmware Internals

### Why lwIP calls are deferred to `loop()`

Arduino network event callbacks on ESP32 run on the system event task, which does **not** hold the lwIP core lock. Calling any lwIP function (`ip_napt_enable`, `NetworkServer::begin`, `ETH.config`) from a callback triggers:

```
assert failed: sys_timeout — Required to lock TCPIP core functionality!
```

**Fix:** event handlers only set boolean flags. `loop()` checks those flags and does the actual work:

```cpp
static bool needServerSetup = false;
static bool needNAT         = false;

// In loop():
if (needNAT) {
    needNAT = false;
    enableNAT();           // runs tcpip_callback() — safe from any task
}
if (needServerSetup) {
    needServerSetup = false;
    proxyServer.begin();   // safe from loop task
    cotServer.begin();
    grpcFwdServer.begin();
    setupOTA();
}
```

`ip_napt_enable` additionally requires running on the lwIP core task itself:
```cpp
static void napt_enable_cb(void *) {
    ip_napt_enable(ipaddr_addr(ETH_IP), 1);
}

void enableNAT() {
    tcpip_callback(napt_enable_cb, nullptr);
}
```

### Why `configEthStatic()` is only called from `ETH_START`

`ETH.config()` touches lwIP internals. It works from `ARDUINO_EVENT_ETH_START` because the lwIP stack isn't fully running yet at that point. Calling it from `ARDUINO_EVENT_ETH_CONNECTED` (which fires later when the physical link is established) causes the same lwIP lock crash. Static IP persists through link reconnects so it only needs to be set once.

### Non-blocking relay pattern

All three relays (`handleProxy`, `handleCoT`, `handleGrpc`) use the same pattern — one call per `loop()` iteration, no blocking:

```
accept() → connect to target → bidirectional relay via relay() → idle timeout → close
```

Idle timeouts: proxy 8s, CoT relay 60s, gRPC forward 300s.

---

## OTA Credentials

| Field | Value |
|---|---|
| Hostname | `HaphazardBridge` |
| Password | `H@phazard!` |
| Port | `3232` |
