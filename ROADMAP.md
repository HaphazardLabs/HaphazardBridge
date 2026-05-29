# HaphazardBridge — Development Roadmap

---

## Released

### v1.5 — Initial Release
- W5500 SPI Ethernet initialization
- WiFi STA connection to HaphazardTAK
- ETH→WiFi NAT (SDR outbound routing)
- gRPC port forward (ATAK plugin → SDR gRPC server)
- CoT TCP relay (port 8087)
- SDR web UI proxy (port 8888)
- OTA updates (ArduinoOTA, port 3232)
- Config portal via WiFiManager (HaphazardBridge-Setup AP)
- lwIP core lock crash fix — deferred `ip_napt_enable` and `NetworkServer::begin` to `loop()` via flags

### v1.6 — Mobility & Dismounted Modes
- **Mobility Mode** — AP+STA: connects to upstream WiFi AP and simultaneously broadcasts HaphazardNet
- **Dismounted Mode** — AP only: standalone, no upstream WiFi required
- **HaphazardNet AP** (`192.168.4.1`) broadcast in both modes
- **Bidirectional NAT** in Dismounted Mode — ATAK can reach SDR directly at `192.168.99.234` on any port
- **CoT UDP relay** — SDR sends to ESP32 ETH, ESP32 rebroadcasts to HaphazardNet clients
- **Multi-connection proxy** — 5 concurrent browser↔SDR connection pairs (fixes web UI drops)
- **Mode switching** — tap BOOT within 3s of boot to cycle modes; mode persisted in NVS
- **WiFiManager non-blocking** — `loop()` always runs during portal, BOOT button always responsive

---

## Planned

### v1.7 — Quality of Life
- [ ] **LED mode indicator** — onboard LED shows current mode and status at a glance
- [ ] **Hostname per mode** — `HaphazardBridge-Mobility` / `HaphazardBridge-Dismounted` for easier network identification
- [ ] **DHCP lease tracking** — ESP32 tracks which IP it assigned to ATAK in Dismounted Mode (removes need to manually look up ATAK's IP for SAPIENT config)
- [ ] **Watchdog** — auto-reboot if ETH or WiFi has been down for > N minutes
- [ ] **Remove forced TAK config save** — allow NVS to persist user-set values across reboots without overwriting on every boot

### v1.8 — Mobility Mode Enhancements
- [ ] **Automatic mode fallback** — if HaphazardTAK AP is not found after N retries, automatically drop to Dismounted Mode rather than opening config portal
- [ ] **Roaming support** — reconnect to HaphazardTAK when it comes back in range while running in fallback Dismounted Mode
- [ ] **Multi-AP support** — store and try multiple upstream AP credentials (vehicle AP + base station AP)

### v2.0 — Mesh Mode
- [ ] **ESP-NOW or ESP-MESH-LITE** integration — multiple ESP32 units communicate peer-to-peer
- [ ] **Root node** — one unit has upstream connectivity (vehicle AP or TAK server); relays data to leaf nodes
- [ ] **Leaf/relay nodes** — each bridges its own SDR; forwards CoT and gRPC traffic through the mesh to the root
- [ ] **Automatic role assignment** — node determines root vs. relay vs. leaf based on upstream connectivity
- [ ] **Mesh topology display** — serial output shows mesh structure and hop count

### v2.1 — Security
- [ ] **WPA3 support** on HaphazardNet AP
- [ ] **MAC allowlist** — restrict HaphazardNet to known ATAK device MACs
- [ ] **Per-mode AP passwords** — different passwords for Mobility and Dismounted APs
- [ ] **OTA authentication hardening** — rotate OTA password, support certificate-based OTA

---

## Ideas / Under Consideration

| Idea | Notes |
|---|---|
| **BLE CoT** | Broadcast CoT to ATAK via BLE when WiFi not available. Useful if ATAK device needs to stay on a separate WiFi/cellular network. BLE bandwidth limits web UI use — CoT only. |
| **ATAK server relay** | Relay ATAK server (TAK Server on Pi) connection through the bridge so dismounted ATAK devices connect to the vehicle TAK server via HaphazardNet |
| **Status web page** | Simple HTML page at `http://192.168.4.1/` showing current mode, connected clients, ETH link status, uptime |
| **GPS input** | Accept NMEA from a connected GPS module and include position in CoT output — removes need for static location config |
| **Multi-SDR support** | Multiple SDRs on the ETH side using a dumb switch, each with a unique IP; separate proxy/relay per SDR |

---

## Known Limitations

| Limitation | Workaround |
|---|---|
| WiFi IP in Mobility Mode is DHCP — may change | Check serial output on boot; update ATAK plugin socket address if it changes |
| Single gRPC connection at a time | gRPC is multiplexed over HTTP/2 — one TCP connection is typically sufficient |
| BOOT tap window (3s) has no visual indicator | Press RESET, count to 1, tap BOOT — the window is generous |
| SDR CoT UDP target must change between modes | Mobility: `192.168.10.123:4242` / Dismounted: `192.168.99.1:4242` |
| Mesh Mode not yet implemented | Use multiple independent bridges on the same AP in the meantime |
