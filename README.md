# ESP32 News Ticker

ESP32 firmware that turns a TFT display into a news ticker. It connects to Wi‑Fi, syncs the clock over NTP, fetches articles from NewsAPI.org over HTTPS and shows their titles and descriptions on a TFT panel using the `TFT_eSPI` library. It cycles through configurable categories and rotates articles periodically.

## Features
- Connects to Wi‑Fi and shows the local IP on the display
- Syncs time over NTP (used to request fresh articles)
- Fetches news (title + description) from NewsAPI.org over HTTPS
- Cycles through categories and rotates articles on the screen
- Simple word-wrapping to fit text on the TFT

## Files
- Main program: [src/main.cpp](src/main.cpp)
- Credentials template: [src/secrets.h.example](src/secrets.h.example)

## Hardware
- ESP32 development board (any common dev board)
- SPI TFT compatible with the `TFT_eSPI` library (ST7789, 240x320 — see `platformio.ini`)
- GPIOs 21 and 27 are driven HIGH during setup (GPIO 21 also serves as the TFT backlight)

Wiring depends on your specific TFT module. The display pins are defined as `build_flags` in `platformio.ini`.

## Configuration
The firmware expects these preprocessor macros to be defined at compile time:

- `WIFI_SSID` — your Wi‑Fi SSID
- `WIFI_PASS` — your Wi‑Fi password
- `API_KEY` — NewsAPI.org API key

Provide them in one of two ways:

**1) Header file** — copy the template and fill in your values:

```bash
cp src/secrets.h.example src/secrets.h
```

`main.cpp` includes `secrets.h` automatically when `WIFI_SSID` is not already defined.

**2) Build flags via `secrets.ini`** — create a `secrets.ini` in the project root. `platformio.ini` pulls it in through `extra_configs`:

```ini
[secrets]
build_flags =
    -DWIFI_SSID=\"your_ssid\"
    -DWIFI_PASS=\"your_password\"
    -DAPI_KEY=\"your_newsapi_key\"
```

Both `src/secrets.h` and `secrets.ini` are listed in `.gitignore` — never commit credentials.

## Build & Upload
From the project root, using PlatformIO:

```bash
pio run            # build
pio run -t upload  # build + upload to the connected device
pio device monitor # open the serial monitor (115200 baud)
```

Or open the project in the PlatformIO IDE and use the graphical buttons.

## Runtime behavior
- The code cycles the categories defined in `src/main.cpp` (`Tech`, `Space`, `AI`, `Software`, `Economy`).
- Each category runs for the duration set in the `categories` array (1 hour by default).
- Within a category, articles rotate every minute (`NEWS_INTERVAL`).

See the implementation and comments in [src/main.cpp](src/main.cpp) for details about timing, display layout and how text wrapping is implemented.

## Dependencies
Managed via `platformio.ini`:

- `TFT_eSPI`
- `ArduinoJson`
- `WiFi`, `HTTPClient`, `WiFiClientSecure` (ESP32 Arduino core)

## Notes
- NewsAPI limits requests by plan — avoid frequent polling during development.
- The HTTPS request uses `setInsecure()` (no TLS certificate validation) to avoid bundling a CA certificate; traffic is still encrypted.
- Keep your `API_KEY` and Wi‑Fi credentials private. Do not commit secrets to version control.
