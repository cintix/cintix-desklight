#!/usr/bin/env python3
"""DeskLight host service.

Serves the web UI (../data) and bridges its API to the ESP32 firmware over
USB serial. The firmware no longer runs WiFi/HTTP itself: it applies JSON
commands received on the serial port (see src/serial/SerialLink.cpp) and
answers with its current state as a JSON line.

API (kept identical to the old on-device API so data/app.js is unchanged
in spirit):
  GET  /              -> data/index.html (+ static files from data/)
  GET  /api/state     -> current lamp state + connection flag
  POST /api/control   -> form- or JSON-encoded {mode,color,brightness,bpm,
                         alwaysOn}

Serial protocol (newline-delimited JSON @115200 baud):
  host -> lamp: {"get":true} or a partial/full state set
  lamp -> host: {"mode":...,"color":"#rrggbb","brightness":...,"bpm":...,
                 "alwaysOn":...,"host":...}

`host` in the lamp's reply is the lamp's own view of the USB link: false once
only standby power is left, which is when it keeps the strip dark unless
`alwaysOn` is set.

Usage:
  python3 host/desklight_service.py [--port /dev/ttyACM0] [--http-port 8805]
"""

import argparse
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import serial
from serial.tools import list_ports

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(ROOT, "data")

CONTROL_KEYS = ("mode", "color", "brightness", "bpm", "alwaysOn")
DEFAULT_STATE = {"mode": "solid", "color": "#6a0a7f",
                 "brightness": 80, "bpm": 60, "alwaysOn": False}
CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
    ".png": "image/png",
}


def detect_port(explicit):
    """Return --port if given, otherwise auto-detect an ESP32 USB port."""
    if explicit:
        return explicit
    wanted = []
    for p in list_ports.comports():
        name = (p.device or "") + " " + (p.description or "")
        if p.device.startswith("/dev/ttyACM") or p.device.startswith("/dev/ttyUSB"):
            wanted.append(p)
        elif "usb" in (p.description or "").lower():
            wanted.append(p)
    for p in sorted(wanted, key=lambda p: p.device):
        if p.device == explicit:
            return p.device
    if wanted:
        return wanted[0].device
    return None


