// Network module - owns the radio: joining the home network, the always-on
// fallback access point, mDNS and background reconnection.
//
// Public contract. WiFi internals stay hidden in Network.cpp.
#ifndef Network_h
#define Network_h

#include <stdint.h>
#include <IPAddress.h>

class Network {
public:
    Network();

    void begin();           // start AP first, then try to join the home net
    void update();          // supervision + background retries

    bool isOnline() const;  // connected to the home network
    IPAddress localIp() const;  // home-network IP (0.0.0.0 when offline)
    IPAddress apIp() const;     // fallback access-point IP

private:
    void startAccessPoint();
    void joinStation();
    void printScan();

    bool     online_ = false;
    bool     mdnsStarted_ = false;  // the responder survives a reconnect
    uint32_t lastProbeMs_ = 0;
};
#endif
