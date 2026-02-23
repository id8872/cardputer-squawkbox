/*
 * =======================================================================================
 * SQUAWK BOX v9.9 - CARDPUTER ADV EDITION
 * =======================================================================================
 * ORIGINAL PLATFORM: Particle Photon 2
 * PORTED TO: M5Stack Cardputer (ESP32-S3, ST7789 135x240 TFT, built-in keyboard/speaker)
 *
 * FIRST TIME SETUP — NO HARDCODED CREDENTIALS NEEDED:
 * 1. Flash this sketch as-is.
 * 2. On first boot, the Cardputer creates a WiFi hotspot: SQUAWKBOX-SETUP
 * 3. Connect your phone/laptop to that network (no password).
 * 4. A setup page opens automatically (or visit 192.168.4.1).
 * 5. Enter your WiFi SSID, password, and Finnhub API key → Save.
 * 6. Device reboots and connects to your network.
 * 7. Press [W] at any time to re-enter setup mode and change credentials.
 *
 * KEYBOARD MAP:
 * [I]  = Toggle System Info Screen (Shows IP address)
 * [M]  = Toggle Mute
 * [B]  = Toggle Backlight
 * [1]  = Switch to SPY
 * [2]  = Switch to QQQ
 * [3]  = Switch to IWM
 * [+]  = Increase Chop Limit by 0.001
 * [-]  = Decrease Chop Limit by 0.001
 * [T]  = Test Bull tone
 * [Y]  = Test Bear tone
 * [W]  = WiFi setup portal (change credentials)
 *
 * REQUIRED LIBRARIES (install via Arduino Library Manager):
 * - M5Cardputer  (M5Stack official)
 * - M5GFX        (M5Stack official)
 * - ArduinoJson  (Benoit Blanchon, v7+)
 * - Preferences  (built-in ESP32 core)
 *
 * =======================================================================================
 */

#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiAP.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <cmath>

// NTP / Timezone (Eastern = UTC-5, DST handled via POSIX string)
#define NTP_SERVER      "pool.ntp.org"
#define TZ_INFO         "EST5EDT,M3.2.0,M11.1.0"   // US Eastern with auto-DST

// =======================================================================================
// DISPLAY DIMENSIONS & COLOURS — defined early so portal functions can use them
// =======================================================================================
#define DISP_W  240
#define DISP_H  135

// Colours (RGB565) — verified dark theme
#define C_BG       0x0000  // True black background
#define C_CARD     0x1082  // ~#101010 dark card
#define C_GREEN    0x07E0  // Pure green  #00FF00
#define C_GREEN2   0x0752  // Softer green #00E888 for accents
#define C_RED      0xF800  // Pure red
#define C_YELLOW   0xFFE0  // Gold #FFD700
#define C_CYAN     0x07FF  // Cyan
#define C_WHITE    0xFFFF
#define C_GREY     0x7BEF  // Mid grey
#define C_DKGREY   0x2945  // Dark grey for dividers
#define C_BLUE     0x001F  // Deep blue accent

// =======================================================================================
// WIFI CREDENTIALS (stored in NVS, never hardcoded)
// Credentials live in their own NVS namespace "wificreds", separate from trading
// settings ("squawk"), so they survive a settings version bump without being wiped.
// =======================================================================================
struct Credentials {
    char ssid[64];
    char pass[64];
    char apikey[64]; // Finnhub API key — free tier supports ~60 calls/min
};
Credentials creds;

// Portal objects — only used when in setup mode, idle otherwise
DNSServer     dnsServer;
WiFiServer    portalServer(80);
bool          inPortalMode = false;

// Load from NVS — empty strings if not yet saved (first boot)
void loadCredentials() {
    Preferences p;
    p.begin("wificreds", true);  // true = read-only
    p.getString("ssid",   creds.ssid,   sizeof(creds.ssid));
    p.getString("pass",   creds.pass,   sizeof(creds.pass));
    p.getString("apikey", creds.apikey, sizeof(creds.apikey));
    p.end();
}

// Persist to NVS — survives power loss and OTA flashes
void saveCredentials() {
    Preferences p;
    p.begin("wificreds", false); // false = read-write
    p.putString("ssid",   creds.ssid);
    p.putString("pass",   creds.pass);
    p.putString("apikey", creds.apikey);
    p.end();
}

// WiFi password is optional (open networks exist), but SSID and API key are required
bool hasCredentials() {
    return strlen(creds.ssid) > 0 && strlen(creds.apikey) > 0;
}

// Draw setup portal screen on the Cardputer display
void drawPortalScreen() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(C_YELLOW);
    M5Cardputer.Display.setCursor(4, 8);
    M5Cardputer.Display.print("SETUP MODE");

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(C_WHITE);
    M5Cardputer.Display.setCursor(4, 35);
    M5Cardputer.Display.print("1. Connect to WiFi network:");
    M5Cardputer.Display.setTextColor(C_GREEN);
    M5Cardputer.Display.setCursor(4, 47);
    M5Cardputer.Display.print("   SQUAWKBOX-SETUP");

    M5Cardputer.Display.setTextColor(C_WHITE);
    M5Cardputer.Display.setCursor(4, 62);
    M5Cardputer.Display.print("2. Open browser and visit:");
    M5Cardputer.Display.setTextColor(C_CYAN);
    M5Cardputer.Display.setCursor(4, 74);
    M5Cardputer.Display.print("   192.168.4.1");

    M5Cardputer.Display.setTextColor(C_WHITE);
    M5Cardputer.Display.setCursor(4, 89);
    M5Cardputer.Display.print("3. Enter WiFi & API key");
    M5Cardputer.Display.setCursor(4, 101);
    M5Cardputer.Display.print("   then tap Save.");

    M5Cardputer.Display.setTextColor(C_DKGREY);
    M5Cardputer.Display.setCursor(4, 120);
    M5Cardputer.Display.print("Waiting for connection...");
}

// Serve the setup form HTML
void servePortalHTML(WiFiClient& client, bool saved = false) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.print(
        "<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{font-family:'Segoe UI',sans-serif;background:#121212;color:#eee;"
        "margin:0;padding:20px;max-width:480px;margin:0 auto}"
        "h1{color:#00ff88;font-size:22px;margin-bottom:4px}"
        "p{color:#aaa;font-size:13px;margin:4px 0 20px 0}"
        "label{display:block;color:#aaa;font-size:12px;margin:12px 0 4px 0}"
        "input{width:100%;padding:12px;background:#1e1e1e;border:1px solid #444;"
        "color:#fff;border-radius:6px;font-size:15px;box-sizing:border-box}"
        "button{width:100%;padding:14px;margin-top:20px;background:#00ff88;"
        "color:#000;border:none;border-radius:6px;font-size:16px;"
        "font-weight:bold;cursor:pointer}"
        ".saved{background:#1e1e1e;border:1px solid #00ff88;color:#00ff88;"
        "padding:12px;border-radius:6px;text-align:center;margin-bottom:16px}"
        "</style></head><body>"
        "<h1>SQUAWK BOX</h1>"
        "<p>Enter your WiFi credentials and Finnhub API key.<br>"
        "Get a free API key at <b>finnhub.io</b></p>"
    );
    if (saved) {
        client.print("<div class='saved'>✓ Saved! Rebooting in 3 seconds...</div>");
    }
    client.print(
        "<form method='GET' action='/save'>"
        "<label>WiFi Network Name (SSID)</label>"
        "<input name='s' type='text' placeholder='YourNetworkName' autocomplete='off'>"
        "<label>WiFi Password</label>"
        "<input name='p' type='password' placeholder='YourPassword'>"
        "<label>Finnhub API Key</label>"
        "<input name='k' type='text' placeholder='d1abc123xyz...' autocomplete='off'>"
        "<button type='submit'>SAVE &amp; CONNECT</button>"
        "</form></body></html>"
    );
}

