#include <Arduino.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <esp_system.h>

#include "host/Host.h"
#include "lighting/Lighting.h"
#include "network/Network.h"
#include "Web.h"

// ---------------------------------------------------------------------------
// Hidden implementation.
// ---------------------------------------------------------------------------
struct Web::Impl {
    WebServer* server = nullptr;
    Lighting*  lighting = nullptr;
    Network*   network = nullptr;
    Host*      host = nullptr;
};

Web::Web() : impl_(new Impl) {}

// ---------------------------------------------------------------------------
// JSON / HTTP helpers (transport boundary only - no business logic here).
// ---------------------------------------------------------------------------
static String colorHex(uint32_t rgb) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x",
             (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return String(buf);
}

bool Web::parseHexColor(const String& s, uint32_t& rgb) {
    String h = s;
    h.trim();
    if (h.startsWith("#")) h = h.substring(1);
    if (h.length() != 6) return false;
    for (size_t i = 0; i < h.length(); i++) {
        if (!isxdigit(h.charAt(i))) return false;
    }
    const long v = strtol(h.c_str(), nullptr, 16);
    rgb = (uint32_t)v;
    return true;
}

// Accepts the spellings a checkbox round-trips as well as the obvious ones;
// toInt() would read "true" as 0.
bool Web::parseBool(const String& s) {
    String v = s;
    v.trim();
    v.toLowerCase();
    return v == "true" || v == "1" || v == "on";
}

// Diagnostics.
static const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

String Web::contentTypeFor(const String& path) const {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".ico")) return "image/x-icon";
    return "text/plain";
}

bool Web::serveFile(const String& path) {
    if (!LittleFS.exists(path)) return false;
    File file = LittleFS.open(path, "r");
    if (!file) return false;
    impl_->server->streamFile(file, contentTypeFor(path));
    file.close();
    return true;
}

String Web::stateJson() const {
    const LightingState s = impl_->lighting->state();
    const String ip = impl_->network->isOnline()
                          ? impl_->network->localIp().toString()
                          : impl_->network->apIp().toString();
    return String("{") +
           "\"ip\":\"" + ip + "\"," +
           "\"ap\":" + (impl_->network->isOnline() ? "false" : "true") + "," +
           "\"host\":" + (impl_->host->isPresent() ? "true" : "false") + "," +
           "\"alwaysOn\":" + (s.alwaysOn ? "true" : "false") + "," +
           "\"mode\":\"" + s.mode + "\"," +
           "\"color\":\"" + colorHex(s.color) + "\"," +
           "\"brightness\":" + String(s.brightness) + "," +
           "\"bpm\":" + String(s.bpm) + "}";
}

// ---------------------------------------------------------------------------
// Request handlers.
// ---------------------------------------------------------------------------
void Web::handleRoot() {
    if (!serveFile("/index.html")) {
        impl_->server->send(404, "text/plain",
                            "404: index.html not found (upload filesystem: "
                            "pio run -t uploadfs)");
    }
}

void Web::handleState() {
    impl_->server->send(200, "application/json", stateJson());
}

