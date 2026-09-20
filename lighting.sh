#!/usr/bin/env bash
# DeskLight - start host-webservicen og åbn web-UI'et i browseren.
#
#   ./lighting.sh                  -> http://127.0.0.1:8805
#   ./lighting.sh --port /dev/ttyACM1
#
# Alias i ~/.bashrc:
#   alias lighting='/mnt/data/projects/cintix-desklight/lighting.sh'
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT=8805
URL="http://127.0.0.1:${PORT}"

# Find et python3 der kan importere pyserial (default python3, ellers
# PlatformIO-venv'et som fallback).
PY=""
for py in python3 "$HOME/.platformio/penv/bin/python" python; do
    if command -v "$py" >/dev/null 2>&1 && "$py" -c "import serial" >/dev/null 2>&1; then
        PY="$py"
        break
    fi
done
if [ -z "$PY" ]; then
    echo "Fejl: ingen python3 med pyserial fundet. Installér med:" >&2
    echo "  python3 -m pip install pyserial" >&2
    exit 1
fi

open_browser() {
    if command -v xdg-open >/dev/null 2>&1; then
        xdg-open "${URL}" >/dev/null 2>&1 || true
    elif command -v gio >/dev/null 2>&1; then
        gio open "${URL}" >/dev/null 2>&1 || true
    else
        echo "Åbn selv ${URL} i din browser." >&2
    fi
}

# Kører servicen allerede? Så bare åbn browseren.
if curl -s -o /dev/null --max-time 1 "${URL}/"; then
    echo "DeskLight kører allerede på ${URL}"
    open_browser
    exit 0
fi

echo "Starter DeskLight på ${URL} ..."
"$PY" "${DIR}/host/desklight_service.py" --http-port "${PORT}" "$@" &
SERVER_PID=$!
trap 'kill "${SERVER_PID}" 2>/dev/null || true' EXIT INT TERM

# Vent på at serveren svarer (maks ~10 s).
READY=0
for _ in $(seq 1 50); do
    if curl -s -o /dev/null --max-time 1 "${URL}/"; then
        READY=1
        break
    fi
    sleep 0.2
done

if [ "${READY}" -eq 1 ]; then
    echo "Åbner ${URL}"
    open_browser
else
    echo "Advarsel: ${URL} svarede ikke - er porten optaget?" >&2
fi

# Hold serveren i forgrunden, så Ctrl+C stopper den igen.
wait "${SERVER_PID}"