// Parse a single URL-encoded field value from a GET request string
String urlDecode(String s) {
    String out = "";
    for (int i = 0; i < (int)s.length(); i++) {
        if (s[i] == '+') { out += ' '; }
        else if (s[i] == '%' && i + 2 < (int)s.length()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else { out += s[i]; }
    }
    return out;
}

String extractParam(const String& req, const String& key) {
    String search = key + "=";
    int idx = req.indexOf(search);
    if (idx < 0) return "";
    idx += search.length();
    int end = req.indexOf('&', idx);
    if (end < 0) end = req.indexOf(' ', idx);
    if (end < 0) end = req.length();
    return urlDecode(req.substring(idx, end));
}

// Full captive portal loop — blocks until credentials are saved, then reboots.
// Called on first boot (no credentials) or when user presses [W].
// The DNS server redirects ALL domains to 192.168.4.1 so phones that try to
// auto-detect a captive portal (e.g. iOS, Android) get sent straight to the form.
void runPortal() {
    inPortalMode = true;

    // Broadcast our own AP — no password so any device can connect
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SQUAWKBOX-SETUP");
    delay(500);  // Give the AP time to stabilise before accepting DNS queries

    // Wildcard DNS: any domain → 192.168.4.1 (the portal IP)
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    portalServer.begin();

    drawPortalScreen();
    Serial.println("[Portal] AP started: SQUAWKBOX-SETUP at 192.168.4.1");

    while (true) {
        dnsServer.processNextRequest();
        M5Cardputer.update();
        WiFiClient client = portalServer.accept();
        if (client) {
            unsigned long t = millis();
            while (!client.available() && millis() - t < 1000) delay(1);
            if (client.available()) {
                String req = client.readStringUntil('\n');
                while (client.available()) client.read();

                Serial.println("[Portal] Request: " + req);

                if (req.indexOf("GET /save") != -1) {
                    // Extract and save credentials
                    String s = extractParam(req, "s");
                    String p = extractParam(req, "p");
                    String k = extractParam(req, "k");

                    if (s.length() > 0 && k.length() > 0) {
                        s.toCharArray(creds.ssid,   sizeof(creds.ssid));
                        p.toCharArray(creds.pass,   sizeof(creds.pass));
                        k.toCharArray(creds.apikey, sizeof(creds.apikey));
                        saveCredentials();

                        servePortalHTML(client, true);
                        client.clear();
                        client.stop();
                        
                        // Show saved confirmation on screen
                        M5Cardputer.Display.fillRect(0, 110, DISP_W, 25, TFT_BLACK);
                        M5Cardputer.Display.setTextColor(C_GREEN);
                        M5Cardputer.Display.setTextSize(1);
                        M5Cardputer.Display.setCursor(4, 118);
                        M5Cardputer.Display.print("Saved! Rebooting...");
                        M5Cardputer.Speaker.tone(1200, 200);

                        delay(3000);
                        ESP.restart();
                    } else {
                        // Missing fields — re-show form
                        servePortalHTML(client, false);
                    }
                } else {
                    // All other requests → show the form (captive portal catch-all)
                    servePortalHTML(client, false);
                }
                client.clear();
                client.stop();
            }
        }
        delay(10);
    }
}

// =======================================================================================
// STRATEGY PARAMETERS (percentage-based, same as original)
// alphaFast / alphaSlow: EMA smoothing factors. Higher = more reactive to price changes.
//   SPY moves slowly and tightly, so uses lower alphas.
//   IWM (small caps) is more volatile, so uses higher alphas to track it properly.
// chopLimit: minimum velocity % to be considered a directional move vs. noise.
//   Tuned per-symbol based on typical daily volatility.
// =======================================================================================
const float P_SPY_FAST = 0.22f; const float P_SPY_SLOW = 0.10f; const float P_SPY_CHOP = 0.010f;
const float P_QQQ_FAST = 0.25f; const float P_QQQ_SLOW = 0.12f; const float P_QQQ_CHOP = 0.014f;
const float P_IWM_FAST = 0.28f; const float P_IWM_SLOW = 0.14f; const float P_IWM_CHOP = 0.035f;

// Confluence thresholds:
// BULL_RUSH: the velocity gain required after a BULL BREAK to confirm a BUY signal.
// BEAR_DUMP: the velocity drop required after a BEAR BREAK to confirm a SELL signal.
// CONFIRMATION_WINDOW: how long (ms) we wait for the confirmation after a BREAK.
//   If confirmation doesn't arrive within 15s, the pending signal is discarded.
const unsigned long CONFIRMATION_WINDOW = 15000UL;

// =======================================================================================
// PERSISTENT CONFIG (NVS via Preferences)
// =======================================================================================
struct Config {
    float alphaFast;
    float alphaSlow;
    float chopLimit;
    bool  isMuted;
    bool  backlightOn;
    uint8_t volume;
    char  symbol[8];
};
Config settings;
Preferences prefs;

// =======================================================================================
// SYSTEM STATE
// =======================================================================================
// EMAs and velocity are volatile because they're written in the main loop and
// read in display/web functions — volatile prevents the compiler from caching stale values.
volatile float emaFast = 0, emaSlow = 0, diff = 0;
volatile float lastPrice = 0;

bool initialized    = false;   // false until first valid price is received
bool forceRedraw    = true;    // set true to trigger a full screen repaint next loop
bool showInfoScreen = false;   // tracks if the System Info screen is active

unsigned long lastPollMs   = 0; // tracks when we last hit Finnhub
unsigned long lastWebMs    = 0; // reserved for future web rate-limiting

// --- SIGNAL STATE MACHINE ---
// IDLE     → waiting for a BREAK event
// TRIGGERED→ BREAK seen, waiting for RUSH/DUMP confirmation within window
// (CONFIRMED is reserved but currently unused — signals fire immediately on confirmation)
enum SignalState { IDLE, TRIGGERED, CONFIRMED };
SignalState currentSignal  = IDLE;
unsigned long lastTriggerTime = 0; // millis() when the last BREAK was detected

// --- PAPER TRADING ---
// All PnL is in raw price-per-share (no share size), matching the original Photon version.
// Positive closedPnL = net winner. tradeCount only increments on close, not open.
enum PositionState { POS_NONE, POS_LONG, POS_SHORT };
PositionState currentPos   = POS_NONE;
float tradeEntryPrice      = 0.0f;
float closedPnL            = 0.0f;
int   tradeCount           = 0;

// --- ALERT LOG ---
// Ring buffer of the 5 most recent events — shown on web console and Serial
struct Alert {
    char time[9];   // "HH:MM:SS\0"
    char type[16];  // e.g. "BULL BREAK", "BUY SIGNAL"
    float val;      // velocity value at time of event
};
Alert alertLog[5];

// --- GRAPH / HISTORY BUFFER ---
// Ring buffer of velocity readings for the on-device bar chart and web graph.
// 60 samples × 4px/bar = 240px — exactly fills the display width.
const int HISTORY_SIZE = 60;
volatile float velocityHistory[HISTORY_SIZE];
volatile int   historyHead = 0; // index of the NEXT write position (oldest data)

// --- AUDIO STATE MACHINE ---
// Non-blocking tone sequencer. bzState is set by signal events;
// updateBuzzer() fires the appropriate tone pattern each loop() tick.
enum BuzzerState { BZ_IDLE, BZ_BULL, BZ_BEAR, BZ_STUTTER };
BuzzerState bzState  = BZ_IDLE;
unsigned long bzTimer = 0; // millis() when current tone sequence started

const int DUR_BULLISH = 200;  // ms — short sharp beep for bull events
const int DUR_BEARISH = 1000; // ms — longer low tone for bear events

// --- WEB SERVER ---
WiFiServer webServer(80);

// --- SCREEN LAYOUT (zones, top to bottom in landscape 240x135) ---
// Row 0: Header bar          y=0..24
// Row 1: Price + Velocity    y=25..59
// Row 2: Status + PnL        y=60..89
// Row 3: Mini graph          y=90..119
// Row 4: Footer / IP         y=120..134

// =======================================================================================
// FORWARD DECLARATIONS
// =======================================================================================
void loadCredentials();
void saveCredentials();
bool hasCredentials();
void drawPortalScreen();
void servePortalHTML(WiFiClient& client, bool saved);
void runPortal();
String urlDecode(String s);
String extractParam(const String& req, const String& key);
void loadSettings();
void saveSettings();
void applySymbolPreset(const char* sym);
void resetTrades();
void closePosition();
void openLong();
void openShort();
void fetchQuote();
void handleWebTraffic();
void handleKeyboard();
void updateSignalLogic(float momentum, const char* alertType);
void triggerBuySignal();
void triggerSellSignal();
void logEvent(const char* type, float v);
void drawFullScreen();
void drawInfoScreen();
void drawVelocityZone();
void drawGraph();
void drawPriceBar();
void drawSignalAlert(const char* bigLabel, const char* subLabel, uint16_t bgCol);
void drawSignalOverlay(const char* line1, const char* line2, uint16_t colour);
void updateBuzzer();
unsigned long getSmartInterval();
String getTimeString();
String getLocalIPString();
void serveJSON(WiFiClient& client);
void serveHTML(WiFiClient& client);

// =======================================================================================
// PAPER TRADING ENGINE
// Tracks a single simulated position at a time. No share size — PnL is raw price delta.
// Flipping direction (LONG → SHORT or vice versa) automatically closes the prior trade first.
// =======================================================================================
void closePosition() {
    if (currentPos == POS_NONE) return;
    float pnl = 0.0f;
    if (currentPos == POS_LONG)  pnl = lastPrice - tradeEntryPrice; // profit if price rose
    if (currentPos == POS_SHORT) pnl = tradeEntryPrice - lastPrice; // profit if price fell
    closedPnL += pnl;
    tradeCount++;
    currentPos     = POS_NONE;
    tradeEntryPrice = 0.0f;
}

void openLong() {
    if (currentPos == POS_SHORT) closePosition(); // auto-close short before going long
    if (currentPos == POS_NONE)  { currentPos = POS_LONG; tradeEntryPrice = lastPrice; }
    // If already LONG, do nothing — don't double-enter
}

void openShort() {
    if (currentPos == POS_LONG)  closePosition(); // auto-close long before going short
    if (currentPos == POS_NONE)  { currentPos = POS_SHORT; tradeEntryPrice = lastPrice; }
    // If already SHORT, do nothing
}

// Called when switching symbols — wipes trades so PnL reflects the new ticker only
void resetTrades() {
    currentPos      = POS_NONE;
    tradeEntryPrice = 0.0f;
    closedPnL       = 0.0f;
    tradeCount      = 0;
}

// =======================================================================================
// SETTINGS (NVS via Preferences)
// Version byte (ver=19) acts as a schema guard. If the stored version doesn't match,
// we're either on a fresh chip or after a breaking settings change — defaults are written.
// Increment the version number any time you add/remove/rename a settings key.
// =======================================================================================
void loadSettings() {
    prefs.begin("squawk", false);
    uint8_t ver = prefs.getUChar("ver", 0);
    if (ver != 20) { // <--- Bumped to 20 for new volume schema
        // First boot or schema change — initialise with sensible defaults
        strcpy(settings.symbol, "SPY");
        applySymbolPreset("SPY");
        settings.isMuted     = false;
        settings.backlightOn = true;
        settings.volume      = 128; // <--- Default 50% volume
        saveSettings();
        prefs.putUChar("ver", 20);
    } else {
        settings.alphaFast   = prefs.getFloat("aFast",  P_SPY_FAST);
        settings.alphaSlow   = prefs.getFloat("aSlow",  P_SPY_SLOW);
        settings.chopLimit   = prefs.getFloat("chop",   P_SPY_CHOP);
        settings.isMuted     = prefs.getBool ("muted",  false);
        settings.backlightOn = prefs.getBool ("bl",     true);
        settings.volume      = prefs.getUChar("vol",    128); // Load volume
        prefs.getString("sym", settings.symbol, sizeof(settings.symbol));
        if (strlen(settings.symbol) == 0) strcpy(settings.symbol, "SPY");
    }
    prefs.end();
}

void saveSettings() {
    prefs.begin("squawk", false);
    prefs.putFloat("aFast", settings.alphaFast);
    prefs.putFloat("aSlow", settings.alphaSlow);
    prefs.putFloat("chop",  settings.chopLimit);
    prefs.putBool ("muted", settings.isMuted);
    prefs.putBool ("bl",    settings.backlightOn);
    prefs.putUChar("vol",   settings.volume); // Save volume
    prefs.putString("sym",  settings.symbol);
    prefs.putUChar("ver",   20);
    prefs.end();
}

void applySymbolPreset(const char* sym) {
    if      (strcmp(sym, "SPY") == 0) { settings.alphaFast = P_SPY_FAST; settings.alphaSlow = P_SPY_SLOW; settings.chopLimit = P_SPY_CHOP; }
    else if (strcmp(sym, "QQQ") == 0) { settings.alphaFast = P_QQQ_FAST; settings.alphaSlow = P_QQQ_SLOW; settings.chopLimit = P_QQQ_CHOP; }
    else if (strcmp(sym, "IWM") == 0) { settings.alphaFast = P_IWM_FAST; settings.alphaSlow = P_IWM_SLOW; settings.chopLimit = P_IWM_CHOP; }
}

// =======================================================================================
// ALERT LOG
// =======================================================================================
void logEvent(const char* type, float v) {
    for (int i = 4; i > 0; i--) alertLog[i] = alertLog[i-1];
    String ts = getTimeString();
    strncpy(alertLog[0].time, ts.c_str(), sizeof(alertLog[0].time) - 1);
    alertLog[0].time[sizeof(alertLog[0].time) - 1] = '\0';
    strncpy(alertLog[0].type, type, sizeof(alertLog[0].type) - 1);
    alertLog[0].type[sizeof(alertLog[0].type) - 1] = '\0';
    alertLog[0].val = v;
}

// =======================================================================================
// TIME HELPERS
// =======================================================================================
String getTimeString() {
    struct tm ti;
    if (!getLocalTime(&ti)) return "--:--:--";
    char buf[9];
    strftime(buf, sizeof(buf), "%T", &ti);
    return String(buf);
}

int getHour() {
    struct tm ti;
    if (!getLocalTime(&ti)) return 12;
    return ti.tm_hour;
}
int getMinute() {
    struct tm ti;
    if (!getLocalTime(&ti)) return 0;
    return ti.tm_min;
}
int getWeekday() {  // 0=Sun
    struct tm ti;
    if (!getLocalTime(&ti)) return 1;
    return ti.tm_wday;
}

String getLocalIPString() {
    return WiFi.localIP().toString();
}

// =======================================================================================
// SMART POLLING INTERVAL (seconds)
// Adapts poll rate to market conditions to conserve Finnhub API calls (60/min free tier)
// while maximising responsiveness during the periods that matter most.
//   Weekend     → 300s  (5 min) — market closed, no need to hammer the API
//   Off-hours   → 60s   (1 min) — pre/post market, low activity
//   Open/Close  → 2s    — 9:30–10:00 and 15:00–16:00 are highest-volatility windows
//   Lunch       → 10s   — 12:00 is chop territory, slower polling saves API calls
//   Normal      → 4s    — mid-session baseline
// =======================================================================================
unsigned long getSmartInterval() {
    int wd = getWeekday();
    if (wd == 0 || wd == 6) return 300;                         // Saturday(0) or Sunday(6)
    
    int h = getHour(); int m = getMinute();
    if (h < 8 || (h == 8 && m < 30) || h >= 16) return 60;      // outside market hours
    if ((h == 9 && m >= 30) || h == 10 || h == 15) return 2;    // open and close sprints
    if (h == 12) return 10;                                     // lunch chop window
    
    return 4;                                                   // normal mid-session
}

// =======================================================================================
// FINNHUB QUOTE FETCH (Direct HTTPS REST)
// =======================================================================================
void fetchQuote() {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClientSecure secClient;
    secClient.setInsecure(); // Skip cert validation — acceptable for market data

    HTTPClient http;
    String url = "https://finnhub.io/api/v1/quote?symbol=";
    url += settings.symbol;
    url += "&token=";
    url += creds.apikey;

    http.begin(secClient, url);
    http.setTimeout(5000);
    int code = http.GET();
    
    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
            float p = doc["c"].as<float>(); // "c" = current price in Finnhub
            if (p > 0) processPrice(p);
        }
    } else {
        Serial.printf("[Finnhub] HTTP error: %d\n", code);
    }
    http.end();
}

