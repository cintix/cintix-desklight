#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <math.h>

#include "config.h"
#include "Lighting.h"

// ---------------------------------------------------------------------------
// Hidden implementation: LED driver, effect state and persistence.
// Nothing here is visible outside the Lighting module.
// ---------------------------------------------------------------------------
namespace {

const char CONFIG_PATH[] = "/config.json";

enum class Mode : uint8_t { Off = 0, Solid, Rainbow, Beat };

const char* modeName(Mode mode) {
    switch (mode) {
        case Mode::Off:     return "off";
        case Mode::Solid:   return "solid";
        case Mode::Rainbow: return "rainbow";
        case Mode::Beat:    return "beat";
    }
    return "solid";
}

Mode modeFromName(const String& name) {
    if (name == "off") return Mode::Off;
    if (name == "rainbow") return Mode::Rainbow;
    if (name == "beat") return Mode::Beat;
    return Mode::Solid;
}

uint32_t rgbToUint32(CRGB c) {
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

String colorHex(CRGB c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return String(buf);
}

bool parseHexColor(const String& s, CRGB& out) {
    String h = s;
    h.trim();
    if (h.startsWith("#")) h = h.substring(1);
    if (h.length() != 6) return false;
    for (size_t i = 0; i < h.length(); i++) {
        if (!isxdigit(h.charAt(i))) return false;
    }
    const long v = strtol(h.c_str(), nullptr, 16);
    out = CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl - everything the module needs to remember.
// ---------------------------------------------------------------------------
struct Lighting::Impl {
    CRGB*   leds = nullptr;
    Mode    mode = Mode::Solid;
    CRGB    color = CRGB(0x6a, 0x0a, 0x7f);  // default purple
    uint8_t brightnessPct = 80;              // 0..100
    uint8_t bpm = 124;                       // beats per minute
    uint8_t hue = 0;

    uint32_t lastFrameMs = 0;

    bool     configDirty = false;
    uint32_t lastChangeMs = 0;
    uint32_t lastKickMs = 0;      // beat effect timing

    void markDirty() {
        configDirty = true;
        lastChangeMs = millis();
    }
};

Lighting::Lighting() : impl_(new Impl) {}

void Lighting::begin() {
    Impl* m = impl_;
    m->leds = new CRGB[LED_COUNT];
    FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(m->leds, LED_COUNT);
    FastLED.setBrightness(255);
    FastLED.clear();
    FastLED.show();

    loadConfig();

    // The light is on from the moment the firmware boots; it does not
    // wait for the network. (An Off mode from the persisted config
    // keeps the strip dark, as the user last chose.)
    render();
}

// ---------------------------------------------------------------------------
// Persistence (owned by this module - /config.json is lighting state).
// ---------------------------------------------------------------------------
void Lighting::loadConfig() {
    Impl* m = impl_;
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

    m->mode = modeFromName(doc["mode"] | "solid");
    CRGB c;
    if (parseHexColor(doc["color"] | "#6a0a7f", c)) m->color = c;
    m->brightnessPct = (uint8_t)constrain((int)(doc["brightness"] | 80), 0, 100);
    m->bpm = (uint8_t)constrain((int)(doc["bpm"] | 124), 60, 200);

    Serial.printf("Config loaded: mode=%s color=%s brightness=%d bpm=%d\n",
                  modeName(m->mode), colorHex(m->color).c_str(),
                  m->brightnessPct, m->bpm);
}

void Lighting::saveConfig() {
    Impl* m = impl_;
    JsonDocument doc;
    doc["mode"] = modeName(m->mode);
    doc["color"] = colorHex(m->color);
    doc["brightness"] = m->brightnessPct;
    doc["bpm"] = m->bpm;

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

// ---------------------------------------------------------------------------
// Effect rendering.
// ---------------------------------------------------------------------------
void Lighting::renderBeat(uint32_t now) {
    Impl* m = impl_;
    const uint32_t intervalMs = 60000u / m->bpm;  // ms per quarter note

    if (m->lastKickMs == 0) m->lastKickMs = now;
    // Fast-forward if we missed beats (e.g. a slow HTTP request).
    while (now - m->lastKickMs >= intervalMs) m->lastKickMs += intervalMs;

    const float p = (float)(now - m->lastKickMs) / (float)intervalMs;  // 0..1

    // Kick: sharp attack with exponential decay.
    const float kick = expf(-p * 6.0f);
    // Off-beat accent at p ~ 0.5 for a pumping, dance-floor feel.
    const float off = 0.55f * expf(-powf((p - 0.5f) * 8.0f, 2.0f));
    const float env = fmaxf(kick, off);
    const float level = 0.05f + 0.95f * env;  // a faint glow between beats

    FastLED.setBrightness((uint8_t)(brightness255() * level));
    fill_solid(m->leds, LED_COUNT, m->color);
    FastLED.show();
}

void Lighting::render() {
    Impl* m = impl_;
    switch (m->mode) {
        case Mode::Off:
            FastLED.clear();
            FastLED.show();
            break;
        case Mode::Solid:
            FastLED.setBrightness(brightness255());
            fill_solid(m->leds, LED_COUNT, m->color);
            FastLED.show();
            break;
        case Mode::Rainbow:
            m->hue += 2;  // advance the rainbow slowly
            FastLED.setBrightness(brightness255());
            fill_rainbow(m->leds, LED_COUNT, m->hue, 12);
            FastLED.show();
            break;
        case Mode::Beat:
            renderBeat(millis());
            break;
    }
}

uint8_t Lighting::brightness255() const {
    return (uint16_t)impl_->brightnessPct * 255u / 100u;
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------
void Lighting::update(uint32_t nowMs) {
    Impl* m = impl_;

    // Persist shortly after the last change (debounced to limit flash wear).
    if (m->configDirty && nowMs - m->lastChangeMs >= 1000) {
        m->configDirty = false;
        saveConfig();
    }

    if (nowMs - m->lastFrameMs >= 16) {  // ~60 fps
        m->lastFrameMs = nowMs;
        render();
    }
}

void Lighting::setMode(const char* mode) {
    Impl* m = impl_;
    const Mode next = modeFromName(mode);
    if (next == m->mode) return;
    m->mode = next;
    m->markDirty();
    if (next == Mode::Off) {  // power off clears the strip immediately
        FastLED.clear();
        FastLED.show();
    }
}

void Lighting::setColor(uint32_t rgb) {
    Impl* m = impl_;
    CRGB c((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    if (c.r == m->color.r && c.g == m->color.g && c.b == m->color.b) return;
    m->color = c;
    m->markDirty();
}

void Lighting::setBrightness(uint8_t pct) {
    Impl* m = impl_;
    const uint8_t b = (uint8_t)constrain((int)pct, 0, 100);
    if (b == m->brightnessPct) return;
    m->brightnessPct = b;
    m->markDirty();
}

void Lighting::setBpm(uint8_t bpm) {
    Impl* m = impl_;
    const uint8_t b = (uint8_t)constrain((int)bpm, 60, 200);
    if (b == m->bpm) return;
    m->bpm = b;
    m->markDirty();
}

LightingState Lighting::state() const {
    Impl* m = impl_;
    LightingState s;
    s.mode = modeName(m->mode);
    s.color = rgbToUint32(m->color);
    s.brightness = m->brightnessPct;
    s.bpm = m->bpm;
    return s;
}
