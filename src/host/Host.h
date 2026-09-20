// Host module - owns the USB link to the host PC. Reports whether a live host
// is attached, so the light can go dark once only standby power is left.
//
// Public contract. USB peripheral details stay hidden in Host.cpp.
#ifndef Host_h
#define Host_h

#include <stdint.h>

class Host {
public:
    void begin();            // seed the initial state
    void update();           // poll for host presence (call from the main loop)
    bool isPresent() const;  // a host PC is attached and running

    // --- Diagnostics ---------------------------------------------------------
    // Raw USB signals, served through /api/debug. Kept on purpose: when host
    // detection misbehaves the USB link is exactly the thing that cannot be
    // trusted, and these readings stay reachable over WiFi when it is not.
    bool     pluggedNow() const;  // Serial.isPlugged(), sampled right now
    bool     sofMoving() const;   // a host is actively sending SOF frames
    uint32_t sofIndex() const;    // SOF frame counter (wraps at 2048)
    uint32_t usbIntRaw() const;   // raw interrupt register
    uint32_t usbIntEna() const;   // enabled interrupt mask

private:
    bool     present_ = false;
    uint32_t lastPresentMs_ = 0;
};
#endif