// =======================================================================================
// MARKET ENGINE (EMA + PERCENTAGE VELOCITY)
// Each new price runs through two EMAs with different smoothing factors.
// The gap between them, expressed as a % of price, is "velocity" (diff).
// Positive diff = fast EMA above slow = upward momentum.
// Negative diff = fast EMA below slow = downward momentum.
// Near-zero diff (within chopLimit) = chop zone, no directional bias.
//
// Alert detection compares the current diff to the previous diff:
//   BULL/BEAR BREAK  → velocity crosses the chop threshold (trend beginning)
//   BULL RUSH        → velocity accelerates 20%+ above its prior value (trend strengthening)
//   BEAR DUMP        → velocity drops 20%+ below its prior value (same, downside)
//   TREND END        → velocity falls back inside the chop band (trend ending)
// =======================================================================================
void processPrice(float p) {
    lastPrice = p;

    // First price received — seed both EMAs to current price so diff starts at 0
    // rather than producing a false signal on startup.
    if (!initialized) {
        emaFast = lastPrice;
        emaSlow = lastPrice;
        diff = 0;
        initialized = true;
        forceRedraw = true;
        return;
    }

    float prevDiff = diff; // snapshot before updating, used for BREAK/RUSH detection
    
    emaFast = (lastPrice * settings.alphaFast) + (emaFast * (1.0f - settings.alphaFast));
    emaSlow = (lastPrice * settings.alphaSlow) + (emaSlow * (1.0f - settings.alphaSlow));
    
    // Express the EMA gap as a percentage of price for symbol-agnostic comparison
    float rawDiff = emaFast - emaSlow;
    diff = (rawDiff / lastPrice) * 100.0f;

    // Store in circular buffer for the on-device graph and web chart
    velocityHistory[historyHead] = diff;
    historyHead = (historyHead + 1) % HISTORY_SIZE;

    // --- ALERT DETECTION ---
    if (fabs(diff) > settings.chopLimit) {
        if (diff > 0) {
            // Bullish side
            if (prevDiff <= settings.chopLimit) {
                // Was in chop, now above threshold — trend starting upward
                bzState = BZ_BULL; bzTimer = millis();
                logEvent("BULL BREAK", diff);
                updateSignalLogic(diff, "BULL BREAK");
            } else if (diff > (prevDiff * 1.20f)) {
                // Already bullish and accelerating by 20%+ — momentum surge
                bzState = BZ_STUTTER; bzTimer = millis();
                logEvent("BULL RUSH", diff);
                updateSignalLogic(diff, "BULL RUSH");
            }
        } else {
            // Bearish side
            if (prevDiff >= -settings.chopLimit) {
                // Was in chop, now below threshold — trend starting downward
                bzState = BZ_BEAR; bzTimer = millis();
                logEvent("BEAR BREAK", diff);
                updateSignalLogic(diff, "BEAR BREAK");
            } else if (diff < (prevDiff * 1.20f)) {
                // Already bearish and accelerating — momentum dump
                bzState = BZ_STUTTER; bzTimer = millis();
                logEvent("BEAR DUMP", diff);
                updateSignalLogic(diff, "BEAR DUMP");
            }
        }
    } else if (fabs(prevDiff) > settings.chopLimit) {
        // Was trending, now back inside chop band — trend has ended
        bzState = BZ_BULL; bzTimer = millis();  // short blip on trend end
        logEvent("TREND END", diff);
        updateSignalLogic(diff, "TREND END");
    }

    if (!showInfoScreen) {
        drawVelocityZone();
        drawGraph();
        drawPriceBar();
    }
}

