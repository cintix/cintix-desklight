#include <Arduino.h>
#include <Lights.h>

Lights leds;

void setup() {
  Serial.begin(9600);  
 // leds.setBrightness(80);
  leds.setColor(0x6a0a7f);

}

void loop() {
    //leds.rainbow(35);
}