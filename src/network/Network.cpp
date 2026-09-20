#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "config.h"
#include "Network.h"

Network::Network() {}

// Start the fallback access point. IMPORTANT: this must run BEFORE the first
// STA attempt - on this ESP32-C3 a failed STA connection leaves the radio in a
// state where an AP started afterwards never sends beacons. An AP started
// first keeps beaconing reliably even while the STA side fails and retries.
void Network::startAccessPoint() {
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    online_ = false;
    lastProbeMs_ = millis();
    Serial.printf("Access point \"%s\" started. IP: %s\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());
}

// Called whenever the home network is (or becomes) reachable. The access
// point stays on as a permanent fallback.
void Network::joinStation() {
    online_ = true;
    // Set mDNS up once. This runs again on every reconnect, and a second
    // begin() would leave the first responder (and its service list) in place,
    // so addService() then fails with "Failed adding service http.tcp" while
    // the connection itself looks fine.
    if (!mdnsStarted_ && MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted_ = true;
        Serial.printf("mDNS: http://%s.local\n", HOSTNAME);
    }
    Serial.printf("Connected. IP: %s  (RSSI: %d dBm)\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

// Diagnostic: list what the radio actually sees, so the serial log shows
// whether the module works and whether the configured network is visible.
void Network::printScan() {
    Serial.println("Scanning for nearby WiFi networks...");
    const int8_t n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println("  (no networks found - power/antenna?)");
        return;
    }
    bool found = false;
    for (int8_t i = 0; i < n; i++) {
        const String ssid = WiFi.SSID(i);
        const bool isTarget = ssid == WIFI_SSID;
        found = found || isTarget;
        Serial.printf("  %s%s  channel=%d  RSSI=%d dBm\n", ssid.c_str(),
                      isTarget ? "   <-- configured" : "", WiFi.channel(i),
                      WiFi.RSSI(i));
    }
    if (!found) {
        Serial.printf("  NOTE: \"%s\" not found (is 2.4 GHz enabled?)\n",
                      WIFI_SSID);
    }
    WiFi.scanDelete();
}

void Network::begin() {
    startAccessPoint();  // AP first - must precede any STA attempt
    WiFi.mode(WIFI_AP_STA);  // keep the AP while the STA side joins the router
    WiFi.setSleep(false);    // modem sleep can cause random disconnects
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("Connecting to WiFi \"%s\"", WIFI_SSID);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        joinStation();
    } else {
        Serial.printf("\nHome network unreachable (WiFi status %d).\n",
                      WiFi.status());
        printScan();
        Serial.println("The web UI is available on the access point.");
    }
}

void Network::update() {
    const uint32_t now = millis();

    if (WiFi.status() == WL_CONNECTED) {
        if (!online_) {
            joinStation();  // home network became reachable
        }
    } else if (online_) {
        // Just lost the home net - switch reporting back to the access point.
        online_ = false;
        lastProbeMs_ = now;
        Serial.println("Home network lost; access point takes over");
    } else if (now - lastProbeMs_ > 30000) {
        // Home net down: the AP keeps serving the UI; retry the router slowly.
        lastProbeMs_ = now;
        WiFi.setSleep(false);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        Serial.println("Home network unreachable; access point stays up");
    }
}

bool Network::isOnline() const {
    return online_;
}

IPAddress Network::localIp() const {
    return WiFi.localIP();
}

IPAddress Network::apIp() const {
    return WiFi.softAPIP();
}