// =======================================================================================
// CONFLUENCE SIGNAL ENGINE
// Two-step confirmation required before a trade fires:
//   Step 1: BULL/BEAR BREAK — velocity crosses the chop threshold. Sets TRIGGERED state
//           and starts a 15-second confirmation window.
//   Step 2: BULL RUSH / BEAR DUMP — acceleration within the window. If momentum hits
//           the rush/dump threshold, a BUY or SELL signal fires.
//
// If the confirmation never arrives (window expires), TRIGGERED resets to IDLE.
// This filters out false breakouts that reverse immediately.
//
// TREND END always closes any open position regardless of the above, and resets the
// signal state so stale TRIGGERED states don't linger across a trend boundary.
// =======================================================================================
void updateSignalLogic(float currentMomentum, const char* alertType) {
    int h = getHour(); int m = getMinute();
    // Lunch chop window: 12:00–1:30 PM EST — historically low-quality signals
    bool isLunchExit = (h == 12) || (h == 13 && m <= 30);

    if (strcmp(alertType, "TREND END") == 0) {
        if (currentPos != POS_NONE) {
            // Always show an exit alert, not just during lunch
            const char* exitSub = isLunchExit ? "LUNCH CHOP ZONE" : "TREND ENDED";
            uint16_t exitCol = isLunchExit ? C_YELLOW : C_CYAN;
            closePosition();
            drawSignalAlert("EXIT", exitSub, exitCol);
            if (!settings.isMuted) {
                M5Cardputer.Speaker.tone(800, 150); delay(180);
                M5Cardputer.Speaker.tone(600, 300);
            }
            bzState = BZ_BEAR; bzTimer = millis();
            forceRedraw = true;
        } else if (isLunchExit) {
            // No open position but we're in the lunch window — advisory alert only
            drawSignalAlert("EXIT", "LUNCH CHOP ZONE", C_YELLOW);
            bzState = BZ_BEAR; bzTimer = millis();
            forceRedraw = true;
        }
        currentSignal = IDLE; // clear any pending TRIGGERED state across a trend end
        return;
    }

    // Step 1: BREAK detected — arm the confirmation window
    if (strcmp(alertType, "BULL BREAK") == 0 || strcmp(alertType, "BEAR BREAK") == 0) {
        currentSignal    = TRIGGERED;
        lastTriggerTime  = millis();
    }

    // Calculate a dynamic window based on current polling rate (2 polls + 2 seconds grace)
    unsigned long dynamicWindow = (getSmartInterval() * 1000UL) * 2 + 2000UL;

    // Step 2: Check for confirmation while window is still open
    if (currentSignal == TRIGGERED && (millis() - lastTriggerTime <= dynamicWindow)) {
        // Trust the 20% acceleration logic from processPrice() rather than hardcoded floats
        if (strcmp(alertType, "BULL RUSH") == 0) {
            triggerBuySignal();
            currentSignal = IDLE;  // disarm so a stray BEAR DUMP can't fire a short immediately after
        } else if (strcmp(alertType, "BEAR DUMP") == 0) {
            triggerSellSignal();
            currentSignal = IDLE;
        }
    }

    // Window expired without confirmation — discard the pending signal
    if (currentSignal == TRIGGERED && (millis() - lastTriggerTime > dynamicWindow)) {
        currentSignal = IDLE;
    }
}

void triggerBuySignal() {
    openLong();
    logEvent("BUY SIGNAL", diff);
    char subBuf[20];
    snprintf(subBuf, sizeof(subBuf), "LONG @ $%.2f", lastPrice);
    
    if (!settings.isMuted) {
        M5Cardputer.Speaker.tone(1200, 150); delay(180);
        M5Cardputer.Speaker.tone(1500, 300);
    }
    
    drawSignalAlert("BUY", subBuf, C_GREEN);
    currentSignal = IDLE;
    forceRedraw = true;
}

void triggerSellSignal() {
    openShort();
    logEvent("SELL SIGNAL", diff);
    char subBuf[20];
    snprintf(subBuf, sizeof(subBuf), "SHORT @ $%.2f", lastPrice);
    
    if (!settings.isMuted) {
        M5Cardputer.Speaker.tone(600, 150); delay(180);
        M5Cardputer.Speaker.tone(400, 600);
    }
    
    drawSignalAlert("SELL", subBuf, C_RED);
    currentSignal = IDLE;
    forceRedraw = true;
}

// =======================================================================================
// AUDIO STATE MACHINE
// Non-blocking tone sequencer — called every loop() tick.
// bzState is set by signal events; bzTimer records when the sequence started.
// Using elapsed time (millis - bzTimer) avoids blocking the main loop with delay().
//
// BZ_BULL    → single short high tone (1000Hz, 200ms)
// BZ_BEAR    → single long low tone  (500Hz, 1000ms)
// BZ_STUTTER → three rapid mid-pitch beeps (used for RUSH/DUMP momentum surges)
// All tones respect the isMuted flag — checked once at entry.
// =======================================================================================
void updateBuzzer() {
    if (settings.isMuted || bzState == BZ_IDLE) return;
    
    unsigned long el = millis() - bzTimer;  // elapsed ms since tone sequence started

    switch (bzState) {
        case BZ_BULL:
            if (el == 0 || el < 5) M5Cardputer.Speaker.tone(1000, DUR_BULLISH);
            if (el >= (unsigned long)DUR_BULLISH) bzState = BZ_IDLE;
            break;
            
        case BZ_BEAR:
            if (el == 0 || el < 5) M5Cardputer.Speaker.tone(500, DUR_BEARISH);
            if (el >= (unsigned long)DUR_BEARISH) bzState = BZ_IDLE;
            break;
            
        case BZ_STUTTER:
            // Three 80ms beeps with 80ms gaps between them — total ~400ms
            if (el < 80)        M5Cardputer.Speaker.tone(1100, 80);
            else if (el < 160)  M5Cardputer.Speaker.stop();
            else if (el < 240)  M5Cardputer.Speaker.tone(1100, 80);
            else if (el < 320)  M5Cardputer.Speaker.stop();
            else if (el < 400)  M5Cardputer.Speaker.tone(1100, 80);
            else                bzState = BZ_IDLE;
            break;
            
        default: break;
    }
}

