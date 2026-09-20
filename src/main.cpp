// DeskLight runtime - composes the feature modules and owns the main loop.
// No business logic lives here; each capability is owned by its module:
//   lighting/ - LED strip, effects and settings persistence (LittleFS)
//   serial/   - USB serial control channel used by the host service
//
// The network/ and web/ modules are intentionally NOT used right now: the
// web UI is hosted by a service on the host PC (host/desklight_service.py)
// which talks to this firmware over USB serial. The module files are kept.
#include <Arduino.h>
#include <LittleFS.h>

#include "lighting/Lighting.h"
#include "serial/SerialLink.h"

static Lighting lights;
static SerialLink serialLink;

void setup()
{
    Serial.begin(115200);
    delay(100);

    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS mount failed");
    }

    lights.begin();  // loads persisted settings and lights the LEDs at once
    serialLink.begin(lights);

    Serial.println("DeskLight ready - JSON commands over USB serial.");
}

void loop()
{
    serialLink.update();          // apply commands sent by the host service
    lights.update(millis());
}
