// Web module - owns the HTTP server: the static web UI (LittleFS) and the
// JSON API (/api/state, /api/control).
//
// Public contract. The module only consumes the public contracts of Lighting
// and Network; Arduino WebServer internals stay hidden in Web.cpp.
#ifndef Web_h
#define Web_h

#include <Arduino.h>  // String

class Host;
class Lighting;
class Network;

class Web {
public:
    Web();

    void begin(Lighting& lights, Network& network, Host& host);
    void handle();  // serve pending HTTP requests (call from the main loop)

private:
    struct Impl;  // hidden implementation (Web.cpp)
    Impl* impl_;

    // Internal request handling (Web.cpp).
    void handleRoot();
    void handleState();
    void handleControl();
    void handleNotFound();
    bool serveFile(const String& path);
    String contentTypeFor(const String& path) const;
    String stateJson() const;
    static bool parseHexColor(const String& s, uint32_t& rgb);
    static bool parseBool(const String& s);
};
#endif