// Raw USB signals plus the gating inputs, so it can be seen whether the strip
// *should* be able to light. Served over WiFi on purpose: the serial link is
// exactly what is in doubt when host detection misbehaves.
void Web::handleDebug() {
    const bool online = impl_->network->isOnline();
    const bool host = impl_->host->isPresent();
    const LightingState s = impl_->lighting->state();
    const bool shouldRun = online && (host || s.alwaysOn);

    const bool configExists = LittleFS.exists("/config.json");
    size_t configSize = 0;
    if (configExists) {
        File file = LittleFS.open("/config.json", "r");
        if (file) {
            configSize = file.size();
            file.close();
        }
    }

    char rawHex[16];
    snprintf(rawHex, sizeof(rawHex), "0x%08x", (unsigned)impl_->host->usbIntRaw());
    char enaHex[16];
    snprintf(enaHex, sizeof(enaHex), "0x%08x", (unsigned)impl_->host->usbIntEna());

    String out = "{";
    out += "\"uptime_ms\":" + String(millis()) + ",";
    out += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    out += "\"reset_reason\":\"" +
           String(resetReasonName(esp_reset_reason())) + "\",";
    out += "\"host\":{";
    out += "\"present\":" + String(host ? "true" : "false") + ",";
    out += "\"plugged_now\":" +
           String(impl_->host->pluggedNow() ? "true" : "false") + ",";
    out += "\"sof_index\":" + String(impl_->host->sofIndex()) + ",";
    out += "\"sof_moving\":" +
           String(impl_->host->sofMoving() ? "true" : "false") + ",";
    out += "\"int_raw\":\"" + String(rawHex) + "\",";
    out += "\"int_ena\":\"" + String(enaHex) + "\"";
    out += "},";
    out += "\"gate\":{";
    out += "\"online\":" + String(online ? "true" : "false") + ",";
    out += "\"host_present\":" + String(host ? "true" : "false") + ",";
    out += "\"always_on\":" + String(s.alwaysOn ? "true" : "false") + ",";
    out += "\"should_run\":" + String(shouldRun ? "true" : "false");
    out += "},";
    out += "\"config\":{";
    out += "\"exists\":" + String(configExists ? "true" : "false") + ",";
    out += "\"size\":" + String((unsigned)configSize);
    out += "}";
    out += "}";

    impl_->server->send(200, "application/json", out);
}

void Web::handleControl() {
    WebServer& s = *impl_->server;
    Lighting&  l = *impl_->lighting;

    if (s.hasArg("mode")) {
        l.setMode(s.arg("mode").c_str());
    }
    if (s.hasArg("color")) {
        uint32_t rgb;
        if (parseHexColor(s.arg("color"), rgb)) {
            l.setColor(rgb);
        }
    }
    if (s.hasArg("brightness")) {
        l.setBrightness((uint8_t)s.arg("brightness").toInt());
    }
    if (s.hasArg("bpm")) {
        l.setBpm((uint8_t)s.arg("bpm").toInt());
    }
    if (s.hasArg("alwaysOn")) {
        l.setAlwaysOn(parseBool(s.arg("alwaysOn")));
    }

    // Diagnostics: show what the UI actually sent. The cheapest way to tell a
    // UI that is not sending apart from a firmware that is not saving.
    Serial.printf("Control: mode='%s' color='%s' brightness='%s' bpm='%s' "
                  "alwaysOn='%s'\n",
                  s.arg("mode").c_str(), s.arg("color").c_str(),
                  s.arg("brightness").c_str(), s.arg("bpm").c_str(),
                  s.arg("alwaysOn").c_str());

    s.send(200, "application/json", stateJson());
}

void Web::handleNotFound() {
    const String uri = impl_->server->uri();
    // Generic static files from LittleFS; never expose the lighting config
    // file (owned by the Lighting module).
    if (uri != "/config.json" && serveFile(uri)) return;
    impl_->server->send(404, "text/plain", "404: Not Found");
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------
void Web::begin(Lighting& lights, Network& network, Host& host) {
    impl_->lighting = &lights;
    impl_->network = &network;
    impl_->host = &host;
    impl_->server = new WebServer(80);

    WebServer& s = *impl_->server;
    s.on("/", HTTP_GET, [this] { handleRoot(); });
    s.on("/index.html", HTTP_GET, [this] { handleRoot(); });
    s.on("/api/state", HTTP_GET, [this] { handleState(); });
    s.on("/api/debug", HTTP_GET, [this] { handleDebug(); });
    s.on("/api/control", HTTP_POST, [this] { handleControl(); });
    s.onNotFound([this] { handleNotFound(); });
    s.begin();
}

void Web::handle() {
    impl_->server->handleClient();
}