// =======================================================================================
// KEYBOARD HANDLER (M5Cardputer built-in keyboard)
// =======================================================================================
void handleKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange()) return;
    if (!M5Cardputer.Keyboard.isPressed()) return;

    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    for (auto key : status.word) {
        Serial.printf("[KEY] %c\n", key);
        
        switch (key) {
            case 'i': case 'I':
                showInfoScreen = !showInfoScreen;
                forceRedraw = true;
                break;
                
            case 'm': case 'M':
                settings.isMuted = !settings.isMuted;
                saveSettings();
                forceRedraw = true;
                break;

            case 'b': case 'B':
                settings.backlightOn = !settings.backlightOn;
                M5Cardputer.Display.setBrightness(settings.backlightOn ? 128 : 0);
                saveSettings();
                break;

            case '1':
                strcpy(settings.symbol, "SPY");
                applySymbolPreset("SPY");
                initialized = false; resetTrades(); saveSettings();
                forceRedraw = true;
                break;
                
            case '2':
                strcpy(settings.symbol, "QQQ");
                applySymbolPreset("QQQ");
                initialized = false; resetTrades(); saveSettings();
                forceRedraw = true;
                break;

            case '3':
                strcpy(settings.symbol, "IWM");
                applySymbolPreset("IWM");
                initialized = false; resetTrades(); saveSettings();
                forceRedraw = true;
                break;
                
            case '+': case '=':
                settings.chopLimit += 0.001f;
                saveSettings(); forceRedraw = true;
                break;

            case '-':
                if (settings.chopLimit > 0.002f) settings.chopLimit -= 0.001f;
                saveSettings(); forceRedraw = true;
                break;

            case '.': case '>':
                if (settings.volume >= 16) settings.volume -= 16;
                else settings.volume = 0;
                M5Cardputer.Speaker.setVolume(settings.volume);
                if (!settings.isMuted) M5Cardputer.Speaker.tone(1000, 50);
                saveSettings();
                break;
                
            case ';': case ':':
                if (settings.volume <= 239) settings.volume += 16;
                else settings.volume = 255;
                M5Cardputer.Speaker.setVolume(settings.volume);
                if (!settings.isMuted) M5Cardputer.Speaker.tone(1000, 50);
                saveSettings();
                break;

            case 't': case 'T':
                logEvent("TEST BULL", 0.050f);
                bzState = BZ_BULL; bzTimer = millis();
                break;

            case 'y': case 'Y':
                logEvent("TEST BEAR", -0.050f);
                bzState = BZ_BEAR; bzTimer = millis();
                break;

            case 'w': case 'W':
                // Re-enter WiFi setup portal to change credentials
                runPortal(); // blocks until saved, then reboots
                break;
        }
    }
}

// =======================================================================================
// TFT DISPLAY ENGINE
// =======================================================================================

void drawInfoScreen() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(C_CYAN);
    M5Cardputer.Display.setCursor(4, 8);
    M5Cardputer.Display.print("SYSTEM INFO");

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(C_WHITE);
    
    M5Cardputer.Display.setCursor(4, 35);
    M5Cardputer.Display.printf("WiFi RSSI: %d dBm", WiFi.RSSI());
    
    M5Cardputer.Display.setCursor(4, 50);
    M5Cardputer.Display.printf("IP: %s", getLocalIPString().c_str());
    
    M5Cardputer.Display.setCursor(4, 65);
    M5Cardputer.Display.printf("RAM Free:  %d KB", ESP.getFreeHeap() / 1024);
    
    M5Cardputer.Display.setCursor(4, 80);
    M5Cardputer.Display.printf("Uptime:    %lu min", millis() / 60000UL);

    M5Cardputer.Display.setTextColor(C_DKGREY);
    M5Cardputer.Display.setCursor(4, 118);
    M5Cardputer.Display.print("Press [ I ] to close");
}

void drawFullScreen() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    if (showInfoScreen) {
        drawInfoScreen();
    } else {
        drawVelocityZone();
        drawGraph();
        drawPriceBar();
    }
    forceRedraw = false;
}

// ── VELOCITY ZONE  y 0..44 ───────────────────────────────────────────────────
// Dominant element. Left: big velocity number. Right: filled state badge.
void drawVelocityZone() {
    M5Cardputer.Display.fillRect(0, 0, DISP_W, 45, TFT_BLACK);

    uint16_t trendCol;
    const char* trendStr;
    if      (diff >  settings.chopLimit) { trendCol = C_GREEN;  trendStr = "BULL"; }
    else if (diff < -settings.chopLimit) { trendCol = C_RED;    trendStr = "BEAR"; }
    else                                 { trendCol = C_CYAN;   trendStr = "CHOP"; }

    // Velocity value — size 3 (18px tall), coloured, left-aligned
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setTextColor(trendCol);
    char vBuf[16];
    snprintf(vBuf, sizeof(vBuf), "% .4f%%", diff);
    M5Cardputer.Display.setCursor(3, 8);
    M5Cardputer.Display.print(vBuf);
    
    // "VELOCITY" label — tiny grey under the number
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(C_DKGREY);
    M5Cardputer.Display.setCursor(3, 33);
    M5Cardputer.Display.print("VELOCITY");
    
    // Mute indicator — tiny, next to label
    M5Cardputer.Display.setTextColor(settings.isMuted ? C_RED : C_DKGREY);
    M5Cardputer.Display.setCursor(62, 33);
    M5Cardputer.Display.print(settings.isMuted ? "MUTED" : "");

    // State badge — sized exactly for 4 chars at textSize 3
    // textSize 3 = 18px per char × 4 chars = 72px + 4px padding each side = 80px wide
    // Anchored flush to right edge: x = 240 - 80 = 160
    M5Cardputer.Display.fillRoundRect(160, 3, 78, 38, 5, trendCol);
    M5Cardputer.Display.setTextColor(TFT_BLACK);
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setCursor(163, 12);  // 3px left padding inside badge
    M5Cardputer.Display.print(trendStr);

    M5Cardputer.Display.drawFastHLine(0, 45, DISP_W, C_DKGREY);
}

// ── GRAPH ZONE  y 46..89 ─────────────────────────────────────────────────────
void drawGraph() {
    const int GX = 0, GY = 46, GW = DISP_W, GH = 44;
    M5Cardputer.Display.fillRect(GX, GY, GW, GH, TFT_BLACK);

    int zeroY = GY + GH / 2; // y=68
    M5Cardputer.Display.drawFastHLine(GX, zeroY, GW, C_DKGREY);

    // Auto-scale
    float maxAbs = settings.chopLimit * 4.0f;
    for (int i = 0; i < HISTORY_SIZE; i++) {
        float v = fabs(velocityHistory[i]);
        if (v > maxAbs) maxAbs = v;
    }
    if (maxAbs < 0.001f) maxAbs = 0.001f;
    
    // Dotted chop-limit band
    int chopPx = (int)((settings.chopLimit / maxAbs) * (GH / 2));
    chopPx = max(1, min(chopPx, GH / 2 - 1));
    for (int x = GX; x < GX + GW; x += 4) {
        M5Cardputer.Display.drawPixel(x, zeroY - chopPx, C_GREEN);
        M5Cardputer.Display.drawPixel(x, zeroY + chopPx, C_RED);
    }

    // Bars — 4px wide for 60 samples = 240px
    int barW = GW / HISTORY_SIZE;
    if (barW < 1) barW = 1;

    for (int i = 0; i < HISTORY_SIZE; i++) {
        float v = velocityHistory[(historyHead + i) % HISTORY_SIZE];
        if (std::isnan(v) || std::isinf(v)) continue;

        int barH = (int)((fabs(v) / maxAbs) * (GH / 2));
        barH = min(barH, GH / 2);
        if (barH < 1 && v != 0) barH = 1;

        uint16_t col;
        if      (v >  settings.chopLimit) col = C_GREEN;
        else if (v < -settings.chopLimit) col = C_RED;
        else                              col = C_CYAN;
        
        int xPos = GX + i * barW;
        if (v >= 0) M5Cardputer.Display.fillRect(xPos, zeroY - barH, barW - 1, barH, col);
        else        M5Cardputer.Display.fillRect(xPos, zeroY + 1,    barW - 1, barH, col);
    }

    M5Cardputer.Display.drawFastHLine(0, 90, DISP_W, C_DKGREY);
}

// ── INFO BAR  y 91..134 ──────────────────────────────────────────────────────
// Two compact rows: price/symbol top, PnL/time bottom
void drawPriceBar() {
    M5Cardputer.Display.fillRect(0, 91, DISP_W, 44, TFT_BLACK);
    
    float openPnL = 0.0f;
    if (currentPos == POS_LONG)  openPnL = lastPrice - tradeEntryPrice;
    if (currentPos == POS_SHORT) openPnL = tradeEntryPrice - lastPrice;

    const char* posStr;
    uint16_t posCol;
    if      (currentPos == POS_LONG)  { posStr = "LONG";  posCol = C_GREEN;  }
    else if (currentPos == POS_SHORT) { posStr = "SHORT"; posCol = C_RED;    }
    else                              { posStr = "FLAT";  posCol = C_DKGREY; }

    uint16_t opCol = (openPnL  >= 0) ? C_GREEN : C_RED;
    uint16_t cpCol = (closedPnL >= 0) ? C_GREEN : C_RED;
    
    // ── Row 1  y 94: SYM  $price  |  POS  Open PnL
    M5Cardputer.Display.setTextSize(2);
    
    // Symbol — yellow
    M5Cardputer.Display.setTextColor(C_YELLOW);
    M5Cardputer.Display.setCursor(3, 94);
    M5Cardputer.Display.print(settings.symbol);

    // Price — white
    M5Cardputer.Display.setTextColor(C_WHITE);
    char pBuf[10]; snprintf(pBuf, sizeof(pBuf), " $%.2f", lastPrice);
    M5Cardputer.Display.print(pBuf);

    // POS badge — right side, size 1
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(posCol);
    M5Cardputer.Display.setCursor(188, 94);
    M5Cardputer.Display.print(posStr);

    // ── Row 2  y 112: Open PnL | Total PnL | time
    M5Cardputer.Display.setTextSize(1);

    // Open PnL
    M5Cardputer.Display.setTextColor(C_DKGREY);
    M5Cardputer.Display.setCursor(3, 116);
    M5Cardputer.Display.print("O:");
    M5Cardputer.Display.setTextColor(opCol);
    char opBuf[10];
    snprintf(opBuf, sizeof(opBuf), "%+.2f", openPnL);
    M5Cardputer.Display.print(opBuf);

    // Total PnL
    M5Cardputer.Display.setTextColor(C_DKGREY);
    M5Cardputer.Display.setCursor(68, 116);
    M5Cardputer.Display.print("T:");
    M5Cardputer.Display.setTextColor(cpCol);
    char cpBuf[10];
    snprintf(cpBuf, sizeof(cpBuf), "%+.2f", closedPnL);
    M5Cardputer.Display.print(cpBuf);

    // Trade count
    M5Cardputer.Display.setTextColor(C_DKGREY);
    M5Cardputer.Display.setCursor(138, 116);
    char tcBuf[8]; snprintf(tcBuf, sizeof(tcBuf), "#%d", tradeCount);
    M5Cardputer.Display.print(tcBuf);

    // Time — right-aligned
    M5Cardputer.Display.setTextColor(C_DKGREY);
    M5Cardputer.Display.setCursor(176, 116);
    M5Cardputer.Display.print(getTimeString());
}

