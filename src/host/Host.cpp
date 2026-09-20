#include <Arduino.h>
#include <hal/usb_serial_jtag_ll.h>

#include "Host.h"

// ---------------------------------------------------------------------------
// Hidden implementation.
//
// This board builds with ARDUINO_USB_MODE=1, so Serial is the USB Serial/JTAG
// peripheral (HWCDC) rather than UART0. Its isPlugged() answers exactly the
// question we care about: is a live USB host on the other end?
//
// A host controller sends an SOF (Start Of Frame) packet every 1 ms, and the
// core's tick hook - registered automatically at system init, no begin()
// needed - clears the flag after ~5 ms without one. A PC that is switched off
// still supplies 5 V on VBUS, but its controller is gone and sends no SOF.
// ---------------------------------------------------------------------------
namespace {

// A brief gap in SOF (bus reset, hub re-enumeration) must not blink the light.
const uint32_t ABSENCE_CONFIRM_MS = 3000;

}  // namespace

void Host::begin() {
    // The core starts out assuming "connected" until proven otherwise, so a
    // sample taken here can be optimistically true on a powered-off PC.
    // Starting from false instead lets the light come on at the first update()
    // that has real evidence, and never flash during boot.
    present_ = false;
    lastPresentMs_ = millis();
    Serial.println("Host: watching for a live USB host");
}

void Host::update() {
    const uint32_t now = millis();

    if (Serial.isPlugged()) {
        lastPresentMs_ = now;
        if (!present_) {
            present_ = true;
            Serial.println("Host present; light enabled");
        }
        return;
    }

    // Absence is only believed once it has held for a while.
    if (present_ && now - lastPresentMs_ >= ABSENCE_CONFIRM_MS) {
        present_ = false;
        Serial.println("Host absent; light disabled");
    }
}

bool Host::isPresent() const {
    return present_;
}

// ---------------------------------------------------------------------------
// Diagnostics (see /api/debug).
// ---------------------------------------------------------------------------
bool Host::pluggedNow() const {
    return Serial.isPlugged();
}

uint32_t Host::sofIndex() const {
    return USB_SERIAL_JTAG.fram_num.sof_frame_index;
}

// A live USB host sends an SOF every 1 ms and each one advances the frame
// index, so a change between two samples proves a host is talking to us. The
// core's tick hook consumes the SOF interrupt bit every millisecond, which
// makes sampling that bit a race - this register is left alone.
bool Host::sofMoving() const {
    const uint32_t before = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    delay(10);
    return USB_SERIAL_JTAG.fram_num.sof_frame_index != before;
}

uint32_t Host::usbIntRaw() const {
    return USB_SERIAL_JTAG.int_raw.val;
}

uint32_t Host::usbIntEna() const {
    return USB_SERIAL_JTAG.int_ena.val;
}
