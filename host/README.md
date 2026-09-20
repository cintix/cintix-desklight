# DeskLight host-webservice

Lampen (ESP32-C3) kører ikke længere WiFi/HTTP selv. I stedet hoster denne
lille Python-webservice web-UI'et fra `data/` på din computer og brobygger
til lampen over USB-serial:

```
Browser  ->  host/desklight_service.py  ->  /dev/ttyACMx  ->  ESP32 (SerialLink)
```

Firmwaren gemmer stadig sine indstillinger i `/config.json` på LittleFS -
den får bare kommandoerne gennem serial i stedet for HTTP.

## Kørsel

Forudsætninger: Python 3 + `pyserial` (findes typisk; ellers
`pip install pyserial`).

```bash
# 1. Flash firmware (luk evt. serial-monitor først, så porten er fri)
pio run -t upload

# 2. Start webservicen + åbn browseren (port 8805)
./lighting.sh

# ...eller med eksplicit serial-port
./lighting.sh --port /dev/ttyACM1

# Valgfrit: gør det til en kommando `lighting` i ~/.bashrc:
#   alias lighting='/mnt/data/projects/cintix-desklight/lighting.sh'
```

Åbn derefter `http://127.0.0.1:8805` i browseren. Vil du styre lampen fra
andre enheder på netværket: `--http-host 0.0.0.0`.

## API (samme som det gamle on-device API)

| Rute            | Beskrivelse                                        |
|-----------------|----------------------------------------------------|
| `GET /`         | Web-UI'et fra `data/`                              |
| `GET /api/state`| Lampens aktuelle tilstand + `connected` + `host`   |
| `POST /api/control` | Sæt felter: `mode`, `color`, `brightness`, `bpm`, `alwaysOn` |

## Serial-protokol

Newline-adskilt JSON på 115200 baud:

- `{"get":true}` -> lampen svarer med sin tilstand
- `{"mode":"rainbow","brightness":70}` -> sætter de angivne felter
- Svar: `{"mode":"solid","color":"#ff8800","brightness":70,"bpm":90,"alwaysOn":false,"host":true}`

Lampen svarer altid efter en kommando (ack), og host-servicen gemmer svaret,
så `/api/state` altid afspejler lampens faktiske (og gemte) tilstand.

## PC-tilstedeværelse ("Altid tændt")

Lampen får strøm fra PC'ens USB-port, og en slukket PC giver stadig 5 V på
VBUS. Firmwaren spørger derfor ikke "er kablet sat i", men "kører værten
endnu": `src/host/` lytter efter de SOF-pakker en levende USB-host sender
hvert millisekund, og først når de har været væk i ~3 s slukkes lyset.

`host` i status er lampens eget svar på det spørgsmål. Er PC'en slukket,
holder lampen LED'erne slukket - medmindre **Altid tændt** (`alwaysOn`) er
slået til i UI'et; indstillingen gemmes i `/config.json` sammen med resten.

Bemærk: kører servicen på den PC der også giver lampen strøm, vil `host`
normalt være `true`, når `connected` er `true`. Feltet er mest nyttigt til at
se om lampen selv mener at værten er der (fx ved et løst kabel).

## Netværksmodulerne

`src/network/` og `src/web/` er **ikke slettet** - de er bare ekskluderet fra
buildet i `platformio.ini` (`build_src_filter`). For at tage WiFi/HTTP i brug
igen: fjern den linje og kald modulerne fra `src/main.cpp`.

## Fejlfinding

- `Permission denied: '/dev/ttyACM0'` -> brugere skal være i `dialout`-gruppen
  (log ud/ind efter `sudo usermod -aG dialout $USER`).
- `port is busy` -> en serial-monitor eller anden proces holder porten; luk den.
- Lampen svarer ikke -> tjek USB-kablet, og at firmwaren er flash'et (`pio run -t upload`).
