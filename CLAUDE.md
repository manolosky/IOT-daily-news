# Project Instructions — esp32-news-ticker

ESP32 firmware (Arduino framework, built with PlatformIO) that fetches articles
from NewsAPI.org and displays them on a TFT panel.

These rules govern development in this repository. The global WordPress / PHP /
React standards do **not** apply here — this is an embedded C++ project.

## Language & Communication
- Respond in Spanish.
- Write all code comments in English.
- Keep technical language clear and concise.

## Platform & tooling
- Target: ESP32 (`esp32dev` board), Arduino framework, PlatformIO.
- Build/upload from the project root: `pio run`, `pio run -t upload`,
  `pio device monitor` (115200 baud).
- Do not hand-edit generated files: `sdkconfig.esp32dev` and anything under `.pio/`.

## C++ / embedded standards
- Target C++17 (the Arduino-ESP32 default); avoid features the toolchain rejects.
- Prefer fixed-size buffers and stack allocation over the heap; avoid `new`/`malloc`
  in the hot path and watch for heap fragmentation.
- Always bound string operations: use `snprintf` with `sizeof(buffer)`; never
  `sprintf`, `strcpy` or `strcat` without an explicit limit.
- Minimize use of the Arduino `String` class; when used, release large strings
  early (e.g. `payload = String();`).
- Mark constants `constexpr`/`const`; prefer `enum class` over bare `#define`
  for enumerated values. Reserve `#define` for compile-time config and macros.
- Apply `const` correctness to pointers and function parameters.
- Group related state in `struct`s, as the existing `Article` / `NewsCategory` do.
- Naming: functions and variables in `camelCase`, macros and compile-time
  constants in `UPPER_SNAKE_CASE`. Use descriptive names.

## Concurrency & timing
- `loop()` must stay non-blocking. Use `millis()`-based timers instead of long
  `delay()` calls.
- Never busy-wait on network or hardware state without a timeout and an escape path.

## Networking
- Always use HTTPS for external APIs; pair `HTTPClient` with `WiFiClientSecure`.
- Set an explicit timeout on every HTTP request.
- Handle Wi-Fi disconnection and HTTP/JSON errors gracefully — never assume success.
- Use ArduinoJson filters to bound memory when parsing large responses.

## Hardware & pins
- Define display and peripheral pins as `build_flags` in `platformio.ini`,
  not as magic numbers scattered through the code.
- Document any GPIO driven directly in `setup()`.

## Secrets
- Never commit credentials. `src/secrets.h` and `secrets.ini` are git-ignored.
- Keep `src/secrets.h.example` in sync with the macros the firmware actually uses
  (`WIFI_SSID`, `WIFI_PASS`, `API_KEY`).
- Provide secrets via `src/secrets.h` or the `[secrets]` section that
  `platformio.ini` pulls in through `extra_configs`.

## Dependencies
- Declare every library in `platformio.ini` under `lib_deps` with a pinned
  version range.

## Git
- Write concise commit messages in English using Conventional Commits
  (`feat:`, `fix:`, `refactor:`, `docs:`, `chore:`).
- Never force-push to `main`.
