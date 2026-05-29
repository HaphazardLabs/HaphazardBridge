#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <NetworkUdp.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <NetworkServer.h>
#include <NetworkClient.h>
#include <Preferences.h>
#include <esp_netif.h>
#include <lwip/lwip_napt.h>
#include <lwip/tcpip.h>

// ── Waveshare ESP32-S3-ETH W5500 SPI pins ────────────────────────────────────
#define W5500_SCK   13
#define W5500_MISO  12
#define W5500_MOSI  11
#define W5500_CS    14
#define W5500_IRQ   10
#define W5500_RST    9

// ── Ethernet bridge config ────────────────────────────────────────────────────
#define ETH_IP      "192.168.99.1"
#define ETH_NETMASK "255.255.255.0"

// ── SDR ───────────────────────────────────────────────────────────────────────
#define SDR_HTTP_PORT   80
#define SDR_GRPC_PORT   8000
#define PROXY_PORT      8888
#define COT_LISTEN_PORT 8087
#define GRPC_FWD_PORT   8000
#define COT_UDP_PORT    4242

// ── HaphazardNet AP (Mobility + Dismounted) ───────────────────────────────────
#define AP_SSID "HaphazardNet"
#define AP_PASS "H@phazard!"

// ── Config button (BOOT = GPIO0) ──────────────────────────────────────────────
#define CFG_BTN 0

// ── Operating modes ───────────────────────────────────────────────────────────
enum BridgeMode : uint8_t {
    MODE_MOBILITY   = 0,   // STA + SoftAP — connects to upstream WiFi AP
    MODE_DISMOUNTED = 1,   // SoftAP only  — standalone, ATAK connects direct
    MODE_MESH       = 2    // future
};
static BridgeMode currentMode = MODE_MOBILITY;