// ── FULL SCREEN SIGNAL ALERT ──────────────────────────────────────────────────
// Entire screen floods with signal colour and flashes 3x — unmissable.
void drawSignalAlert(const char* bigLabel, const char* subLabel, uint16_t bgCol) {
    auto paintFrame = [&]() {
        M5Cardputer.Display.fillScreen(bgCol);
        
        // Giant label (BUY / SELL / EXIT) — black on colour, size 5 = 30px/char
        M5Cardputer.Display.setTextColor(TFT_BLACK);
        M5Cardputer.Display.setTextSize(5);
        int x1 = max(0, (DISP_W - (int)strlen(bigLabel) * 30) / 2);
        M5Cardputer.Display.setCursor(x1, 15);
        M5Cardputer.Display.print(bigLabel);
        
        // Sub-label (entry price / confirmation type) — size 2, centred below
        M5Cardputer.Display.setTextSize(2);
        int x2 = max(0, (DISP_W - (int)strlen(subLabel) * 12) / 2);
        M5Cardputer.Display.setCursor(x2, 95);
        M5Cardputer.Display.print(subLabel);
    };
    
    // 3 flashes: colour → black → colour
    for (int i = 0; i < 3; i++) {
        paintFrame();
        delay(280);
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        delay(140);
    }

    // Final hold — 3.5 seconds so the trader can act on it
    paintFrame();
    delay(3500);
}

// Keep old name as a thin wrapper so existing call sites compile unchanged
void drawSignalOverlay(const char* line1, const char* line2, uint16_t colour) {
    drawSignalAlert(line1, line2, colour);
}

// =======================================================================================
// WEB SERVER — JSON API + HTML DASHBOARD
// Single-threaded, handles one request per loop() tick.
// Two routes:
//   GET /data → JSON payload (polled every 3s by the web dashboard JS)
//   GET /     → full HTML dashboard (with optional command params in the URL)
//
// Web commands are embedded as GET params in the root URL, e.g. /?sym=QQQ or /?mute=1.
// This avoids needing POST/AJAX for simple actions and keeps the HTML minimal.
// =======================================================================================
void handleWebTraffic() {
    WiFiClient client = webServer.accept();
    if (!client) return;
    
    // Wait up to 500ms for the request line — needed on slow connections
    unsigned long t0 = millis();
    while (!client.available() && (millis() - t0) < 500) delay(1);
    if (!client.available()) { client.stop(); return; }

    String req = client.readStringUntil('\n');
    while (client.available()) client.read(); // drain remaining headers (we don't need them)

    // Route requests
    if (req.indexOf("GET /data") != -1) {
        serveJSON(client);
    } else {
        // Parse any action commands embedded in the URL before serving the page
        bool saveNeeded = false;
        
        if (req.indexOf("sym=SPY") != -1) { strcpy(settings.symbol, "SPY"); applySymbolPreset("SPY"); initialized=false; resetTrades(); saveNeeded=true; forceRedraw=true; }
        if (req.indexOf("sym=QQQ") != -1) { strcpy(settings.symbol, "QQQ"); applySymbolPreset("QQQ"); initialized=false; resetTrades(); saveNeeded=true; forceRedraw=true; }
        if (req.indexOf("sym=IWM") != -1) { strcpy(settings.symbol, "IWM"); applySymbolPreset("IWM"); initialized=false; resetTrades(); saveNeeded=true; forceRedraw=true; }
        if (req.indexOf("mute=1") != -1) { settings.isMuted = true;  saveNeeded = true; forceRedraw = true; }
        if (req.indexOf("mute=0") != -1) { settings.isMuted = false; saveNeeded = true; forceRedraw = true; }
        
        if (req.indexOf("chop=")  != -1) {
            int idx = req.indexOf("chop=") + 5;
            float nc = req.substring(idx).toFloat();
            if (nc > 0) { settings.chopLimit = nc; saveNeeded = true; }
        }
        if (req.indexOf("vol=up") != -1) { 
            if (settings.volume <= 239) settings.volume += 16; else settings.volume = 255; 
            M5Cardputer.Speaker.setVolume(settings.volume);
            if (!settings.isMuted) M5Cardputer.Speaker.tone(1000, 50);
            saveNeeded = true; 
        }
        if (req.indexOf("vol=dn") != -1) { 
            if (settings.volume >= 16) settings.volume -= 16; else settings.volume = 0; 
            M5Cardputer.Speaker.setVolume(settings.volume);
            if (!settings.isMuted) M5Cardputer.Speaker.tone(1000, 50);
            saveNeeded = true; 
        }
        if (req.indexOf("test=bull") != -1) { bzState = BZ_BULL; bzTimer = millis(); logEvent("TEST BULL",  0.050f); }
        if (req.indexOf("test=bear") != -1) { bzState = BZ_BEAR; bzTimer = millis(); logEvent("TEST BEAR", -0.050f); }

        if (saveNeeded) saveSettings();
        serveHTML(client);
    }

    client.clear();
    delay(5);
    client.stop();
}

void serveJSON(WiFiClient& client) {
    float openPnL = 0.0f;
    if (currentPos == POS_LONG)  openPnL = lastPrice - tradeEntryPrice;
    if (currentPos == POS_SHORT) openPnL = tradeEntryPrice - lastPrice;
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    client.print("{\"price\":"); client.print(lastPrice, 2);
    client.print(",\"diff\":"); client.print(diff, 4);
    client.print(",\"chop\":"); client.print(settings.chopLimit, 4);
    client.print(",\"sym\":\""); client.print(settings.symbol); client.print("\"");
    
    client.print(",\"pos\":\"");
    if      (currentPos == POS_NONE)  client.print("NONE");
    else if (currentPos == POS_LONG)  client.print("LONG");
    else                              client.print("SHORT");
    client.print("\"");
    
    client.print(",\"entry\":"); client.print(tradeEntryPrice, 2);
    client.print(",\"openPnl\":"); client.print(openPnL, 2);
    client.print(",\"closedPnl\":"); client.print(closedPnL, 2);
    client.print(",\"trades\":"); client.print(tradeCount);

    // Uptime in seconds
    client.print(",\"uptime\":");
    client.print(millis() / 1000UL);

    // RSSI
    client.print(",\"rssi\":"); client.print(WiFi.RSSI());

    // Free heap
    client.print(",\"heap\":"); client.print(ESP.getFreeHeap() / 1024);
    
    // Alert log
    client.print(",\"alerts\":[");
    bool first = true;
    for (int i = 0; i < 5; i++) {
        if (alertLog[i].type[0] != '\0') {
            if (!first) client.print(",");
            client.print("{\"t\":\""); client.print(alertLog[i].time); client.print("\"");
            client.print(",\"e\":\""); client.print(alertLog[i].type); client.print("\"");
            client.print(",\"v\":"); client.print(alertLog[i].val, 3);
            client.print("}");
            first = false;
        }
    }
    client.print("]");

    // Velocity history
    client.print(",\"history\":[");
    for (int i = 0; i < HISTORY_SIZE; i++) {
        float v = velocityHistory[(historyHead + i) % HISTORY_SIZE];
        if (std::isnan(v) || std::isinf(v)) client.print("0.0");
        else client.print(v, 4);
        if (i < HISTORY_SIZE - 1) client.print(",");
    }
    client.print("]}");
}

