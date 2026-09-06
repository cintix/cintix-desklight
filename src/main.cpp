// DeskLight runtime - composes the feature modules and owns the main loop.
// No business logic lives here; each capability is owned by its module:
//   lighting/ - LED strip, effects and settings persistence
//   network/  - WiFi (home network + fallback AP) and mDNS
//   web/      - HTTP UI + JSON API
#include <Arduino.h>
#include <LittleFS.h>
#include <IPAddress.h>

#include "lighting/Lighting.h"
#include "network/Network.h"
#include "web/Web.h"

static Lighting gLights;
static Network gNetwork;
static Web gWeb;

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\nDeskLight booting...");

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
    }

    gLights.begin();   // loads persisted settings; LEDs stay dark until online
    gNetwork.begin();  // starts the fallback AP, then joins the home network
    gWeb.begin(gLights, gNetwork);

    const IPAddress ip = gNetwork.isOnline() ? gNetwork.localIp()
                                             : gNetwork.apIp();
    Serial.printf("Open the web UI at http://%s\n", ip.toString().c_str());
}

void loop() {
    gNetwork.update();                        // keep the home-net link healthy
    gWeb.handle();                            // serve the web UI / API
    gLights.setOnline(gNetwork.isOnline());   // LEDs on = online indicator
    gLights.update(millis());
}
