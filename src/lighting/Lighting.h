// Lighting module - owns the LED strip: the active effect, its settings and
// the persistence of those settings to /config.json.
//
// Public contract. Other modules may only use what is declared here; the
// FastLED driver, effect math and storage details stay hidden in Lighting.cpp.
#ifndef Lighting_h
#define Lighting_h

#include <stdint.h>

// Boundary state snapshot consumed by other modules (e.g. the web UI/API).
struct LightingState {
    const char* mode;       // "off" | "solid" | "rainbow" | "beat"
    uint32_t    color;      // 0xRRGGBB
    uint8_t     brightness; // 0..100 (%)
    uint8_t     bpm;        // 60..200 (beats per minute)
};

class Lighting {
public:
    Lighting();

    void begin();                 // initialise driver, restore persisted settings
    void setOnline(bool online);  // LEDs only run while online (indicator)
    void update(uint32_t nowMs);  // render + debounced save (~60x/sec)

    // Controls. Values are validated/clamped inside the module.
    void setMode(const char* mode);    // "off" | "solid" | "rainbow" | "beat"
    void setColor(uint32_t rgb);       // 0xRRGGBB
    void setBrightness(uint8_t pct);   // 0..100
    void setBpm(uint8_t bpm);          // clamped to 60..200

    LightingState state() const;

private:
    struct Impl;  // hidden implementation (Lighting.cpp)
    Impl* impl_;

    // Internal implementation (Lighting.cpp).
    void loadConfig();
    void saveConfig();
    void render();
    void renderBeat(uint32_t now);
    uint8_t brightness255() const;
};
#endif