static const char *modeName(BridgeMode m) {
    switch (m) {
        case MODE_MOBILITY:   return "Mobility";
        case MODE_DISMOUNTED: return "Dismounted";
        case MODE_MESH:       return "Mesh (N/A)";
        default:              return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
WiFiManager wm;
NetworkServer proxyServer(PROXY_PORT);
NetworkServer cotServer(COT_LISTEN_PORT);
NetworkServer grpcFwdServer(GRPC_FWD_PORT);
NetworkUDP    cotUdp;

static bool ethUp           = false;
static bool wifiUp          = false;
static bool otaReady        = false;
static bool needServerSetup = false;
static bool needNAT         = false;
static bool udpRelayActive  = false;

// TAK target — saved in NVS, editable via captive portal
static char cotIpStr[20]  = "192.168.10.123";
static char cotPortStr[6] = "8000";
static IPAddress cotIp(192, 168, 10, 123);
static uint16_t  cotPortNum = 8000;

// Non-blocking relay state
#define PROXY_MAX_CONN 5
struct ProxyConn { NetworkClient browser, sdr; uint32_t idle; };
static ProxyConn     proxyConns[PROXY_MAX_CONN];
static NetworkClient cotClient, cotTak;
static uint32_t      cotIdle;
static NetworkClient grpcAtak, grpcSdr;
static uint32_t      grpcIdle;

// ─────────────────────────────────────────────────────────────────────────────
void loadConfig() {
    Preferences prefs;
    prefs.begin("bridge", true);
    currentMode = (BridgeMode)prefs.getUChar("mode", MODE_MOBILITY);
    prefs.getString("cot_ip",   cotIpStr,   sizeof(cotIpStr));
    prefs.getString("cot_port", cotPortStr, sizeof(cotPortStr));
    prefs.end();
    cotIp.fromString(cotIpStr);
    cotPortNum = (uint16_t)atoi(cotPortStr);
}

void saveConfig(const char *ip, const char *port) {
    strncpy(cotIpStr,   ip,   sizeof(cotIpStr)   - 1);
    strncpy(cotPortStr, port, sizeof(cotPortStr) - 1);
    cotIp.fromString(cotIpStr);
    cotPortNum = (uint16_t)atoi(cotPortStr);
    Preferences prefs;
    prefs.begin("bridge", false);
    prefs.putString("cot_ip",   cotIpStr);
    prefs.putString("cot_port", cotPortStr);
    prefs.end();
    Serial.printf("[Config] TAK %s:%s saved\n", cotIpStr, cotPortStr);
}

void saveMode(BridgeMode m) {
    currentMode = m;
    Preferences prefs;
    prefs.begin("bridge", false);
    prefs.putUChar("mode", (uint8_t)m);
    prefs.end();
    Serial.printf("[Mode] Saved: %s\n", modeName(m));
}

// ─────────────────────────────────────────────────────────────────────────────
static void napt_enable_cb(void *) {
    ip_napt_enable(ipaddr_addr(ETH_IP), 1);
    Serial.println("[Bridge] NAT active  ETH(192.168.99.x) → WiFi");
}

static void napt_ap_enable_cb(void *) {
    ip_napt_enable(ipaddr_addr("192.168.4.1"), 1);
    Serial.println("[Bridge] NAT active  AP(192.168.4.x) → ETH");
}

void enableNAT() {
    tcpip_callback(napt_enable_cb, nullptr);
}

void enableApNAT() {
    tcpip_callback(napt_ap_enable_cb, nullptr);
}

void configEthStatic() {
    ETH.config(IPAddress(192,168,99,1), IPAddress(192,168,99,1),
               IPAddress(255,255,255,0));
    Serial.printf("[ETH] Static %s/%s\n", ETH_IP, ETH_NETMASK);
}

void setupOTA() {
    ArduinoOTA.setHostname("HaphazardBridge");
    ArduinoOTA.setPassword("H@phazard!");
    ArduinoOTA.onStart([]() { Serial.println("[OTA] Starting..."); });
    ArduinoOTA.onEnd([]()   { Serial.println("\n[OTA] Done — rebooting"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Error %u\n", e); });
    ArduinoOTA.begin();
    otaReady = true;
    Serial.println("[OTA] Ready  hostname:HaphazardBridge  port:3232");
}

// ─────────────────────────────────────────────────────────────────────────────
static bool relay(NetworkClient &a, NetworkClient &b) {
    bool active = false;
    if (a.available()) {
        uint8_t buf[512];
        int n = a.read(buf, sizeof(buf));
        if (n > 0) { b.write(buf, n); active = true; }
    }
    if (b.available()) {
        uint8_t buf[512];
        int n = b.read(buf, sizeof(buf));
        if (n > 0) { a.write(buf, n); active = true; }
    }
    return active;
}

// SDR web UI proxy — up to PROXY_MAX_CONN concurrent connections
void handleProxy() {
    // Service existing connections
    for (int i = 0; i < PROXY_MAX_CONN; i++) {
        ProxyConn &c = proxyConns[i];
        if (!c.browser && !c.sdr) continue;
        if (!c.browser.connected() || !c.sdr.connected() ||
            millis() - c.idle > 60000) {
            c.browser.stop(); c.sdr.stop();
        } else {
            if (relay(c.browser, c.sdr)) c.idle = millis();
        }
    }
    // Accept new connection into a free slot
    NetworkClient client = proxyServer.accept();
    if (!client) return;
    for (int i = 0; i < PROXY_MAX_CONN; i++) {
        ProxyConn &c = proxyConns[i];
        if (c.browser || c.sdr) continue;
        NetworkClient sdr;
        if (!sdr.connect(IPAddress(192,168,99,234), SDR_HTTP_PORT)) {
            Serial.println("[Proxy] SDR unreachable");
            client.stop(); return;
        }
        c.browser = client;
        c.sdr     = sdr;
        c.idle    = millis();
        return;
    }
    // All slots full
    client.stop();
}

// CoT TCP relay
void handleCoT() {
    if (cotClient && cotTak) {
        if (!cotClient.connected() || !cotTak.connected() ||
            millis() - cotIdle > 60000) {
            cotClient.stop(); cotTak.stop();
            Serial.println("[CoT] Relay closed");
        } else {
            if (relay(cotClient, cotTak)) cotIdle = millis();
        }
        return;
    }
    NetworkClient client = cotServer.accept();
    if (!client) return;
    NetworkClient tak;
    if (!tak.connect(cotIp, cotPortNum)) {
        Serial.printf("[CoT] Cannot reach TAK %s:%d\n", cotIpStr, cotPortNum);
        client.stop(); return;
    }
    cotClient = client;
    cotTak    = tak;
    cotIdle   = millis();
    Serial.printf("[CoT] Relay open → %s:%d\n", cotIpStr, cotPortNum);
}

// gRPC port forward — ATAK plugin → ESP32:8000 → SDR gRPC server
void handleGrpc() {
    if (grpcAtak && grpcSdr) {
        if (!grpcAtak.connected() || !grpcSdr.connected() ||
            millis() - grpcIdle > 300000) {
            grpcAtak.stop(); grpcSdr.stop();
            Serial.println("[gRPC] Closed");
        } else {
            if (relay(grpcAtak, grpcSdr)) grpcIdle = millis();
        }
        return;
    }
    NetworkClient client = grpcFwdServer.accept();
    if (!client) return;
    NetworkClient sdr;
    if (!sdr.connect(IPAddress(192,168,99,234), SDR_GRPC_PORT)) {
        Serial.println("[gRPC] SDR unreachable");
        client.stop(); return;
    }
    grpcAtak = client;
    grpcSdr  = sdr;
    grpcIdle = millis();
    Serial.println("[gRPC] Connected → SDR:8000");
}

// CoT UDP relay — Dismounted Mode only
// SDR sends CoT UDP to ESP32 ETH (192.168.99.1:4242)
// ESP32 rebroadcasts to HaphazardNet (192.168.4.255:4242) for ATAK
void handleCotUdpRelay() {
    int size = cotUdp.parsePacket();
    if (size <= 0) return;
    uint8_t buf[2048];
    int n = cotUdp.read(buf, sizeof(buf));
    if (n <= 0) return;
    IPAddress bcast(192, 168, 4, 255);
    cotUdp.beginPacket(bcast, COT_UDP_PORT);
    cotUdp.write(buf, n);
    cotUdp.endPacket();
}

// ─────────────────────────────────────────────────────────────────────────────
void onNetEvent(arduino_event_t *ev) {
    switch (ev->event_id) {

    case ARDUINO_EVENT_ETH_START:
        configEthStatic();
        break;

    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("[ETH] Link up");
        ethUp = true;
        if (wifiUp) needNAT = true;
        break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("[ETH] Link down");
        ethUp = false;
        break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.printf("[WiFi] STA IP: %s\n", WiFi.localIP().toString().c_str());
        wifiUp = true;
        needServerSetup = true;
        if (ethUp) needNAT = true;
        break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("[WiFi] Disconnected — reconnecting...");
        wifiUp = false;
        WiFi.reconnect();
        break;

    case ARDUINO_EVENT_WIFI_AP_START:
        Serial.printf("[AP] HaphazardNet up — %s\n",
                      WiFi.softAPIP().toString().c_str());
        break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        Serial.println("[AP] Client connected");
        break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        Serial.println("[AP] Client disconnected");
        break;

    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    loadConfig();

    Serial.println();
    Serial.println("╔══════════════════════════════╗");
    Serial.println("║    HaphazardBridge  v1.6     ║");
    Serial.printf( "║  Mode: %-22s║\n", modeName(currentMode));
    Serial.println("╚══════════════════════════════╝");
    Serial.printf("[Config] TAK %s:%s\n", cotIpStr, cotPortStr);

    pinMode(CFG_BTN, INPUT_PULLUP);

    // ── Startup button window (3s) ────────────────────────────────────────────
    // Tap  BOOT → cycle mode (Mobility ↔ Dismounted)
    // Hold BOOT → clear all config and reset to defaults
    Serial.printf("[Mode] %s — tap BOOT within 3s to switch\n", modeName(currentMode));
    {
        uint32_t window = millis();
        while (millis() - window < 3000) {
            if (digitalRead(CFG_BTN) == LOW) {
                uint32_t pressStart = millis();
                while (digitalRead(CFG_BTN) == LOW) delay(10);
                uint32_t heldMs = millis() - pressStart;

                if (heldMs < 1500) {
                    // Tap — cycle mode
                    BridgeMode next = (currentMode == MODE_MOBILITY)
                                      ? MODE_DISMOUNTED : MODE_MOBILITY;
                    saveMode(next);
                    currentMode = next;
                    Serial.printf("[Mode] → %s\n", modeName(currentMode));
                } else {
                    // Hold — clear all config
                    Serial.println("[Config] Clearing all config — resetting to defaults");
                    wm.resetSettings();
                    Preferences prefs;
                    prefs.begin("bridge", false);
                    prefs.clear();
                    prefs.end();
                    strncpy(cotIpStr,   "192.168.10.123", sizeof(cotIpStr));
                    strncpy(cotPortStr, "8000",           sizeof(cotPortStr));
                    cotIp.fromString(cotIpStr);
                    cotPortNum  = 8000;
                    currentMode = MODE_MOBILITY;
                }
                break;
            }
            delay(10);
        }
    }

    Network.onEvent(onNetEvent);

    ETH.begin(ETH_PHY_W5500, 1, W5500_CS, W5500_IRQ, W5500_RST,
              SPI3_HOST, W5500_SCK, W5500_MISO, W5500_MOSI);

    // ── Portal custom fields ──────────────────────────────────────────────────
    static WiFiManagerParameter p_cot_ip("cotip", "TAK Server IP",
                                          cotIpStr, 20);
    static WiFiManagerParameter p_cot_port("cotport", "TAK CoT/TCP Port",
                                            cotPortStr, 6);
    wm.addParameter(&p_cot_ip);
    wm.addParameter(&p_cot_port);
    wm.setSaveParamsCallback([&]() {
        saveConfig(p_cot_ip.getValue(), p_cot_port.getValue());
    });
    wm.setHostname("HaphazardBridge");
    wm.setConfigPortalTimeout(300);
    wm.setConnectTimeout(30);
    wm.setMinimumSignalQuality(10);

    // ── Mode-specific WiFi setup ──────────────────────────────────────────────
    if (currentMode == MODE_DISMOUNTED) {
        Serial.println("[WiFi] Dismounted — starting HaphazardNet AP only");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        // No STA — servers start immediately
        wifiUp          = true;
        needServerSetup = true;
        if (ethUp) needNAT = true;

    } else {
        // Mobility Mode — AP + STA
        Serial.println("[WiFi] Mobility — connecting to upstream AP + HaphazardNet");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID, AP_PASS);
        WiFi.begin("HaphazardTAK", "H@phazard!");
        // Non-blocking so loop() always runs (BOOT button works during portal)
        wm.setConfigPortalBlocking(false);
        wm.autoConnect("HaphazardBridge-Setup");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    if (needNAT) {
        needNAT = false;
        enableNAT();
    }

    if (needServerSetup) {
        needServerSetup = false;
        proxyServer.begin();
        cotServer.begin();
        grpcFwdServer.begin();

        IPAddress apIp = (currentMode == MODE_DISMOUNTED)
                         ? WiFi.softAPIP()
                         : WiFi.localIP();

        Serial.printf("[Proxy] SDR UI   → %s:%d\n",     apIp.toString().c_str(), PROXY_PORT);
        Serial.printf("[CoT]   Relay    → port %d\n",   COT_LISTEN_PORT);
        Serial.printf("[gRPC]  Forward  → %s:%d → SDR\n", apIp.toString().c_str(), GRPC_FWD_PORT);

        if (currentMode == MODE_DISMOUNTED) {
            cotUdp.begin(COT_UDP_PORT);
            udpRelayActive = true;
            enableApNAT();  // ATAK → SDR on any port via NAT
            Serial.printf("[CoT UDP] Relay  → ETH:%d → AP broadcast\n", COT_UDP_PORT);
            Serial.println("[Config] SDR CoT UDP target: 192.168.99.1:4242");
            Serial.println("[Config] ATAK plugin socket: 192.168.4.1:8000");
            Serial.println("[Config] SDR direct access:  192.168.99.234");
        } else {
            Serial.printf("[Config] ATAK plugin socket: %s:8000\n",
                          WiFi.localIP().toString().c_str());
            Serial.println("[Config] Or via HaphazardNet: 192.168.4.1:8000");
        }

        if (!otaReady) setupOTA();
    }

    if (otaReady) ArduinoOTA.handle();
    if (currentMode == MODE_MOBILITY) wm.process();

    if (wifiUp) {
        handleProxy();
        handleCoT();
        handleGrpc();
        if (udpRelayActive) handleCotUdpRelay();
    }

    // ── BOOT button: hold 3s in operation → open config portal ───────────────
    static uint32_t holdStart = 0;
    static bool     holdActed = false;
    if (digitalRead(CFG_BTN) == LOW) {
        if (!holdStart) { holdStart = millis(); holdActed = false; }
        if (!holdActed && millis() - holdStart > 3000) {
            holdActed = true;
            Serial.println("[Config] Opening config portal...");
            wm.startConfigPortal("HaphazardBridge-Setup");
        }
    } else {
        holdStart = 0;
        holdActed = false;
    }
}