class LampLink:
    """Owns the serial connection and keeps a cache of the lamp's state."""

    def __init__(self, port, baud=115200):
        self.port = port
        self.baud = baud
        self.ser = None
        self.lock = threading.Lock()
        self.state = {}
        self.connected = False
        self.host_present = False  # the lamp's view of the USB link
        self.last_seen = 0.0
        self._stop = False
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    # -- public -----------------------------------------------------------
    def snapshot(self):
        with self.lock:
            st = dict(DEFAULT_STATE)
            st.update(self.state)
            st["connected"] = self.connected
            st["host"] = self.host_present
            return st

    def send(self, obj):
        payload = (json.dumps(obj) + "\n").encode()
        with self.lock:
            if self.ser is None:
                return False
            try:
                self.ser.write(payload)
                return True
            except Exception:
                self._close_locked()
                return False

    @staticmethod
    def _norm(key, value):
        if key in ("brightness", "bpm"):
            try:
                return int(value)
            except (TypeError, ValueError):
                return value
        if key == "alwaysOn":
            if isinstance(value, str):
                return value.strip().lower() in ("true", "1", "on")
            return bool(value)
        return str(value)

    def wait_ack(self, expected, timeout=1.2):
        """Block briefly until the lamp confirms 'expected' as its state."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self.lock:
                st = dict(DEFAULT_STATE)
                st.update(self.state)
            if all(self._norm(k, st.get(k)) == self._norm(k, v)
                   for k, v in expected.items()):
                return True
            time.sleep(0.02)
        return False

    def stop(self):
        self._stop = True
        with self.lock:
            self._close_locked()

    # -- internals --------------------------------------------------------
    def _open(self):
        try:
            ser = serial.Serial(self.port, self.baud, timeout=0.05)
            ser.reset_input_buffer()
        except Exception as exc:
            print(f"[serial] cannot open {self.port}: {exc}")
            with self.lock:
                self.ser = None
                self.connected = False
            return False
        with self.lock:
            self.ser = ser
            self.connected = True
        print(f"[serial] connected to {self.port}")
        self.send({"get": True})  # pull the lamp's persisted state
        return True

    def _close_locked(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
        self.connected = False
        self.host_present = False
        self.last_seen = 0.0

    def _handle_line(self, raw):
        try:
            obj = json.loads(raw.decode("utf-8", "replace"))
        except ValueError:
            print(f"[serial] ignored non-JSON line: {raw!r}")
            return
        if not isinstance(obj, dict) or "mode" not in obj:
            return
        with self.lock:
            self.state = {k: obj[k] for k in CONTROL_KEYS if k in obj}
            if "host" in obj:
                self.host_present = bool(obj["host"])
            self.last_seen = time.monotonic()
            self.connected = True
        print(f"[serial] state <- {self.state} host={self.host_present}")

    def _run(self):
        buf = b""
        last_try = 0.0
        while not self._stop:
            now = time.monotonic()
            with self.lock:
                ser = self.ser
                connected = self.connected
            if ser is None:
                if now - last_try > 1.0:
                    last_try = now
                    self._open()
                time.sleep(0.2)
                continue
            try:
                chunk = ser.read(256)
            except Exception:
                with self.lock:
                    self._close_locked()
                continue
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    if line.strip():
                        self._handle_line(line.strip())
            # Heartbeat: nudge the lamp so a reboot/reset is noticed.
            stale = now - self.last_seen
            if connected and self.last_seen and stale > 5.0:
                self.send({"get": True})
            if stale > 10.0:
                with self.lock:
                    if self.ser is not None:
                        self._close_locked()
            time.sleep(0.02)


class Handler(BaseHTTPRequestHandler):
    bridge = None

    def log_message(self, fmt, *args):  # quieter access log
        pass

    # -- helpers ----------------------------------------------------------
    def _send_json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _serve_file(self, rel):
        # Normalise and keep inside DATA_DIR (no path traversal).
        rel = os.path.normpath("/" + rel).lstrip("/")
        path = os.path.join(DATA_DIR, rel)
        if not os.path.realpath(path).startswith(os.path.realpath(DATA_DIR)):
            return False
        if not os.path.isfile(path):
            return False
        ext = os.path.splitext(path)[1].lower()
        ctype = CONTENT_TYPES.get(ext, "application/octet-stream")
        with open(path, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)
        return True

    # -- routes -----------------------------------------------------------
    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            if not self._serve_file("index.html"):
                self._send_json({"error": "data/index.html not found"}, 404)
            return
        if path == "/api/state":
            self._send_json(self.bridge.snapshot())
            return
        rel = path.lstrip("/")
        if rel == "config.json" or not self._serve_file(rel):
            self._send_json({"error": "not found"}, 404)

    def do_POST(self):
        if urlparse(self.path).path != "/api/control":
            self._send_json({"error": "not found"}, 404)
            return
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        ctype = self.headers.get("Content-Type", "")
        updates = {}
        if "application/json" in ctype:
            try:
                updates = json.loads(raw.decode("utf-8", "replace"))
            except ValueError:
                updates = {}
        else:
            for key, values in parse_qs(raw.decode("utf-8", "replace")).items():
                if not values:
                    continue
                value = values[-1]
                if key in ("brightness", "bpm"):
                    try:
                        value = int(value)
                    except ValueError:
                        continue  # invalid number: leave the field unchanged
                elif key == "alwaysOn":
                    value = value.strip().lower() in ("true", "1", "on")
                updates[key] = value
        cmd = {k: updates[k] for k in CONTROL_KEYS if k in updates}
        if cmd:
            self.bridge.send(cmd)
            self.bridge.wait_ack(cmd)
        self._send_json(self.bridge.snapshot())


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=os.environ.get("DESKLIGHT_PORT"),
                    help="USB serial port (default: auto-detect)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http-host", default="127.0.0.1",
                    help="Listen address (default 127.0.0.1; use 0.0.0.0 for LAN)")
    ap.add_argument("--http-port", type=int, default=8805)
    args = ap.parse_args()

    port = detect_port(args.port)
    if not port:
        print("No ESP32 serial port found. Pass one with --port, e.g.:")
        print("  python3 host/desklight_service.py --port /dev/ttyACM0")
        ports = [p.device for p in list_ports.comports()]
        print(f"Available serial ports: {ports or '(none)'}")
        sys.exit(1)

    bridge = LampLink(port, args.baud)
    Handler.bridge = bridge
    httpd = ThreadingHTTPServer((args.http_host, args.http_port), Handler)
    httpd.daemon_threads = True
    url = f"http://{args.http_host}:{args.http_port}"
    print(f"[http] serving {DATA_DIR} at {url}  (Ctrl+C to stop)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        bridge.stop()
        httpd.server_close()


if __name__ == "__main__":
    main()
