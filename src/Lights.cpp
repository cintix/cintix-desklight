#include "Lights.h"

void Lights::begin() {
    FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, LED_COUNT);
    FastLED.setBrightness(255);
    clear();
    show();
}

void Lights::clear() {
    fill_solid(leds, LED_COUNT, CRGB::Black);
}

void Lights::show() {
    FastLED.show();
}

void Lights::setBrightness(uint8_t value) {
    FastLED.setBrightness(value);
}

void Lights::fillSolid(CRGB color) {
    fill_solid(leds, LED_COUNT, color);
}

void Lights::fillRainbow(uint8_t startHue, uint8_t deltaHue) {
    fill_rainbow(leds, LED_COUNT, startHue, deltaHue);
}
