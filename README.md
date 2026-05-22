# ESP32 News Ticker

ESP32 firmware that turns a TFT display into a news ticker. It connects to Wi‑Fi, syncs the clock over NTP, fetches articles from NewsAPI.org over HTTPS and shows their titles and descriptions on a TFT panel using the `TFT_eSPI` library. It cycles through configurable categories and rotates articles periodically, and every few minutes it shows a combined weather + world-clock screen.

## Features
- Connects to Wi‑Fi and shows the local IP on the display
- Syncs time over NTP (used for article dates and the world clocks)
- Fetches news (title + description) from NewsAPI.org over HTTPS
- Cycles through categories and rotates articles on the screen
- Shows current weather (temperature, humidity, rain probability) for the device location
- Displays a world clock for Colombia, Spain, Uruguay and Texas
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
- `WEATHER_CITY` — city used to look up the weather (plain ASCII recommended; it is also shown on the display)
- `WEATHER_COUNTRY` — *optional* country (ISO-2 code or name) to disambiguate cities with the same name

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
    '-DWEATHER_CITY="Your City"'
    '-DWEATHER_COUNTRY="ES"'
```

Build-flag values that contain spaces (such as a multi-word city) must be wrapped in single quotes, as shown for `WEATHER_CITY`.

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
- The display alternates every minute (`SCREEN_INTERVAL`) between the weather + clock screen and a news article — info, news, info, news… — advancing to the next article on each news turn.
- When a category's hour ends, the next category is loaded and the same alternation continues.

See the implementation and comments in [src/main.cpp](src/main.cpp) for details about timing, display layout and how text wrapping is implemented.

## Weather & world clock
The combined info screen shows:

- **Weather** — temperature, relative humidity and rain probability from [Open-Meteo](https://open-meteo.com) (`api.open-meteo.com`, HTTPS, no API key). The location is configured with `WEATHER_CITY` and resolved to coordinates once at startup via the Open-Meteo geocoding API (`geocoding-api.open-meteo.com`, HTTPS, no API key). Weather is refreshed every `WEATHER_INTERVAL` (30 min).
- **World clock** — local time for Colombia, Spain, Uruguay and Texas. No time API is used: the clocks are derived from the existing NTP sync and POSIX `TZ` strings, which handle daylight saving time automatically.

Both services are keyless, so no extra credentials are needed. If location resolution or the weather request fails, the screen falls back to showing only the clocks.

## Dependencies
Managed via `platformio.ini`:

- `TFT_eSPI`
- `ArduinoJson`
- `WiFi`, `HTTPClient`, `WiFiClientSecure` (ESP32 Arduino core)

## Notes
- NewsAPI limits requests by plan — avoid frequent polling during development.
- HTTPS requests use `setInsecure()` (no TLS certificate validation) to avoid bundling CA certificates; traffic is still encrypted.
- News text is folded to plain ASCII before display (á → a, ñ → n) because the built-in TFT fonts contain no accented glyphs.
- The weather location comes from `WEATHER_CITY`; if geocoding picks the wrong place, set `WEATHER_COUNTRY` or use a more specific city name.
- Keep your `API_KEY` and Wi‑Fi credentials private. Do not commit secrets to version control.
