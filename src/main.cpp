#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
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

// ── Config button (BOOT = GPIO0) ──────────────────────────────────────────────
#define CFG_BTN 0

// ─────────────────────────────────────────────────────────────────────────────
WiFiManager wm;
NetworkServer proxyServer(PROXY_PORT);
NetworkServer cotServer(COT_LISTEN_PORT);
NetworkServer grpcFwdServer(GRPC_FWD_PORT);

static bool ethUp           = false;
static bool wifiUp          = false;
static bool otaReady        = false;
static bool needServerSetup = false;
static bool needNAT         = false;

// TAK server — saved in NVS, editable via captive portal
static char cotIpStr[20]  = "192.168.10.123";
static char cotPortStr[6] = "8000";
static IPAddress cotIp(192, 168, 10, 123);
static uint16_t  cotPortNum = 8000;

// Non-blocking relay state
static NetworkClient proxyBrowser, proxySdr;
static uint32_t      proxyIdle;
static NetworkClient cotClient, cotTak;
static uint32_t      cotIdle;
static NetworkClient grpcAtak, grpcSdr;
static uint32_t      grpcIdle;

// ─────────────────────────────────────────────────────────────────────────────
void loadConfig() {
    Preferences prefs;
    prefs.begin("bridge", true);
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

static void napt_enable_cb(void *) {
    ip_napt_enable(ipaddr_addr(ETH_IP), 1);
    Serial.println("[Bridge] NAT active  ETH(192.168.99.x) → WiFi");
}

void enableNAT() {
    // ip_napt_enable touches lwIP timers — must run on the lwIP core task
    tcpip_callback(napt_enable_cb, nullptr);
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

// SDR web UI proxy — one iteration per loop() call (non-blocking)
void handleProxy() {
    if (proxyBrowser && proxySdr) {
        if (!proxyBrowser.connected() || !proxySdr.connected() ||
            millis() - proxyIdle > 8000) {
            proxyBrowser.stop(); proxySdr.stop();
            Serial.println("[Proxy] Closed");
        } else {
            if (relay(proxyBrowser, proxySdr)) proxyIdle = millis();
        }
        return;
    }
    NetworkClient client = proxyServer.accept();
    if (!client) return;
    NetworkClient sdr;
    if (!sdr.connect(IPAddress(192,168,99,234), SDR_HTTP_PORT)) {
        Serial.println("[Proxy] SDR unreachable");
        client.stop(); return;
    }
    proxyBrowser = client;
    proxySdr     = sdr;
    proxyIdle    = millis();
    Serial.println("[Proxy] Connected");
}

// CoT TCP relay — one iteration per loop() call (non-blocking)
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

// ATAK plugin connects here to pull SDR's gRPC stream — data flows SDR→ATAK only
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
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        wifiUp = true;
        needServerSetup = true;
        if (ethUp) needNAT = true;
        break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("[WiFi] Disconnected — reconnecting...");
        wifiUp = false;
        WiFi.reconnect();
        break;

    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n╔══════════════════════════╗");
    Serial.println("║    HaphazardBridge v1.5  ║");
    Serial.println("╚══════════════════════════╝");

    loadConfig();
    // Force correct TAK target — overrides any stale NVS value
    saveConfig("192.168.10.123", "8000");
    Serial.printf("[Config] TAK %s:%s\n", cotIpStr, cotPortStr);

    pinMode(CFG_BTN, INPUT_PULLUP);
    if (digitalRead(CFG_BTN) == LOW) {
        Serial.println("[Config] Boot button held — clearing WiFi + TAK config");
        wm.resetSettings();
        Preferences prefs;
        prefs.begin("bridge", false);
        prefs.clear();
        prefs.end();
        // reset runtime values to defaults
        strncpy(cotIpStr,   "192.168.10.123", sizeof(cotIpStr));
        strncpy(cotPortStr, "8000",           sizeof(cotPortStr));
        cotIp.fromString(cotIpStr);
        cotPortNum = 8000;
    }

    Network.onEvent(onNetEvent);

    ETH.begin(ETH_PHY_W5500, 1, W5500_CS, W5500_IRQ, W5500_RST,
              SPI3_HOST, W5500_SCK, W5500_MISO, W5500_MOSI);

    // Portal custom fields — static so they outlive setup()
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

    // Pre-load credentials into NVS so autoConnect finds them without portal
    WiFi.begin("HaphazardTAK", "H@phazard!");

    Serial.println("[WiFi] Connecting... (portal if unconfigured)");
    if (!wm.autoConnect("HaphazardBridge-Setup")) {
        Serial.println("[WiFi] Connection failed — restarting in 5s");
        delay(5000);
        ESP.restart();
    }
}

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
        Serial.printf("[Proxy] SDR UI  → port %d\n", PROXY_PORT);
        Serial.printf("[CoT]   Relay   → port %d → %s:%d\n",
                      COT_LISTEN_PORT, cotIpStr, cotPortNum);
        Serial.printf("[gRPC]  Forward → port %d → 192.168.99.234:%d\n",
                      GRPC_FWD_PORT, SDR_GRPC_PORT);
        if (!otaReady) setupOTA();
    }
    if (otaReady) ArduinoOTA.handle();
    if (wifiUp) {
        handleProxy();
        handleCoT();
        handleGrpc();
    }

    static uint32_t btnDown = 0;
    if (digitalRead(CFG_BTN) == LOW) {
        if (!btnDown) btnDown = millis();
        if (millis() - btnDown > 3000) {
            Serial.println("[Config] Re-opening WiFi portal...");
            wm.startConfigPortal("HaphazardBridge-Setup");
            btnDown = 0;
        }
    } else {
        btnDown = 0;
    }
}