void serveHTML(WiFiClient& client) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    
    // Head
    client.print(
        "<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
        "<style>"
        "body{font-family:'Segoe UI',sans-serif;background:#121212;color:#eee;margin:0;padding:10px}"
        ".container{max-width:600px;margin:0 auto;width:100%;box-sizing:border-box}"
        ".card{background:#1e1e1e;padding:15px;border-radius:8px;margin-bottom:15px;border:1px solid #333}"
        "h2{color:#00ff88;font-size:16px;border-bottom:1px solid #333;padding-bottom:5px;margin-top:0}"
        "h3{color:#fff;font-size:14px;margin:10px 0 5px 0}"
        "p,li{font-size:13px;color:#ccc;line-height:1.4;margin:5px 0}"
        ".stat{font-size:12px;color:#aaa;display:inline-block;margin-right:15px}"
        ".val{color:#fff;font-weight:bold}"
        "button{width:100%;padding:12px;margin:4px 0;border-radius:5px;cursor:pointer;border:none;font-weight:bold;color:#fff;background:#333}"
        ".gold{background:#FFD700!important;color:#000!important;border:2px solid #fff}"
        "input{width:100%;padding:10px;background:#2c2c2c;border:1px solid #444;color:#fff;border-radius:4px;box-sizing:border-box}"
        "</style></head><body><div class='container'>"
    );
    
    // Vitals
    client.print("<div class='card'><h2>SYSTEM VITALS</h2>");
    client.printf("<div class='stat'>WiFi: <span class='val'>%ddBm</span></div>", WiFi.RSSI());
    client.printf("<div class='stat'>RAM: <span class='val'>%d / %d kB free</span></div>", (int)(ESP.getFreeHeap()/1024), (int)(ESP.getHeapSize()/1024));
    client.printf("<div class='stat'>Uptime: <span class='val'>%lum min</span></div>", millis()/60000UL);
    client.print("</div>");

    // Live Monitor
    client.print("<div class='card'><h2>LIVE MONITOR</h2>");
    client.printf("<p>SYMBOL: <b id='symText' style='color:#FFD700'>%s</b> | PRICE: <b id='priceText'>$%.2f</b></p>",
                  settings.symbol, lastPrice);
    client.print("<div style='position:relative;height:180px;width:100%'><canvas id='c'></canvas></div></div>");

    // Trade Tracker
    client.print("<div class='card'><h2>TRADE TRACKER (PAPER PnL)</h2>");
    client.print("<div class='stat'>Pos: <span class='val' id='posText'>NONE</span></div>");
    client.print("<div class='stat'>Entry: <span class='val' id='entryText'>$0.00</span></div>");
    client.print("<div class='stat'>Open PnL: <span class='val' id='openPnlText'>$0.00</span></div>");
    client.print("<div class='stat'>Total PnL: <span class='val' id='closedPnlText'>$0.00</span></div>");
    client.print("<div class='stat'>Trades: <span class='val' id='tradesText'>0</span></div>");
    client.print("</div>");

    // Alerts
    client.print("<div class='card'><h2>RECENT ALERTS (LAST 5)</h2>");
    client.print("<div id='alertBox' style='min-height:20px;color:#888;font-size:13px'>Waiting...</div></div>");
    
    // Presets
    String clsSpy = (strcmp(settings.symbol,"SPY")==0) ? "gold" : "";
    String clsQqq = (strcmp(settings.symbol,"QQQ")==0) ? "gold" : "";
    String clsIwm = (strcmp(settings.symbol,"IWM")==0) ? "gold" : "";
    client.print("<div class='card'><h2>PRESETS</h2><div style='display:flex;gap:10px'>");
    client.print("<a href='/?sym=SPY' style='flex:1'><button class='" + clsSpy + "'>SPY</button></a>");
    client.print("<a href='/?sym=QQQ' style='flex:1'><button class='" + clsQqq + "'>QQQ</button></a>");
    client.print("<a href='/?sym=IWM' style='flex:1'><button class='" + clsIwm + "'>IWM</button></a>");
    client.print("</div></div>");

    // Expert Tuning
    client.print("<div class='card'><h2>EXPERT TUNING</h2>");
    client.printf("<form action='/' method='get'><div class='stat'>Chop Limit (%%): </div>"
                  "<div style='display:flex;gap:10px'>"
                  "<input type='text' name='chop' value='%.3f'>"
                  "<button style='width:auto;background:#00ff88;color:#000'>UPDATE</button>"
                  "</div></form></div>", settings.chopLimit);
                  
// Admin
    client.print("<div class='card'><h2>ADMIN</h2>");
    
    // New Volume Control Row
    client.print("<div style='display:flex;gap:10px;margin-bottom:10px'>");
    client.print("<a href='/?vol=dn' style='flex:1'><button style='background:#444'>VOL -</button></a>");
    client.printf("<div style='flex:1;text-align:center;line-height:40px;color:#00ff88;font-weight:bold'>VOL: %d%%</div>", (int)((settings.volume/255.0)*100));
    client.print("<a href='/?vol=up' style='flex:1'><button style='background:#444'>VOL +</button></a>");
    client.print("</div>");
    
    // Existing Mute and Test buttons
    client.print("<a href='/?mute=");
    client.print(settings.isMuted ? "0" : "1");
    client.print("'><button>");
    client.print(settings.isMuted ? "UNMUTE AUDIO" : "MUTE AUDIO");
    client.print("</button></a>");
    client.print("<div style='display:flex;gap:10px'>"
                 "<a href='/?test=bull' style='flex:1'><button style='background:#0f8;color:#000'>TEST BULL</button></a>"
                 "<a href='/?test=bear' style='flex:1'><button style='background:#f44'>TEST BEAR</button></a>"
                 "</div>"
                 "</div>");

    // About
    client.print("<div class='card'><h2>ABOUT SQUAWK BOX</h2>");
    client.print("<p>Running on <strong>M5Stack Cardputer ADV</strong> (ESP32-S3) with direct Finnhub REST polling.</p>");
    
    client.print("<h3>Keyboard Shortcuts</h3>"
                 "<ul style='list-style-type:none;padding-left:0;margin-top:5px;line-height:1.6'>"
                 "<li><b>[ I ]</b> Toggle System Info</li>"
                 "<li><b>[ M ]</b> Toggle Mute</li>"
                 "<li><b>[ B ]</b> Toggle Backlight</li>"
                 "<li><b>[ . / ; ]</b> Volume Down / Up</li>"
                 "<li><b>[ 1 / 2 / 3 ]</b> Symbol (SPY / QQQ / IWM)</li>"
                 "<li><b>[ + / - ]</b> Adjust Chop Limit</li>"
                 "<li><b>[ T / Y ]</b> Test Bull / Bear Tones</li>"
                 "</ul>"
                 "<p style='margin-top:12px;font-size:13px'>"
                 "<b>WiFi Setup:</b> On first run or hold <b>[W]</b> while powering on to enter setup mode.</p>");

    client.print("<h3>Signal Logic</h3><ul>"
                 "<li><b style='color:#0f8'>BUY:</b> Bull Break &rarr; Bull Rush (&ge;0.016%) within 15s</li>"
                 "<li><b style='color:#f44'>SELL:</b> Bear Break &rarr; Bear Dump (&le;-0.025%) within 15s</li>"
                 "<li><b style='color:#FFD700'>LUNCH EXIT:</b> Trend End between 12:00&ndash;1:30 PM EST</li></ul>");
                 
    client.print("<h3>&#9888; Disclaimer</h3>"
                 "<p style='color:#f44;font-size:12px;border:1px solid #f44;padding:10px;border-radius:6px'>"
                 "<strong>FOR EDUCATIONAL AND ENTERTAINMENT PURPOSES ONLY.</strong> "
                 "Squawk Box is a personal hobby project and paper-trading simulator. "
                 "It does <strong>not</strong> constitute financial advice, investment advice, or a recommendation to buy or sell any security. "
                 "All signals are experimental and based on short-term EMA velocity — they have no guaranteed predictive value. "
                 "Past paper performance does not imply future real-world results. "
                 "Never trade real money based solely on this tool. "
                 "Consult a licensed financial advisor before making any investment decisions."
                 "</p></div>");
                 
    client.print("<div style='text-align:center;color:#555;font-size:11px;padding:10px'>SQUAWK BOX v9.3 CARDPUTER</div></div>");

    // JavaScript — auto-refresh every 3s
    client.printf(
        "<script>"
        "let chart;"
        "const chop=%s;"
        "function update(){"
        "fetch('/data').then(r=>r.json()).then(j=>{"
        "document.getElementById('priceText').innerText='$'+j.price.toFixed(2);"
        "const pc=j.pos==='LONG'?'#0f8':j.pos==='SHORT'?'#f44':'#fff';"
        "document.getElementById('posText').innerText=j.pos;"
        "document.getElementById('posText').style.color=pc;"
        "document.getElementById('entryText').innerText='$'+j.entry.toFixed(2);"
        "document.getElementById('openPnlText').innerText='$'+j.openPnl.toFixed(2);"
        "document.getElementById('openPnlText').style.color=j.openPnl>=0?'#0f8':'#f44';"
        "document.getElementById('closedPnlText').innerText='$'+j.closedPnl.toFixed(2);"
        "document.getElementById('closedPnlText').style.color=j.closedPnl>=0?'#0f8':'#f44';"
        "document.getElementById('tradesText').innerText=j.trades;"
        "let h='';"
        "if(!j.alerts||j.alerts.length===0)h='No alerts yet...';"
        "else for(let a of j.alerts){"
        "let c='#fff';"
        "if(a.e.includes('BULL'))c='#0f8';"
        "if(a.e.includes('BEAR'))c='#f44';"
        "if(a.e.includes('SIGNAL'))c='#FFD700';"
        "h+=`<div style='border-bottom:1px solid #333;padding:5px 0;font-family:monospace;display:flex;justify-content:space-between'>"
        "<span><span style='color:#666;margin-right:10px'>${a.t}</span>"
        "<span style='color:${c};font-weight:bold'>${a.e}</span></span>"
        "<span style='color:#eee'>${a.v.toFixed(3)}%%</span></div>`;"
        "}"
        "document.getElementById('alertBox').innerHTML=h;"
        // Fix: always sync labels length to data length so Chart.js renders bars
        "chart.data.labels=j.history.map((_,i)=>i);"
        "chart.data.datasets[0].data=j.history;"
        "let col='#0ff';"
        "if(j.diff>chop)col='#0f8';"
        "else if(j.diff<-chop)col='#f44';"
        "chart.data.datasets[0].borderColor=col;"
        "chart.data.datasets[0].backgroundColor=col+'22';"
        "chart.update('none');"  // 'none' skips animation for snappy live updates
        "}).catch(e=>console.log(e));"
        "}"
        "window.onload=()=>{"
        "const ctx=document.getElementById('c').getContext('2d');"
        "chart=new Chart(ctx,{"
        "type:'line',"
        "data:{labels:[],datasets:[{"
        "data:[],"
        "borderColor:'#0ff',"
        "backgroundColor:'#0ff22',"
        "pointRadius:0,"
        "tension:0.3,"
        "fill:true,"
        "borderWidth:2"
        "}]},"
        "options:{"
        "maintainAspectRatio:false,"
        "responsive:true,"
        "animation:false,"
        "plugins:{"
        "legend:{display:false},"
        // Inline chop lines using scales instead of annotation plugin (more compatible)
        "tooltip:{enabled:false}"
        "},"
        "scales:{"
        "y:{grid:{color:'#333'},ticks:{color:'#666',font:{size:10}},"
        // Draw zero and chop threshold lines via y-axis grid callbacks
        "afterDataLimits:function(ax){ax.max=Math.max(ax.max,chop*2);ax.min=Math.min(ax.min,-chop*2);}},"
        "x:{display:false}"
        "}"
        "},"
        "plugins:[{"
        // Inline plugin: draw chop threshold lines directly on canvas
        "id:'chopLines',"
        "afterDraw:function(ch){"
        "const ctx=ch.ctx,a=ch.chartArea,yA=ch.scales.y;"
        "const lines=[{v:0,c:'#FFD700',w:2},{v:chop,c:'#0f8',w:1,dash:[5,5]},{v:-chop,c:'#f44',w:1,dash:[5,5]}];"
        "lines.forEach(l=>{"
        "const y=yA.getPixelForValue(l.v);"
        "if(y<a.top||y>a.bottom)return;"
        "ctx.save();ctx.beginPath();"
        "ctx.strokeStyle=l.c;ctx.lineWidth=l.w||1;"
        "if(l.dash)ctx.setLineDash(l.dash);else ctx.setLineDash([]);"
        "ctx.moveTo(a.left,y);ctx.lineTo(a.right,y);"
        "ctx.stroke();ctx.restore();"
        "});"
        "}"
        "}]"
        "});"
        "setInterval(update,3000);update();"
        "};"
        "</script></body></html>",
        String(settings.chopLimit, 4).c_str()
    );
}

