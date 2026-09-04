// Copy this file to include/config.h and fill in your own values.
// config.h is git-ignored so your WiFi password is never committed.

#pragma once

// ---- WiFi (your 2.4 GHz network) ----
#define WIFI_SSID       "DitNetværk"
#define WIFI_PASSWORD   "DitKodeord"

// Fallback access point, started only if the network above cannot be reached.
#define AP_SSID         "DeskLight-Setup"
#define AP_PASSWORD     "desklight123"   // must be at least 8 characters

// ---- LED strip (NeoPixel / WS2812B) ----
#define LED_DATA_PIN    2      // GPIO the strip data line is connected to
#define LED_COUNT       60     // number of LEDs in the strip

// ---- Network ----
#define HOSTNAME        "desklight"   // mDNS name -> http://desklight.local
