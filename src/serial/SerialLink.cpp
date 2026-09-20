#include <Arduino.h>
#include <ArduinoJson.h>

#include "SerialLink.h"
#include "lighting/Lighting.h"

// ---------------------------------------------------------------------------
// Protocol + hidden implementation.
//
// Newline-delimited JSON over the USB CDC serial port:
//   host -> lamp: {"get":true}                       request current state
//   host -> lamp: {"mode":"solid","color":"#ff0000", ...}  set any fields
//   lamp -> host: {"mode":...,"color":"#rrggbb",
//                  "brightness":...,"bpm":...}       reply after each cmd
// ---------------------------------------------------------------------------
namespace {

const size_t SERIAL_LINE_MAX = 160;

String colorHex(uint32_t rgb) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x",
             (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return String(buf);
}

bool parseHexColor(const String& s, uint32_t& rgb) {
    String h = s;
    h.trim();
    if (h.startsWith("#")) h = h.substring(1);
    if (h.length() != 6) return false;
    for (size_t i = 0; i < h.length(); i++) {
        if (!isxdigit(h.charAt(i))) return false;
    }
    rgb = (uint32_t)strtol(h.c_str(), nullptr, 16);
    return true;
}

}  // namespace

struct SerialLink::Impl {
    Lighting* lights = nullptr;
    char      line[SERIAL_LINE_MAX];
    size_t    lineLen = 0;
    bool      discardLine = false;
};

SerialLink::SerialLink() : impl_(new Impl) {}

void SerialLink::begin(Lighting& lights) {
    impl_->lights = &lights;
}

// Report the current lighting state as a single JSON line.
void SerialLink::sendState() {
    const LightingState s = impl_->lights->state();
    String out = String("{\"mode\":\"") + s.mode +
                 "\",\"color\":\"" + colorHex(s.color) +
                 "\",\"brightness\":" + String(s.brightness) +
                 ",\"bpm\":" + String(s.bpm) + "}";
    Serial.println(out);
}

void SerialLink::handleLine(const char* line) {
    Lighting& lights = *impl_->lights;

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, line);
    if (err) {
        Serial.println("{\"error\":\"bad json\"}");
        return;
    }

    if (doc["get"].as<bool>()) {  // host asks for the current state
        sendState();
        return;
    }

    // Apply whichever fields are present; keys match the web UI/API exactly.
    if (doc["mode"].is<const char*>()) {
        lights.setMode(doc["mode"].as<const char*>());
    }
    if (doc["color"].is<const char*>()) {
        uint32_t rgb;
        if (parseHexColor(doc["color"].as<const char*>(), rgb)) {
            lights.setColor(rgb);
        }
    }
    if (doc["brightness"].is<int>()) {
        lights.setBrightness((uint8_t)doc["brightness"].as<int>());
    }
    if (doc["bpm"].is<int>()) {
        lights.setBpm((uint8_t)doc["bpm"].as<int>());
    }

    // Always answer with the (new) state: the host uses the reply as its ack
    // and keeps its cache in sync with the persisted settings.
    sendState();
}

void SerialLink::update() {
    Impl* m = impl_;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n') {
            if (!m->discardLine && m->lineLen > 0) {
                m->line[m->lineLen] = '\0';
                handleLine(m->line);
            }
            m->lineLen = 0;
            m->discardLine = false;
        } else if (!m->discardLine && c != '\r') {
            if (m->lineLen < SERIAL_LINE_MAX - 1) {
                m->line[m->lineLen++] = c;
            } else {
                m->discardLine = true;  // overlong line: drop until newline
            }
        }
    }
}
