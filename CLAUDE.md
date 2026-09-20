# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

PlatformIO/Arduino firmware for a WiFi desk light: an ESP32-C3 Super Mini driving a 60-LED WS2812B strip through FastLED, with a Danish-language web UI served from LittleFS for controlling mode (off/solid/rainbow/beat), color, brightness and BPM.

`AGENTS.md` is the repository's own contributor guide (style, commit, PR conventions) — keep it in sync when conventions change.

## Commands

Run from the repo root with PlatformIO Core:

- `pio run` — compile the firmware
- `pio run -t upload` — flash firmware over USB
- `pio run -t buildfs` — build the LittleFS image from `data/`
- `pio run -t uploadfs` — upload the filesystem (web UI + `/config.json` storage)
- `pio device monitor` — serial console, 115200 baud
- `pio test` — Unity test runner (no tests exist yet)

Firmware and filesystem flash separately: `src/` changes need `upload`; `data/` changes need `uploadfs`. A missing `data/index.html` shows up in the browser as a 404 telling you to run `uploadfs`.

`include/config.h` is git-ignored — copy `include/config.example.h` to it and set WiFi credentials, `LED_DATA_PIN`, `LED_COUNT` and `HOSTNAME` before flashing.

## Architecture

**Modular-hybrid**: each capability is one module that owns its state and resources; `src/main.cpp` is a thin runtime that only composes modules and owns the loop. Modules are:

- `src/lighting/` — LED strip, effects, settings state, persistence to `/config.json`
- `src/network/` — WiFi (home network + always-on fallback AP), mDNS, reconnection supervision
- `src/host/` — the USB link to the host PC (is it still switched on?)
- `src/web/` — HTTP server, static UI from LittleFS, JSON API (`GET /api/state`, `POST /api/control`, plus the `GET /api/debug` diagnostics endpoint)

**Contracts are headers; implementation is private.** Every module header declares the public contract (and often explicit pimpl: `struct Impl; Impl* impl_;` with the definition in the `.cpp`). Cross-module calls go *only* through those public contracts — `Web` takes `Lighting&` and `Network&` and never touches FastLED or WiFi directly; it forward-declares the classes in `Web.h` and includes the headers in `Web.cpp`. When adding a capability, extend the owning module's public header rather than reaching into another module's internals from `main.cpp` or a helper.

Duplication across the module boundary is deliberate: `parseHexColor`/`colorHex` exist independently in both `Web.cpp` and `Lighting.cpp` so neither module depends on the other's conversion helpers.

**UI is data-only**: `data/index.html`, `data/style.css`, `data/app.js` are served verbatim from LittleFS. No HTML/CSS/JS is embedded in C++. UI text is Danish; JSON API keys (`mode`, `color`, `brightness`, `bpm`, `alwaysOn`, `ip`, `ap`, `host`) and the `#rrggbb` color wire format are stable interfaces — `app.js` POSTs `application/x-www-form-urlencoded` to `/api/control` and every control response echoes the full state JSON. `/api/debug` is the exception: it exists for diagnosis and its shape may change freely.

### Non-obvious behaviours worth preserving

- **AP before STA** (`Network::begin`): the fallback access point must start *before* the first station connection attempt. On this ESP32-C3, a failed STA attempt leaves the radio in a state where an AP started afterwards never sends beacons. The AP stays up permanently as a fallback; `update()` retries the home network every 30 s.
- **Dark strip means no host, not no WiFi**: rendering runs while `hostPresent || alwaysOn` — WiFi deliberately has no say. Until host detection existed, `setOnline()` gated rendering, which made a dark strip an "offline" indicator; that was removed because a router hiccup turned the light off while the PC it follows was running fine. `setOnline()` now only watches the *falling* edge: a fresh WiFi drop lights `FAULT_BLINK_MS` (2 s) of amber blinking, then rendering resumes. The blink is suppressed when the strip would be dark anyway, so a network fault never lights up a dark room.
- **Host presence gates the light**: the fixture is powered from a PC's USB port, and a switched-off PC still supplies 5 V on VBUS, so "is it plugged in" is not the question — "is the host controller still running" is. `Host` polls `Serial.isPlugged()`; because `ARDUINO_USB_MODE=1` makes `Serial` the USB Serial/JTAG peripheral (`HWCDC`), that flag is driven by the SOF packets a live host sends every 1 ms, and the core registers the detecting tick hook automatically at system init. Absence is only believed after ~3 s without SOF, so a bus reset does not blink the light. The user-facing override is the "Altid tændt" toggle, persisted in `/config.json` as `alwaysOn` alongside the other settings — `uploadfs` resets it, since that wipes the filesystem.
- **The UI only exists after `uploadfs`**: `data/index.html`, `app.js` and `style.css` ship from LittleFS, not from the firmware image, so `pio run -t upload` alone leaves the browser serving the previous UI — a new control appears with no code change and no flash of the firmware. The mismatch is not harmless: a page cached from before a field existed throws inside `app.js`'s `applyUi()`, and because every handler calls `applyUi()` *before* `send()`, all saving stops silently while the UI keeps showing whatever `/api/state` returned at load. `app.js` guards its lookups for this reason.
- **mDNS is set up once** (`Network::mdnsStarted_`): `joinStation()` runs again on every reconnect, and a second `MDNS.begin()` leaves the first responder and its service list alive, so `addService()` fails with `Failed adding service http.tcp` while the connection itself looks healthy.
- **Diagnostics are deliberate, not leftovers**: `/api/debug` reports the raw USB signals (`plugged_now`, `sof_index`, `sof_moving`, `int_raw`, `int_ena`), the gating inputs (`gate.should_run`) and the persistence state (`config.exists`, `config.size`, `reset_reason`), and `POST /api/control` logs the received arguments to serial. They are served over WiFi on purpose: when host detection misbehaves, the USB link is exactly the thing that cannot be trusted. Keep them.
- **`/config.json` is Lighting's private file**: `Web::handleNotFound` explicitly refuses to serve it through the generic static-file path.
- **Debounced persistence**: setters only mark state dirty; `Lighting::update()` writes to LittleFS ~1 s after the last change to limit flash wear. All setters validate and clamp (brightness 0–100, bpm 60–200) and no-op when the value is unchanged.
- **Global FastLED brightness**: `FastLED.setBrightness()` is set per frame inside each effect. The beat effect scales it by a computed envelope, so brightness there is a product of the user setting and the beat level, not the raw percentage.
- **Frame pacing**: `Lighting::update()` renders at ~16 ms intervals; the beat effect fast-forwards its beat clock (`lastKickMs += intervalMs` in a loop) so a slow HTTP request doesn't desynchronise the tempo.

### Style

4-space indent, no tabs. Types PascalCase, methods/variables camelCase, macros UPPER_SNAKE_CASE. Include guards match the filename (`#ifndef Lighting_h`). Hardware and WiFi settings belong in `include/config.h`, never hard-coded in `src/`. No formatter or linter is configured — match surrounding code.

### Testing

Effects and hardware behaviour are verified by flashing to the device and watching the strip; `pio test` is wired up but unused. When changing behaviour, note in the summary whether it was verified on hardware.
