#include <Arduino.h>
#include <HWCDC.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <math.h>

#include "config.h"
#include "Lights.h"

// ---------------------------------------------------------------------------
// Runtime state. Persisted to /config.json on LittleFS so color, on/off and
// effect settings survive reboots. The web UI (index.html, style.css, app.js)
// is served from LittleFS too; this file only contains endpoints + logic.
// ---------------------------------------------------------------------------
enum class Mode : uint8_t { Off = 0, Solid, Rainbow, Beat };

static Mode gMode = Mode::Solid;
static CRGB gColor = CRGB(0x6a, 0x0a, 0x7f);  // default purple
static uint8_t gBrightnessPct = 80;           // 0..100
static uint8_t gBpm = 124;                    // beats per minute (dance tempo)

static Lights gLights;
static WebServer gServer(80);

static bool gApMode = false;
static uint32_t gLastFrameMs = 0;
static uint32_t gLastReconnectMs = 0;
static uint8_t gHue = 0;

static bool gConfigDirty = false;
static uint32_t gLastChangeMs = 0;

static const char CONFIG_PATH[] = "/config.json";

// ---------------------------------------------------------------------------
// Mode / color helpers
// ---------------------------------------------------------------------------
static Mode modeFromName(const String& name) {
    if (name == "off") return Mode::Off;
    if (name == "rainbow") return Mode::Rainbow;
    if (name == "beat") return Mode::Beat;
    return Mode::Solid;
}

static const char* modeName(Mode mode) {
    switch (mode) {
        case Mode::Off:     return "off";
        case Mode::Solid:   return "solid";
        case Mode::Rainbow: return "rainbow";
        case Mode::Beat:    return "beat";
    }
    return "solid";
}

static uint8_t brightness255() {
    return (uint16_t)gBrightnessPct * 255u / 100u;
}

static String colorHex() {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", gColor.r, gColor.g, gColor.b);
    return String(buf);
}

static bool parseHexColor(const String& s, CRGB& out) {
    String h = s;
    h.trim();
    if (h.startsWith("#")) h = h.substring(1);
    if (h.length() != 6) return false;
    for (size_t i = 0; i < h.length(); i++) {
        if (!isxdigit(h.charAt(i))) return false;
    }
    long v = strtol(h.c_str(), nullptr, 16);
    out = CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

static String stateJson() {
    return String("{") +
           "\"ip\":\"" + (gApMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"," +
           "\"ap\":" + (gApMode ? "true" : "false") + "," +
           "\"mode\":\"" + modeName(gMode) + "\"," +
           "\"color\":\"" + colorHex() + "\"," +
           "\"brightness\":" + String(gBrightnessPct) + "," +
           "\"bpm\":" + String(gBpm) + "}";
}

// ---------------------------------------------------------------------------
// Config persistence (LittleFS)
// ---------------------------------------------------------------------------
static void saveConfig() {
    JsonDocument doc;
    doc["mode"] = modeName(gMode);
    doc["color"] = colorHex();
    doc["brightness"] = gBrightnessPct;
    doc["bpm"] = gBpm;

    String out;
    serializeJson(doc, out);

    File file = LittleFS.open(CONFIG_PATH, "w");
    if (!file) {
        Serial.println("Failed to open config.json for writing");
        return;
    }
    file.print(out);
    file.close();
    Serial.printf("Config saved: %s\n", out.c_str());
}

static void loadConfig() {
    if (!LittleFS.exists(CONFIG_PATH)) {
        Serial.println("No config.json found; using defaults");
        return;
    }
    File file = LittleFS.open(CONFIG_PATH, "r");
    if (!file) return;

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.printf("Failed to parse config.json: %s\n", err.c_str());
        return;
    }

    gMode = modeFromName(doc["mode"] | "solid");
    CRGB c;
    if (parseHexColor(doc["color"] | "#6a0a7f", c)) gColor = c;
    gBrightnessPct = (uint8_t)constrain((int)(doc["brightness"] | 80), 0, 100);
    gBpm = (uint8_t)constrain((int)(doc["bpm"] | 124), 60, 200);

    Serial.printf("Config loaded: mode=%s color=%s brightness=%d bpm=%d\n",
                  modeName(gMode), colorHex().c_str(), gBrightnessPct, gBpm);
}

static void markConfigDirty() {
    gConfigDirty = true;
    gLastChangeMs = millis();
}

// ---------------------------------------------------------------------------
// HTTP endpoints
// ---------------------------------------------------------------------------
static String contentTypeFor(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".ico")) return "image/x-icon";
    return "text/plain";
}

static bool serveFile(const String& path) {
    if (!LittleFS.exists(path)) return false;
    File file = LittleFS.open(path, "r");
    if (!file) return false;
    gServer.streamFile(file, contentTypeFor(path));
    file.close();
    return true;
}

static void handleRoot() {
    if (!serveFile("/index.html")) {
        gServer.send(404, "text/plain",
                     "404: index.html not found (upload filesystem: pio run -t uploadfs)");
    }
}

static void handleState() {
    gServer.send(200, "application/json", stateJson());
}