// =======================================================================================
// SETUP
// Boot sequence:
//  1. Init hardware (display, speaker, keyboard)
//  2. Show splash screen
//  3. Load credentials + settings from NVS
//  4. If no credentials saved → launch captive portal (blocks until saved, then reboots)
//  5. Attempt WiFi connection — if it fails, launch portal so user can fix credentials
//  6. Sync time via NTP
//  7. Start web server
//  8. Draw main UI
// =======================================================================================
void setup() {
    Serial.begin(115200);

    // Init M5Cardputer (display, speaker, keyboard)
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);            // Landscape: 240 wide x 135 tall
    M5Cardputer.Display.setTextFont(0);            // Built-in 6x8 pixel font
    M5Cardputer.Display.setBrightness(200);        // Brighter for readability
    M5Cardputer.Display.fillScreen(TFT_BLACK);     // Force true black first

    // Splash — dark theme matching web console
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    
    // Header bar
    M5Cardputer.Display.fillRect(0, 0, DISP_W, 20, C_CARD);
    M5Cardputer.Display.setTextColor(C_GREEN);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(4, 3);
    M5Cardputer.Display.print("SQUAWK BOX v9.3");
    
    // Subtitle
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.setCursor(4, 30);
    M5Cardputer.Display.print("CARDPUTER ADV EDITION");
    
    M5Cardputer.Display.setCursor(4, 45);
    M5Cardputer.Display.setTextColor(C_YELLOW);
    M5Cardputer.Display.print("Connecting to WiFi...");

    // Load credentials from NVS
    loadCredentials();
    
    // Load trading/display settings from NVS
    loadSettings();
    M5Cardputer.Speaker.setVolume(settings.volume);

    // If no credentials saved, or [W] held at boot → run setup portal
    // (Portal blocks until saved, then reboots)
    if (!hasCredentials()) {
        runPortal(); // never returns — reboots on save
    }

    // Connect to WiFi using saved credentials
    M5Cardputer.Display.setTextColor(C_YELLOW);
    M5Cardputer.Display.setCursor(4, 45);
    M5Cardputer.Display.print("Connecting to WiFi...");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.setCursor(4, 58);
    M5Cardputer.Display.print(creds.ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(creds.ssid, creds.pass);
    
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 40) {
        delay(500);
        Serial.print(".");
        tries++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        // WiFi failed — show error then launch portal so user can fix credentials
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(C_RED);
        M5Cardputer.Display.setCursor(4, 10);
        M5Cardputer.Display.print("WiFi FAILED");
        
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(C_WHITE);
        M5Cardputer.Display.setCursor(4, 40);
        M5Cardputer.Display.print("Could not connect to:");
        
        M5Cardputer.Display.setTextColor(C_YELLOW);
        M5Cardputer.Display.setCursor(4, 52);
        M5Cardputer.Display.print(creds.ssid);
        
        M5Cardputer.Display.setTextColor(C_GREY);
        M5Cardputer.Display.setCursor(4, 72);
        M5Cardputer.Display.print("Launching setup in 3s...");
        
        delay(3000);
        runPortal(); // never returns
    } else {
        Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
        
        // NTP sync
        configTzTime(TZ_INFO, NTP_SERVER);
        struct tm ti; int ntpTries = 0;
        while (!getLocalTime(&ti) && ntpTries++ < 10) delay(500);

        // Show IP on splash
        M5Cardputer.Display.setTextColor(C_GREEN);
        M5Cardputer.Display.setCursor(4, 72);
        M5Cardputer.Display.print("Connected! ");
        M5Cardputer.Display.setTextColor(C_YELLOW);
        M5Cardputer.Display.print(WiFi.localIP().toString());

        // Startup beep
        M5Cardputer.Speaker.tone(1000, 100);
        delay(150);
        M5Cardputer.Speaker.tone(1200, 100);
    }

    // Start web server
    webServer.begin();
    Serial.println("[Web] Server started on port 80");
    
    delay(2500); // Hold splash

    // Clear and draw main UI
    forceRedraw = true;
    drawFullScreen();
}

// =======================================================================================
// MAIN LOOP
// Runs continuously. Order matters:
//  1. update()       — must be called first every tick to scan the keyboard
//  2. handleKeyboard — process any key presses
//  3. handleWebTraffic — serve one pending HTTP request if any
//  4. updateBuzzer   — advance the non-blocking tone sequencer
//  5. forceRedraw    — repaint screen if any state has changed
//  6. Smart poll     — fetch a new price from Finnhub when interval has elapsed
// =======================================================================================
void loop() {
    M5Cardputer.update(); // required every tick — scans keyboard matrix

    handleKeyboard();
    handleWebTraffic();
    updateBuzzer();
    
    // Redraw full screen if flagged
    if (forceRedraw) {
        drawFullScreen();
    }

    // Smart polling for Finnhub
    unsigned long interval = getSmartInterval() * 1000UL;
    if (millis() - lastPollMs > interval) {
        lastPollMs = millis();
        fetchQuote();
    }

    delay(10); // Yield to WiFi stack
}
