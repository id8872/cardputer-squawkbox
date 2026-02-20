/*
 * =======================================================================================
 * SQUAWK BOX v9.8 - CARDPUTER ADV EDITION
 * =======================================================================================
 * AUTHOR: Jason Edgar (Orillia, ON)
 * ORIGINAL PLATFORM: Particle Photon 2
 * PORTED TO: M5Stack Cardputer (ESP32-S3, ST7789 135x240 TFT, built-in keyboard/speaker)
 *
 * FIRST TIME SETUP — NO HARDCODED CREDENTIALS NEEDED:
 *  1. Flash this sketch as-is.
 *  2. On first boot, the Cardputer creates a WiFi hotspot: SQUAWKBOX-SETUP
 *  3. Connect your phone/laptop to that network (no password).
 *  4. A setup page opens automatically (or visit 192.168.4.1).
 *  5. Enter your WiFi SSID, password, and Finnhub API key → Save.
 *  6. Device reboots and connects to your network.
 *  7. Press [W] at any time to re-enter setup mode and change credentials.
 *
 * KEYBOARD MAP:
 *  [M]  = Toggle Mute
 *  [B]  = Toggle Backlight
 *  [I]  = Toggle Device Info Screen 
 *  [1]  = Switch to SPY
 *  [2]  = Switch to QQQ
 *  [3]  = Switch to IWM
 *  [+]  = Increase Chop Limit by 0.001
 *  [-]  = Decrease Chop Limit by 0.001
 *  [T]  = Test Bull tone
 *  [Y]  = Test Bear tone
 *  [W]  = WiFi setup portal (change credentials)
 *  [R]  = Reboot device
 *
 * REQUIRED LIBRARIES (install via Arduino Library Manager):
 *  - M5Cardputer  (M5Stack official)
 *  - M5GFX        (M5Stack official)
 *  - ArduinoJson  (Benoit Blanchon, v7+)
 *  - Preferences  (built-in ESP32 core)
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
// DISPLAY DIMENSIONS (ST7789 on Cardputer, landscape)
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
// =======================================================================================
struct Credentials {
    char ssid[64];
    char pass[64];
    char apikey[64];
    char hostname;
};
Credentials creds;

// Captive portal
DNSServer     dnsServer;
WiFiServer    portalServer(80);
bool          inPortalMode = false;

void loadCredentials() {
    Preferences p;
    p.begin("wificreds", true);
    p.getString("ssid",   creds.ssid,   sizeof(creds.ssid));
    p.getString("pass",   creds.pass,   sizeof(creds.pass));
    p.getString("apikey", creds.apikey, sizeof(creds.apikey));
    p.getString("host",   creds.hostname, sizeof(creds.hostname));
    p.end();

    // Set a default hostname on first boot
    if (strlen(creds.hostname) == 0) {
        strcpy(creds.hostname, "squawkbox");
    }
}

void saveCredentials() {
    Preferences p;
    p.begin("wificreds", false);
    p.putString("ssid",   creds.ssid);
    p.putString("pass",   creds.pass);
    p.putString("apikey", creds.apikey);
    p.putString("host",   creds.hostname);
    p.end();
}

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
    client.println("Content-Type: text/html; charset=UTF-8"); // <-- FIXED: Added UTF-8
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
        // FIXED: Swapped raw checkmark for safe HTML entity &check;
        client.print("<div class='saved'>&check; Saved! Rebooting in 3 seconds...</div>");
    }
    client.print(
        "<form method='GET' action='/save'>"
        "<label>WiFi Network Name (SSID)</label>"
        "<input name='s' type='text' placeholder='YourNetworkName' autocomplete='off'>"
        "<label>WiFi Password</label>"
        "<input name='p' type='password' placeholder='YourPassword'>"
        "<label>Finnhub API Key</label>"
        "<input name='k' type='text' placeholder='d1abc123xyz...' autocomplete='off'>"
        "<label>Device Hostname</label>"
        "<input name='h' type='text' value='" + String(creds.hostname) + "' placeholder='squawkbox' autocomplete='off'>"
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

// Full captive portal loop — blocks until credentials are saved
void runPortal() {
    inPortalMode = true;

    // Start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SQUAWKBOX-SETUP");
    delay(500);

    // DNS: redirect all domains to 192.168.4.1
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
                    String h = extractParam(req, "h");

                    if (s.length() > 0 && k.length() > 0) {
                        s.toCharArray(creds.ssid,   sizeof(creds.ssid));
                        p.toCharArray(creds.pass,   sizeof(creds.pass));
                        k.toCharArray(creds.apikey, sizeof(creds.apikey));

                        if (h.length() > 0) {
                            h.toCharArray(creds.hostname, sizeof(creds.hostname));
                        } else {
                            strcpy(creds.hostname, "squawkbox");
                        }

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
// =======================================================================================
const float P_SPY_FAST = 0.22f; const float P_SPY_SLOW = 0.10f; const float P_SPY_CHOP = 0.010f;
const float P_QQQ_FAST = 0.25f; const float P_QQQ_SLOW = 0.12f; const float P_QQQ_CHOP = 0.014f;
const float P_IWM_FAST = 0.28f; const float P_IWM_SLOW = 0.14f; const float P_IWM_CHOP = 0.035f;

// Confluence thresholds
const unsigned long CONFIRMATION_WINDOW = 15000UL;
const float BULL_RUSH_THRESHOLD =  0.016f;
const float BEAR_DUMP_THRESHOLD = -0.025f;

// =======================================================================================
// PERSISTENT CONFIG (NVS via Preferences)
// =======================================================================================
struct Config {
    float alphaFast;
    float alphaSlow;
    float chopLimit;
    bool  isMuted;
    bool  backlightOn;
    char  symbol[8];
};
Config settings;
Preferences prefs;

// =======================================================================================
// SYSTEM STATE
// =======================================================================================
volatile float emaFast = 0, emaSlow = 0, diff = 0;
volatile float lastPrice = 0;
bool initialized   = false;
bool forceRedraw   = true;      // full screen redraw flag
bool showInfoScreen = false;  

unsigned long lastPollMs   = 0;
unsigned long lastWebMs    = 0;

// --- SIGNAL STATE MACHINE ---
enum SignalState { IDLE, TRIGGERED, CONFIRMED };
SignalState currentSignal  = IDLE;
unsigned long lastTriggerTime = 0;

// --- PAPER TRADING ---
enum PositionState { POS_NONE, POS_LONG, POS_SHORT };
PositionState currentPos   = POS_NONE;
float tradeEntryPrice      = 0.0f;
float closedPnL            = 0.0f;
int   tradeCount           = 0;

// --- ALERT LOG ---
struct Alert {
    char time[9];
    char type[16];
    float val;
};
Alert alertLog[5];

// --- GRAPH / HISTORY BUFFER ---
const int HISTORY_SIZE = 60;          // 60 samples fits well on 240px wide screen
volatile float velocityHistory[HISTORY_SIZE];
volatile int   historyHead = 0;

// --- AUDIO STATE MACHINE ---
enum BuzzerState { BZ_IDLE, BZ_BULL, BZ_BEAR, BZ_STUTTER };
BuzzerState bzState  = BZ_IDLE;
unsigned long bzTimer = 0;

const int DUR_BULLISH = 200;
const int DUR_BEARISH = 1000;

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
// =======================================================================================
void closePosition() {
    if (currentPos == POS_NONE) return;
    float pnl = 0.0f;
    if (currentPos == POS_LONG)  pnl = lastPrice - tradeEntryPrice;
    if (currentPos == POS_SHORT) pnl = tradeEntryPrice - lastPrice;
    closedPnL += pnl;
    tradeCount++;
    currentPos     = POS_NONE;
    tradeEntryPrice = 0.0f;
}

void openLong() {
    if (currentPos == POS_SHORT) closePosition();
    if (currentPos == POS_NONE)  { currentPos = POS_LONG;  tradeEntryPrice = lastPrice; }
}

void openShort() {
    if (currentPos == POS_LONG)  closePosition();
    if (currentPos == POS_NONE)  { currentPos = POS_SHORT; tradeEntryPrice = lastPrice; }
}

void resetTrades() {
    currentPos      = POS_NONE;
    tradeEntryPrice = 0.0f;
    closedPnL       = 0.0f;
    tradeCount      = 0;
}

// =======================================================================================
// SETTINGS (NVS)
// =======================================================================================
void loadSettings() {
    prefs.begin("squawk", false);
    uint8_t ver = prefs.getUChar("ver", 0);
    if (ver != 19) {
        // First boot or version bump — write defaults
        strcpy(settings.symbol, "SPY");
        applySymbolPreset("SPY");
        settings.isMuted    = false;
        settings.backlightOn = true;
        saveSettings();
        prefs.putUChar("ver", 19);
    } else {
        settings.alphaFast   = prefs.getFloat("aFast",  P_SPY_FAST);
        settings.alphaSlow   = prefs.getFloat("aSlow",  P_SPY_SLOW);
        settings.chopLimit   = prefs.getFloat("chop",   P_SPY_CHOP);
        settings.isMuted     = prefs.getBool ("muted",  false);
        settings.backlightOn = prefs.getBool ("bl",     true);
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
    prefs.putString("sym",  settings.symbol);
    prefs.putUChar("ver",   19);
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
// SMART POLLING INTERVAL (seconds) — mirrors original logic
// =======================================================================================
unsigned long getSmartInterval() {
    int wd = getWeekday();
    if (wd == 0 || wd == 6) return 300; // Weekend
    int h = getHour(); int m = getMinute();
    if (h < 8 || (h == 8 && m < 30) || h >= 16) return 60;  // Off-hours
    if ((h == 9 && m >= 30) || h == 10 || h == 15) return 2; // High-freq open/close
    if (h == 12) return 10;
    return 4;
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
// MARKET ENGINE (EMA + PERCENTAGE VELOCITY) — logic identical to original
// =======================================================================================
void processPrice(float p) {
    lastPrice = p;

    if (!initialized) {
        emaFast = lastPrice;
        emaSlow = lastPrice;
        diff = 0;
        initialized = true;
        forceRedraw = true;
        return;
    }

    float prevDiff = diff;
    emaFast = (lastPrice * settings.alphaFast) + (emaFast * (1.0f - settings.alphaFast));
    emaSlow = (lastPrice * settings.alphaSlow) + (emaSlow * (1.0f - settings.alphaSlow));

    float rawDiff = emaFast - emaSlow;
    diff = (rawDiff / lastPrice) * 100.0f;

    velocityHistory[historyHead] = diff;
    historyHead = (historyHead + 1) % HISTORY_SIZE;

    // --- ALERT LOGIC (mirrors original exactly) ---
    if (fabs(diff) > settings.chopLimit) {
        if (diff > 0) {
            if (prevDiff <= settings.chopLimit) {
                bzState = BZ_BULL; bzTimer = millis();
                logEvent("BULL BREAK", diff);
                updateSignalLogic(diff, "BULL BREAK");
            } else if (diff > (prevDiff * 1.20f)) {
                bzState = BZ_STUTTER; bzTimer = millis();
                logEvent("BULL RUSH", diff);
                updateSignalLogic(diff, "BULL RUSH");
            }
        } else {
            if (prevDiff >= -settings.chopLimit) {
                bzState = BZ_BEAR; bzTimer = millis();
                logEvent("BEAR BREAK", diff);
                updateSignalLogic(diff, "BEAR BREAK");
            } else if (diff < (prevDiff * 1.20f)) {
                bzState = BZ_STUTTER; bzTimer = millis();
                logEvent("BEAR DUMP", diff);
                updateSignalLogic(diff, "BEAR DUMP");
            }
        }
    } else if (fabs(prevDiff) > settings.chopLimit) {
        bzState = BZ_BULL; bzTimer = millis(); // short blip on trend end
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
// CONFLUENCE SIGNAL ENGINE (logic identical to original)
// =======================================================================================
void updateSignalLogic(float currentMomentum, const char* alertType) {
    int h = getHour(); int m = getMinute();
    bool isLunchExit = (h == 12) || (h == 13 && m <= 30);

    if (strcmp(alertType, "TREND END") == 0) {
        closePosition();
        if (isLunchExit) {
            drawSignalAlert("EXIT", "LUNCH CHOP ZONE", C_YELLOW);
            bzState = BZ_BEAR; bzTimer = millis();
            forceRedraw = true;
        }
    }

    if (strcmp(alertType, "BULL BREAK") == 0 || strcmp(alertType, "BEAR BREAK") == 0) {
        currentSignal    = TRIGGERED;
        lastTriggerTime  = millis();
    }

    if (currentSignal == TRIGGERED && (millis() - lastTriggerTime < CONFIRMATION_WINDOW)) {
        if (strcmp(alertType, "BULL RUSH") == 0 && currentMomentum >= BULL_RUSH_THRESHOLD) {
            triggerBuySignal();
        } else if (strcmp(alertType, "BEAR DUMP") == 0 && currentMomentum <= BEAR_DUMP_THRESHOLD) {
            triggerSellSignal();
        }
    }

    if (currentSignal == TRIGGERED && (millis() - lastTriggerTime > CONFIRMATION_WINDOW)) {
        currentSignal = IDLE;
    }
}

void triggerBuySignal() {
    openLong();
    logEvent("BUY SIGNAL", diff);
    char subBuf[20];
    snprintf(subBuf, sizeof(subBuf), "LONG @ $%.2f", lastPrice);
    if (!settings.isMuted) {
        M5.Speaker.tone(1200, 150); delay(180);
        M5.Speaker.tone(1500, 300);
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
        M5.Speaker.tone(600, 150); delay(180);
        M5.Speaker.tone(400, 600);
    }
    drawSignalAlert("SELL", subBuf, C_RED);
    currentSignal = IDLE;
    forceRedraw = true;
}

// =======================================================================================
// AUDIO STATE MACHINE (non-blocking, uses M5.Speaker)
// =======================================================================================
void updateBuzzer() {
    if (settings.isMuted || bzState == BZ_IDLE) return;

    unsigned long el = millis() - bzTimer;

    switch (bzState) {
        case BZ_BULL:
            if (el == 0 || el < 5) M5.Speaker.tone(1000, DUR_BULLISH);
            if (el >= (unsigned long)DUR_BULLISH) bzState = BZ_IDLE;
            break;
        case BZ_BEAR:
            if (el == 0 || el < 5) M5.Speaker.tone(500, DUR_BEARISH);
            if (el >= (unsigned long)DUR_BEARISH) bzState = BZ_IDLE;
            break;
        case BZ_STUTTER:
            // Stutter pattern: three short 80ms beeps
            if (el < 80)        M5.Speaker.tone(1100, 80);
            else if (el < 160)  M5.Speaker.stop();
            else if (el < 240)  M5.Speaker.tone(1100, 80);
            else if (el < 320)  M5.Speaker.stop();
            else if (el < 400)  M5.Speaker.tone(1100, 80);
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

            case 'i': case 'I':
                showInfoScreen = !showInfoScreen;
                forceRedraw = true;
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

            case 'r': case 'R':
                ESP.restart();
                break;
        }
    }
}

// =======================================================================================
// TFT DISPLAY ENGINE
// 240x135 landscape — no header, data-first layout:
//
//  y  0..44  │ VELOCITY ZONE  │ big coloured value + BULL/BEAR/CHOP badge
//  y 45..45  │ divider
//  y 46..89  │ GRAPH ZONE     │ 44px tall velocity bar chart
//  y 90..90  │ divider
//  y 91..134 │ INFO BAR       │ SYM $price | POS Open Total | time | chop
// =======================================================================================

void drawFullScreen() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    drawVelocityZone();
    drawGraph();
    drawPriceBar();
    forceRedraw = false;
}

void drawInfoScreen() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    // Header
    M5Cardputer.Display.fillRect(0, 0, DISP_W, 20, C_BLUE);
    M5Cardputer.Display.setTextColor(C_WHITE);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(4, 3);
    M5Cardputer.Display.print("SYSTEM INFO");

    M5Cardputer.Display.setTextSize(1);
    int y = 28;
    int step = 15;

    // WiFi
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print("WIFI: ");
    M5Cardputer.Display.setTextColor(C_GREEN);
    M5Cardputer.Display.print(WiFi.SSID());
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.print("  RSSI: ");
    M5Cardputer.Display.setTextColor(C_WHITE);
    M5Cardputer.Display.print(WiFi.RSSI());
    M5Cardputer.Display.print("dBm");

    // IP
    y += step;
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print("IP:   ");
    M5Cardputer.Display.setTextColor(C_CYAN);
    M5Cardputer.Display.print(WiFi.localIP().toString());

    // Battery (M5Unified handles Fuel Gauge / ADC automatically)
    y += step;
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print("BATT: ");
    int batPct = M5.Power.getBatteryLevel();
    if (batPct >= 80) M5Cardputer.Display.setTextColor(C_GREEN);
    else if (batPct >= 20) M5Cardputer.Display.setTextColor(C_YELLOW);
    else M5Cardputer.Display.setTextColor(C_RED);
    M5Cardputer.Display.print(batPct);
    M5Cardputer.Display.print("%  ");
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.print("VOLTS: ");
    M5Cardputer.Display.setTextColor(C_WHITE);
    M5Cardputer.Display.print(M5.Power.getBatteryVoltage() / 1000.0f, 2);
    M5Cardputer.Display.print("V");

    // Heap
    y += step;
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print("HEAP: ");
    M5Cardputer.Display.setTextColor(C_WHITE);
    M5Cardputer.Display.print(ESP.getFreeHeap() / 1024);
    M5Cardputer.Display.print(" KB Free");

    // Uptime
    y += step;
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print("UPTIME: ");
    M5Cardputer.Display.setTextColor(C_WHITE);
    M5Cardputer.Display.print(millis() / 60000UL);
    M5Cardputer.Display.print(" mins");

    // Poll Interval (Market status)
    y += step;
    M5Cardputer.Display.setTextColor(C_GREY);
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print("POLLING: ");
    M5Cardputer.Display.setTextColor(C_YELLOW);
    M5Cardputer.Display.print(getSmartInterval());
    M5Cardputer.Display.print("s (");
    M5Cardputer.Display.setTextColor(C_WHITE);
    if (getSmartInterval() <= 4) M5Cardputer.Display.print("Active Market");
    else if (getSmartInterval() <= 60) M5Cardputer.Display.print("Off-Hours");
    else M5Cardputer.Display.print("Weekend");
    M5Cardputer.Display.print(")");

    // Footer (Centered)
    M5Cardputer.Display.setTextColor(C_DKGREY);
    M5Cardputer.Display.setCursor(63, 122); 
    M5Cardputer.Display.print("PRESS TO RETURN");

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
    else                                  { trendCol = C_CYAN;   trendStr = "CHOP"; }

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
    if      (currentPos == POS_LONG)  { posStr = "LONG";  posCol = C_GREEN; }
    else if (currentPos == POS_SHORT) { posStr = "SHORT"; posCol = C_RED;   }
    else                               { posStr = "FLAT";  posCol = C_DKGREY; }

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
    char opBuf[10]; snprintf(opBuf, sizeof(opBuf), "%+.2f", openPnL);
    M5Cardputer.Display.print(opBuf);

    // Total PnL
    M5Cardputer.Display.setTextColor(C_DKGREY);
    M5Cardputer.Display.setCursor(68, 116);
    M5Cardputer.Display.print("T:");
    M5Cardputer.Display.setTextColor(cpCol);
    char cpBuf[10]; snprintf(cpBuf, sizeof(cpBuf), "%+.2f", closedPnL);
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
// =======================================================================================
void handleWebTraffic() {
    WiFiClient client = webServer.accept();
    if (!client) return;

    unsigned long t0 = millis();
    while (!client.available() && (millis() - t0) < 500) delay(1);
    if (!client.available()) { client.stop(); return; }

    String req = client.readStringUntil('\n');
    while (client.available()) client.read(); // drain headers

    // Route
    if (req.indexOf("GET /data") != -1) {
        serveJSON(client);
    } else {
        // Parse any commands embedded in the GET URL
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
        if (req.indexOf("test=bull") != -1) { bzState = BZ_BULL;  bzTimer = millis(); logEvent("TEST BULL",  0.050f); }
        if (req.indexOf("test=bear") != -1) { bzState = BZ_BEAR;  bzTimer = millis(); logEvent("TEST BEAR", -0.050f); }
        if (req.indexOf("reboot=1")  != -1) { client.stop(); delay(200); ESP.restart(); }

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
    else                               client.print("SHORT");
    client.print("\"");

    client.print(",\"entry\":"); client.print(tradeEntryPrice, 2);
    client.print(",\"openPnl\":"); client.print(openPnL, 2);
    client.print(",\"closedPnl\":"); client.print(closedPnL, 2);
    client.print(",\"trades\":"); client.print(tradeCount);

    // Uptime in seconds
    client.print(",\"uptime\":"); client.print(millis() / 1000UL);

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
    // Send HTTP Headers
    client.print(F(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Connection: close\r\n"
        "\r\n"
    ));

    // Part 1: Head & CSS (Static)
    client.print(F(R"rawliteral(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<script src='https://cdn.jsdelivr.net/npm/chartjs-plugin-annotation'></script>
<style>
    body { font-family: 'Segoe UI', sans-serif; background: #121212; color: #eee; margin: 0; padding: 10px; }
    .container { max-width: 600px; margin: 0 auto; width: 100%; box-sizing: border-box; }
    .card { background: #1e1e1e; padding: 15px; border-radius: 8px; margin-bottom: 15px; border: 1px solid #333; }
    h2 { color: #00ff88; font-size: 16px; border-bottom: 1px solid #333; padding-bottom: 5px; margin-top: 0; }
    h3 { color: #fff; font-size: 14px; margin: 10px 0 5px 0; }
    p, li { font-size: 13px; color: #ccc; line-height: 1.4; margin: 5px 0; }
    .stat { font-size: 12px; color: #aaa; display: inline-block; margin-right: 15px; }
    .val { color: #fff; font-weight: bold; }
    button { width: 100%; padding: 12px; margin: 4px 0; border-radius: 5px; cursor: pointer; border: none; font-weight: bold; color: #fff; background: #333; }
    .gold { background: #FFD700 !important; color: #000 !important; border: 2px solid #fff; }
    input { width: 100%; padding: 10px; background: #2c2c2c; border: 1px solid #444; color: #fff; border-radius: 4px; box-sizing: border-box; }
    
    /* New Disclaimer Style */
    .disclaimer { background: #2a1111; border: 1px solid #f44; padding: 10px; border-radius: 5px; margin-top: 15px; font-size: 12px; }
    .disclaimer strong { color: #f44; display: block; margin-bottom: 5px; font-size: 14px; }
</style>
</head><body><div class='container'>
)rawliteral"));

    // Part 2: Vitals & Monitor (Dynamic variables injected via printf)
    client.printf(R"rawliteral(
    <div class='card'><h2>SYSTEM VITALS</h2>
        <div class='stat'>WiFi: <span class='val'>%ddBm</span></div>
        <div class='stat'>Heap: <span class='val'>%dkB</span></div>
        <div class='stat'>Uptime: <span class='val'>%lum</span></div>
        <div class='stat'>IP: <span class='val'>%s</span></div>
    </div>
    
    <div class='card'><h2>LIVE MONITOR</h2>
        <p>SYMBOL: <b id='symText' style='color:#FFD700'>%s</b> | PRICE: <b id='priceText'>$%.2f</b></p>
        <div style='position:relative;height:180px;width:100%'><canvas id='c'></canvas></div>
    </div>
)rawliteral", WiFi.RSSI(), (int)(ESP.getFreeHeap() / 1024), millis() / 60000UL, getLocalIPString().c_str(), settings.symbol, lastPrice);

    // Part 3: Trade Tracker & Alerts Structure (Static)
    client.print(F(R"rawliteral(
    <div class='card'><h2>TRADE TRACKER (PAPER PnL)</h2>
        <div class='stat'>Pos: <span class='val' id='posText'>NONE</span></div>
        <div class='stat'>Entry: <span class='val' id='entryText'>$0.00</span></div>
        <div class='stat'>Open PnL: <span class='val' id='openPnlText'>$0.00</span></div>
        <div class='stat'>Total PnL: <span class='val' id='closedPnlText'>$0.00</span></div>
        <div class='stat'>Trades: <span class='val' id='tradesText'>0</span></div>
    </div>
    
    <div class='card'><h2>RECENT ALERTS (LAST 5)</h2>
        <div id='alertBox' style='min-height:20px;color:#888;font-size:13px'>Waiting...</div>
    </div>
)rawliteral"));

    // Part 4: Presets, Expert Tuning & Admin (Dynamic)
    const char* clsSpy = (strcmp(settings.symbol, "SPY") == 0) ? "gold" : "";
    const char* clsQqq = (strcmp(settings.symbol, "QQQ") == 0) ? "gold" : "";
    const char* clsIwm = (strcmp(settings.symbol, "IWM") == 0) ? "gold" : "";
    const char* muteStateUrl = settings.isMuted ? "0" : "1";
    const char* muteStateLbl = settings.isMuted ? "UNMUTE AUDIO" : "MUTE AUDIO";

    client.printf(R"rawliteral(
    <div class='card'><h2>PRESETS</h2>
        <div style='display:flex;gap:10px'>
            <a href='/?sym=SPY' style='flex:1'><button class='%s'>SPY</button></a>
            <a href='/?sym=QQQ' style='flex:1'><button class='%s'>QQQ</button></a>
            <a href='/?sym=IWM' style='flex:1'><button class='%s'>IWM</button></a>
        </div>
    </div>

    <div class='card'><h2>EXPERT TUNING</h2>
        <form action='/' method='get'>
            <div class='stat'>Chop Limit (%%): </div>
            <div style='display:flex;gap:10px'>
                <input type='text' name='chop' value='%.3f'>
                <button style='width:auto;background:#00ff88;color:#000'>UPDATE</button>
            </div>
        </form>
    </div>

    <div class='card'><h2>ADMIN</h2>
        <a href='/?mute=%s'><button>%s</button></a>
        <div style='display:flex;gap:10px'>
            <a href='/?test=bull' style='flex:1'><button style='background:#0f8;color:#000'>TEST BULL</button></a>
            <a href='/?test=bear' style='flex:1'><button style='background:#f44'>TEST BEAR</button></a>
        </div>
        <a href='/?reboot=1'><button style='background:#555;margin-top:10px'>REBOOT DEVICE</button></a>
    </div>
)rawliteral", clsSpy, clsQqq, clsIwm, settings.chopLimit, muteStateUrl, muteStateLbl);

    // Part 5: About & Disclaimer (Dynamic IP at the bottom)
    client.printf(R"rawliteral(
    <div class='card'><h2>ABOUT SQUAWK BOX</h2>
        <h3>Concept & Creation</h3>
        <p>Designed and built by <strong>Jason Edgar</strong> in Orillia, Ontario.</p>
        <p>Running on <strong>M5Stack Cardputer ADV</strong> (ESP32-S3) with direct Finnhub REST polling.</p>
        
        <h3>Keyboard Shortcuts</h3>
        <p><b></b>ute | <b></b>acklight | <b></b>nfo | <b></b> Symbol | <b></b> Chop | <b></b>est Bull | <b></b> Test Bear | <b></b>eboot</p>
        
        <h3>Signal Logic</h3>
        <ul>
            <li><b style='color:#FFD700'>BUY:</b> Bull Break &rarr; Bull Rush (&ge;0.016%%) within 15s</li>
            <li><b style='color:#FFD700'>SELL:</b> Bear Break &rarr; Bear Dump (&le;-0.025%%) within 15s</li>
            <li><b style='color:#FFD700'>LUNCH EXIT:</b> Trend End between 12:00–1:30 PM EST</li>
        </ul>
        
        <div class='disclaimer'>
            <strong>⚠️ DISCLAIMER</strong>
            This software is for educational and informational purposes only. Do not use this as financial advice. 
            Squawk Box and its algorithmic signals are a programmatic interpretation of market momentum and do not guarantee profits or prevent losses. 
            Paper trading and simulated PnL are for entertainment/testing purposes. The author is not responsible for any financial losses incurred from using this software. Trade at your own risk.
        </div>
    </div>
    <div style='text-align:center;color:#555;font-size:11px;margin-bottom:20px;'>
        SQUAWK BOX v9.3 CARDPUTER | %s
    </div>
</div>
)rawliteral", getLocalIPString().c_str());

// Part 6: JavaScript
    // Pass our C++ settings to JS variables cleanly at the top of the script
    client.print(F("<script>\n"));
    client.printf("const chop = %s;\n", String(settings.chopLimit, 4).c_str());
    client.printf("const historySize = %d;\n", HISTORY_SIZE);
    
    // The rest of the JS is totally static, so we can pass it via a raw literal
    client.print(F(R"rawliteral(
    let chart;
    function update() {
        fetch('/data').then(r => r.json()).then(j => {
            document.getElementById('priceText').innerText = '$' + j.price.toFixed(2);
            
            const pc = j.pos === 'LONG' ? '#0f8' : j.pos === 'SHORT' ? '#f44' : '#fff';
            document.getElementById('posText').innerText = j.pos;
            document.getElementById('posText').style.color = pc;
            
            document.getElementById('entryText').innerText = '$' + j.entry.toFixed(2);
            document.getElementById('openPnlText').innerText = '$' + j.openPnl.toFixed(2);
            document.getElementById('openPnlText').style.color = j.openPnl >= 0 ? '#0f8' : '#f44';
            document.getElementById('closedPnlText').innerText = '$' + j.closedPnl.toFixed(2);
            document.getElementById('closedPnlText').style.color = j.closedPnl >= 0 ? '#0f8' : '#f44';
            document.getElementById('tradesText').innerText = j.trades;
            
            let h = '';
            if (!j.alerts || j.alerts.length === 0) {
                h = 'No alerts yet...';
            } else {
                for (let a of j.alerts) {
                    let c = '#fff';
                    if (a.e.includes('BULL')) c = '#0f8';
                    if (a.e.includes('BEAR')) c = '#f44';
                    if (a.e.includes('SIGNAL')) c = '#FFD700';
                    
                    h += `<div style='border-bottom:1px solid #333;padding:5px 0;font-family:monospace;display:flex;justify-content:space-between'>
                            <span><span style='color:#666;margin-right:10px'>${a.t}</span>
                            <span style='color:${c};font-weight:bold'>${a.e}</span></span>
                            <span style='color:#eee'>${a.v.toFixed(3)}%</span>
                          </div>`;
                }
            }
            document.getElementById('alertBox').innerHTML = h;
            
            // FIXED: Added index to target the correct dataset
            chart.data.datasets.data = j.history;
            let col = '#0ff';
            if (j.diff > chop) col = '#0f8';
            else if (j.diff < -chop) col = '#f44';
            chart.data.datasets.borderColor = col;
            chart.update();
        }).catch(e => console.log(e));
    }

    window.onload = () => {
        const ctx = document.getElementById('c').getContext('2d');
        chart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: Array(historySize).fill(''),
                // FIXED: Restored the array brackets for the dataset
                datasets:, borderColor: '#0ff', pointRadius: 0, tension: 0.3, fill: false }]
            },
            options: {
                maintainAspectRatio: false, responsive: true,
                plugins: {
                    legend: { display: false },
                    annotation: {
                        annotations: {
                            zero: { type: 'line', yMin: 0, yMax: 0, borderColor: '#FFD700', borderWidth: 2 },
                            // FIXED: Restored the array for the dashed lines
                            bull: { type: 'line', yMin: chop, yMax: chop, borderColor: '#0f8', borderDash: },
                            bear: { type: 'line', yMin: -chop, yMax: -chop, borderColor: '#f44', borderDash: }
                        }
                    }
                },
                scales: { y: { grid: { color: '#333' } }, x: { display: false } }
            }
        });
        setInterval(update, 3000); 
        update();
    };
    </script></body></html>
)rawliteral"));
}

// =======================================================================================
// SETUP
// =======================================================================================
void setup() {
    Serial.begin(115200);

    // Init M5Cardputer (display, speaker, keyboard)
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);           // Landscape: 240 wide x 135 tall
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
    WiFi.setHostname(creds.hostname);
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
// =======================================================================================
void loop() {
    M5Cardputer.update(); // Required for keyboard scan

    handleKeyboard();
    handleWebTraffic();
    updateBuzzer();

    // Redraw full screen if flagged
    if (forceRedraw) {
        if (showInfoScreen) {
            drawInfoScreen();
        } else {
            drawFullScreen();
        }
    }

    // Smart polling for Finnhub
    unsigned long interval = getSmartInterval() * 1000UL;
    if (millis() - lastPollMs > interval) {
        lastPollMs = millis();
        fetchQuote();
    }

    delay(10); // Yield to WiFi stack
}
