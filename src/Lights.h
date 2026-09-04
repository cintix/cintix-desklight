#ifndef Lights_h
#define Lights_h

#include <FastLED.h>
#include "config.h"

// Low-level NeoPixel (WS2812B) strip driver.
// Pixel type/pin/count come from include/config.h (LED_DATA_PIN, LED_COUNT).
class Lights {
   public:
    void begin();

    void clear();  // fill the whole strip with black
    void show();

    void setBrightness(uint8_t value);
    void fillSolid(CRGB color);
    void fillRainbow(uint8_t startHue, uint8_t deltaHue);

   private:
    CRGB leds[LED_COUNT];
};

#endif  // Lights_h
