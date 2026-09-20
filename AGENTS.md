# Repository Guidelines

## Project Overview

PlatformIO (Arduino) firmware for a desk light built on an ESP32-C3 Super Mini. It drives a 60-LED NeoPixel (WS2812B) strip via FastLED and exposes a Danish web UI over WiFi for controlling color, brightness, on/off, rainbow and a beat/pulse effect.

## Project Structure & Module Organization

- `src/` — feature modules (modular-hybrid architecture) + a thin runtime:
  - `src/main.cpp` — runtime only: boots and composes the modules, owns the main loop
  - `src/lighting/` — `Lighting.h/.cpp`: owns the LED strip, effects, settings state and persistence (`/config.json`)
  - `src/network/` — `Network.h/.cpp`: owns WiFi (home network + always-on fallback AP), mDNS and reconnection
  - `src/host/` — `Host.h/.cpp`: owns the USB link to the host PC, i.e. whether it is still switched on
  - `src/web/` — `Web.h/.cpp`: owns the HTTP server, static UI files and JSON API (`/api/state`, `/api/control`)
  - Public contract lives in each module's header; implementation is hidden in the `.cpp`. Cross-module calls go only through public contracts (Web → Lighting/Network); `main.cpp` holds no business logic.
- `data/` — LittleFS web frontend: `index.html`, `style.css`, `app.js`. Edit here for UI changes; no HTML/CSS/JS in C++ code
- `include/` — `config.h` (git-ignored) with WiFi credentials and hardware settings; `config.example.h` documents it
- `lib/`, `test/` — private libraries and PlatformIO unit tests (currently unused)
- `platformio.ini` — build config; environment `esp32-c3-supermini` (board `esp32-c3-devkitm-1`, LittleFS filesystem, FastLED + ArduinoJson)
- `.pio/` — generated build output (git-ignored)

Copy `include/config.example.h` to `include/config.h` and set your WiFi SSID/password before flashing.

## Build, Test, and Development Commands

Run from the repo root with PlatformIO Core:

- `pio run` — compile the firmware
- `pio run -t upload` — flash the firmware over USB
- `pio run -t buildfs` — build the LittleFS image from `data/`
- `pio run -t uploadfs` — upload the filesystem (web UI + config storage)
- `pio device monitor` — serial console over USB (115200 baud)
- `pio test` — run unit tests

Firmware and filesystem are separate: after changing `data/`, run `uploadfs`; after changing `src/`, run `upload`.

## Coding Style & Naming Conventions

- C/C++: 4-space indentation, no tabs.
- Classes/types PascalCase (`Lights`); methods/variables camelCase (`setBrightness`); macros UPPER_SNAKE_CASE (`LED_COUNT`).
- Header include guards match the file name: `#ifndef Lights_h`.
- Keep hardware/wifi settings in `include/config.h`, not scattered in `src/`.
- Web UI text is Danish; keep JSON API keys (`mode`, `color`, `brightness`, `bpm`, `alwaysOn`, `host`) stable.
- No formatter/linter configured; match surrounding code.

## Testing Guidelines

- PlatformIO Unity-based runner via `pio test`.
- Test files in `test/` named `test_<module>.cpp`; name test functions to describe behavior.
- No tests yet; visual effects are verified manually on hardware.

## Commit & Pull Request Guidelines

- Short, imperative-mood subjects (e.g., "Add beat effect in selected color").
- One logical change per commit; body context when useful.
- PRs: state what changed and why, link issues, and note hardware verification (e.g., "uploaded and visually verified") since effects aren't covered by automated tests.
