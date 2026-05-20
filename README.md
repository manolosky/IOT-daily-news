# News API into LCD module

ESP32 news ticker for a TFT display. The firmware connects the ESP32 to Wi‑Fi, fetches news articles from NewsAPI.org and displays titles and descriptions on a TFT using the `TFT_eSPI` library. It cycles through configurable categories and rotates articles periodically.

## Features
- Connects to Wi‑Fi and shows local IP on the display
- Fetches news (title + description) from NewsAPI.org
- Cycles through categories and rotates articles on the screen
- Simple word-wrapping to fit text on the TFT

## Files
- Main program: [src/main.cpp](src/main.cpp)

## Hardware
- ESP32 development board (any common dev board)
- SPI TFT compatible with the `TFT_eSPI` library
- Optional: two GPIOs (21 and 27 in the code) are driven HIGH during setup

Wiring depends on your specific TFT module; follow the `TFT_eSPI` documentation and set the correct pins in `User_Setup.h` or the project `TFT_eSPI` configuration.

## Configuration
The firmware expects the following preprocessor macros to be defined at compile time:

- `WIFI_SSID` — your Wi‑Fi SSID
- `WIFI_PASS` — your Wi‑Fi password
- `USER_NAME` — display name used in greeting
- `API_KEY` — NewsAPI.org API key

You can provide them in two common ways:

1) Create a header `src/secrets.h` and define the macros there, then include it from `main.cpp` (or project-wide):

```cpp
// src/secrets.h
#ifndef SECRETS_H
#define SECRETS_H
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"
#define USER_NAME "Your Name"
#define API_KEY "your_newsapi_key"
#endif
```

2) Add build flags in `platformio.ini` (quick for CI / local builds):

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
build_flags = -DWIFI_SSID=\"your_ssid\" -DWIFI_PASS=\"your_password\" -DUSER_NAME=\"Your Name\" -DAPI_KEY=\"your_newsapi_key\"
```

Note: `main.cpp` currently references these macros directly — ensure you define them using one of the methods above.

## Build & Upload
Build and upload using PlatformIO from the project root:

```bash
pio run            # build
pio run -t upload  # build + upload to the connected device
```

Or open the project in PlatformIO IDE and use the graphical buttons.

## Runtime behavior
- The code cycles categories (configured in `src/main.cpp`) and fetches top articles for each category using NewsAPI.
- Articles are rotated every minute (`NEWS_INTERVAL`), and each category runs for the configured duration in the `categories` array.

See the implementation and comments in [src/main.cpp](src/main.cpp) for details about timing, display layout and how text wrapping is implemented.

## Dependencies
- `TFT_eSPI`
- `ArduinoJson`
- `WiFi` (ESP32 core)
- `HTTPClient`

Install and manage dependencies via `platformio.ini`.

## Notes
- NewsAPI may limit requests by plan — avoid frequent polling during development.
- Keep your `API_KEY` private. Do not commit secrets to public repositories.

---
If you want, I can also add a `src/secrets.h.example` file and update `main.cpp` to include it, or commit these changes for you.
