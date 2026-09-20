#include <Arduino.h>
#include <WebServer.h>
#include <LittleFS.h>

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
    s.on("/api/control", HTTP_POST, [this] { handleControl(); });
    s.onNotFound([this] { handleNotFound(); });
    s.begin();
}

void Web::handle() {
    impl_->server->handleClient();
}
