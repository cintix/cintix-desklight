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

private:
    bool     present_ = false;
    uint32_t lastPresentMs_ = 0;
};
#endif