static void handleControl() {
    bool changed = false;

    if (gServer.hasArg("mode")) {
        const Mode m = modeFromName(gServer.arg("mode"));
        if (m != gMode) {
            gMode = m;
            changed = true;
        }
    }
    if (gServer.hasArg("color")) {
        CRGB c;
        if (parseHexColor(gServer.arg("color"), c) &&
            (c.r != gColor.r || c.g != gColor.g || c.b != gColor.b)) {
            gColor = c;
            changed = true;
        }
    }
    if (gServer.hasArg("brightness")) {
        const uint8_t b = (uint8_t)constrain(gServer.arg("brightness").toInt(), 0, 100);
        if (b != gBrightnessPct) {
            gBrightnessPct = b;
            changed = true;
        }
    }
    if (gServer.hasArg("bpm")) {
        const uint8_t bpm = (uint8_t)constrain(gServer.arg("bpm").toInt(), 60, 200);
        if (bpm != gBpm) {
            gBpm = bpm;
            changed = true;
        }
    }

    if (changed) markConfigDirty();
    if (gMode == Mode::Off) {  // power off clears the strip immediately
        gLights.clear();
        gLights.show();
    }
    gServer.send(200, "application/json", stateJson());
}

static void handleNotFound() {
    const String uri = gServer.uri();
    // Generic static files from LittleFS; never expose the config file.
    if (uri != CONFIG_PATH && serveFile(uri)) return;
    gServer.send(404, "text/plain", "404: Not Found");
}

// ---------------------------------------------------------------------------
// Effect rendering (called ~60 times per second from loop())
// ---------------------------------------------------------------------------
static void renderBeat(uint32_t now) {
    const uint32_t intervalMs = 60000u / gBpm;  // ms per quarter note

    static uint32_t sLastKickMs = 0;
    if (sLastKickMs == 0) sLastKickMs = now;
    // Fast-forward if we missed beats (e.g. a slow HTTP request), keep the beat.
    while (now - sLastKickMs >= intervalMs) sLastKickMs += intervalMs;

    const float p = (float)(now - sLastKickMs) / (float)intervalMs;  // 0..1

    // Kick: sharp attack with exponential decay.
    const float kick = expf(-p * 6.0f);
    // Off-beat accent at p ~ 0.5 for a pumping, dance-floor feel.
    const float off = 0.55f * expf(-powf((p - 0.5f) * 8.0f, 2.0f));
    const float env = fmaxf(kick, off);
    const float level = 0.05f + 0.95f * env;  // a faint glow between beats

    gLights.setBrightness((uint8_t)(brightness255() * level));
    gLights.fillSolid(gColor);
    gLights.show();
}

static void renderFrame(uint32_t now) {
    switch (gMode) {
        case Mode::Off:
            gLights.clear();
            gLights.show();
            break;
        case Mode::Solid:
            gLights.setBrightness(brightness255());
            gLights.fillSolid(gColor);
            gLights.show();
            break;
        case Mode::Rainbow:
            gHue += 2;  // advance the rainbow slowly
            gLights.setBrightness(brightness255());
            gLights.fillRainbow(gHue, 12);
            gLights.show();
            break;
        case Mode::Beat:
            renderBeat(now);
            break;
    }
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
static void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("Connecting to WiFi \"%s\"", WIFI_SSID);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        gApMode = false;
        Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        // Fallback: expose the same web UI as its own access point.
        gApMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Serial.printf("\nWiFi failed. Access point \"%s\" started. IP: %s\n",
                      AP_SSID, WiFi.softAPIP().toString().c_str());
    }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\nDeskLight booting...");

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
    }
    loadConfig();

    gLights.begin();
    gLights.setBrightness(brightness255());
    if (gMode == Mode::Off) {
        gLights.clear();
    } else {
        gLights.fillSolid(gColor);
    }
    gLights.show();

    connectWiFi();

    gServer.on("/", handleRoot);
    gServer.on("/index.html", handleRoot);
    gServer.on("/api/state", HTTP_GET, handleState);
    gServer.on("/api/control", HTTP_POST, handleControl);
    gServer.onNotFound(handleNotFound);
    gServer.begin();

    if (!gApMode && MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS: http://%s.local\n", HOSTNAME);
    }

    const IPAddress ip = gApMode ? WiFi.softAPIP() : WiFi.localIP();
    Serial.printf("Open the web UI at http://%s\n", ip.toString().c_str());
}

void loop() {
    gServer.handleClient();

    // Persist settings shortly after the last change (debounced to limit
    // flash wear while dragging sliders).
    if (gConfigDirty && millis() - gLastChangeMs >= 1000) {
        gConfigDirty = false;
        saveConfig();
    }

    // Reconnect to the router if the link drops.
    if (!gApMode && WiFi.status() != WL_CONNECTED &&
        millis() - gLastReconnectMs > 10000) {
        gLastReconnectMs = millis();
        WiFi.reconnect();
    }

    const uint32_t now = millis();
    if (now - gLastFrameMs >= 16) {  // ~60 fps
        gLastFrameMs = now;
        renderFrame(now);
    }
}
