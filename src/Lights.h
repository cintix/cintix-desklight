#ifndef Lights_h
#define Lights_h

#include "FastLED.h"

#define FASTLED_ALLOW_INTERRUPTS 0
#define STRIP_PIN 2
#define NUM_LEDS 60


class Lights {
   public:
    Lights();
    void rainbow(uint8_t speed = 25);
    void turnOff();
    void turnOn();
    void setBrightness(int value);
    void setRange(int index, int length, CRGB color);
    void setColor(CRGB color);

   private:
    void clear();
    CRGB leds[NUM_LEDS];
};

Lights::Lights() {
    FastLED.addLeds<NEOPIXEL,STRIP_PIN>(leds, NUM_LEDS);
}

void Lights::turnOn() {
    FastLED.show();
}

void Lights::turnOff() {
    clear();
    FastLED.show();
}

void Lights::setBrightness(int value) {
    FastLED.setBrightness(value);
    FastLED.show();
}

void Lights::setRange(int index, int length, CRGB color) {
    int iLength = index + length;
    for (int i = index; i < iLength; i++) {
        leds[i] = color;
    }
    FastLED.show();
}

void Lights::setColor(CRGB color) {
    setRange(0, NUM_LEDS, color);
}

void Lights::rainbow(uint8_t speed = 25) {
    uint8_t deltaHue = 10;
    uint8_t thisHue = beat8(speed,255); 
    fill_rainbow(leds, NUM_LEDS, thisHue, deltaHue);            
    FastLED.show();
}


void Lights::clear() {
    /** Clear the lights */
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
    }
    FastLED.show();
}


#endif