// SerialLink module - owns the USB serial control channel used by the host
// service. It parses newline-delimited JSON commands, applies them to the
// Lighting module through its public contract and reports the resulting
// state back to the host as a JSON line.
//
// Public contract. Serial/parsing details stay hidden in SerialLink.cpp.
#ifndef SerialLink_h
#define SerialLink_h

class Lighting;

class SerialLink {
public:
    SerialLink();

    void begin(Lighting& lights);  // bind the module it controls
    void update();                 // read + apply pending serial commands

private:
    void sendState();              // report current state as a JSON line
    void handleLine(const char* line);

    struct Impl;  // hidden implementation (SerialLink.cpp)
    Impl* impl_;
};
#endif
