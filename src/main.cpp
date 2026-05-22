#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <stdlib.h>
#include <strings.h>

#ifndef WIFI_SSID
#include "secrets.h"
#endif

// WEATHER_COUNTRY is optional; it disambiguates cities sharing the same name
#ifndef WEATHER_COUNTRY
#define WEATHER_COUNTRY ""
#endif

TFT_eSPI tft = TFT_eSPI();

// WiFi configuration
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

// NewsAPI configuration
const char* apiKey = API_KEY;
const char* apiBase = "https://newsapi.org/v2/everything?sortBy=popularity&language=es&apiKey=";

//Struct to hold news category and its respective duration for fetching news
struct NewsCategory {
    const char* query;
    unsigned long durationMs;
};

//12 hrs news from NewsAPI, categories and their respective durations in milliseconds
NewsCategory categories[] = {
    {"Tech",     1 * 60 * 60 * 1000UL},
    {"Space",    1 * 60 * 60 * 1000UL},
    {"AI",       1 * 60 * 60 * 1000UL},
    {"Software", 1 * 60 * 60 * 1000UL},
    {"Economy",  1 * 60 * 60 * 1000UL},
};
const int totalCategories = sizeof(categories) / sizeof(categories[0]);

// Struct article data
#define MAX_ARTICLES 12
#define MAX_TITLE 150
#define MAX_DESC 300

struct Article {
    char title[MAX_TITLE];
    char description[MAX_DESC];
};

Article articles[MAX_ARTICLES];
int totalArticles = 0;
int currentArticle = 0;
int currentCategory = 0;

// Current weather fetched from Open-Meteo
struct WeatherData {
    float temperature;       // degrees Celsius
    int humidity;            // relative humidity, %
    int precipitationProb;   // precipitation probability for the current hour, %
    bool valid;
};
WeatherData weather = {0.0f, 0, 0, false};

// Device location, resolved once at startup from the configured city
double geoLat = 0.0;
double geoLon = 0.0;
bool geoValid = false;

// World clocks shown on the info screen.
// POSIX TZ strings: the device keeps UTC from NTP and these handle DST automatically.
struct WorldClock {
    const char* label;
    const char* tz;
};
const WorldClock worldClocks[] = {
    {"Colombia", "COT5"},
    {"Espana",   "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Uruguay",  "UYT3"},
    {"Texas",    "CST6CDT,M3.2.0,M11.1.0"},
};
const int totalClocks = sizeof(worldClocks) / sizeof(worldClocks[0]);

// Timing variables
unsigned long lastScreenSwitch = 0;
unsigned long categoryStartTime = 0;
unsigned long lastWeatherFetch = 0;
bool showingInfo = false;

const unsigned long SCREEN_INTERVAL = 60 * 1000UL;        // alternate screens every 1 minute
const unsigned long WEATHER_INTERVAL = 30 * 60 * 1000UL;  // refresh weather every 30 minutes

// Connect to WiFi
void connectWiFi() {
    tft.fillScreen(TFT_WHITE);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.drawString("Conectando WiFi...", 10, 10, 2);
    Serial.println("Conectando WiFi...");

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi conectado");
    Serial.println(WiFi.localIP());

    // Synchronize time with NTP
    tft.fillScreen(TFT_WHITE);
    tft.drawString("Sincronizando hora...", 10, 10, 2);
    Serial.println("Sincronizando hora con NTP...");

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    time_t now = time(nullptr);
    int attempts = 0;
    while (now < 24 * 3600 && attempts < 20) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        attempts++;
    }
    Serial.println();

    tft.fillScreen(TFT_WHITE);
    tft.drawString("WiFi OK!", 10, 10, 2);
    tft.drawString(WiFi.localIP().toString().c_str(), 10, 30, 1);
    delay(2000);
}

// Get yesterday's date in YYYY-MM-DD format
void getYesterdayDateString(char* buffer, size_t bufferSize) {
    time_t now = time(nullptr);

    // If time is not synchronized, use hardcoded date
    if (now < 24 * 3600) {
        strncpy(buffer, "2026-05-20", bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        Serial.println("Advertencia: Hora no sincronizada, usando fecha por defecto");
        return;
    }

    time_t yesterday = now - (24 * 60 * 60);
    struct tm* timeinfo = localtime(&yesterday);

    if (timeinfo == nullptr) {
        strncpy(buffer, "2026-05-20", bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        Serial.println("Error: No se pudo obtener estructura de hora");
        return;
    }

    strftime(buffer, bufferSize, "%Y-%m-%d", timeinfo);
}

// Perform an HTTPS GET and deserialize the JSON body using the given filter.
// setInsecure() skips TLS certificate validation to avoid bundling a CA cert;
// traffic is still encrypted.
bool httpGetJson(const char* url, JsonDocument& doc, JsonDocument& filter) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(15000);

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("HTTP Error %d: %s\n", httpCode, url);
        http.end();
        return false;
    }

    DeserializationError error;
    if (http.getSize() >= 0) {
        // Content-Length is known: parse straight from the stream so the whole
        // body never has to live in one large contiguous String.
        error = deserializeJson(doc, http.getStream(),
            DeserializationOption::Filter(filter));
    } else {
        // Chunked transfer: HTTPClient must de-chunk the body via getString().
        String payload = http.getString();
        error = deserializeJson(doc, payload,
            DeserializationOption::Filter(filter));
        payload = String();
    }

    http.end();

    if (error) {
        Serial.printf("JSON Error: %s\n", error.c_str());
        return false;
    }

    return true;
}

// Percent-encode a string for safe use in a URL query parameter
void urlEncode(const char* src, char* dst, size_t dstSize) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 3 < dstSize; i++) {
        unsigned char c = (unsigned char) src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = (char) c;
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0x0F];
        }
    }
    dst[j] = '\0';
}

// Resolve the configured city to coordinates using the Open-Meteo geocoding API
bool resolveLocation() {
    char cityEncoded[120];
    urlEncode(WEATHER_CITY, cityEncoded, sizeof(cityEncoded));

    char url[256];
    snprintf(url, sizeof(url),
        "https://geocoding-api.open-meteo.com/v1/search"
        "?name=%s&count=5&language=es&format=json",
        cityEncoded);

    Serial.printf("Geocoding: %s\n", url);

    JsonDocument filter;
    filter["results"][0]["latitude"] = true;
    filter["results"][0]["longitude"] = true;
    filter["results"][0]["name"] = true;
    filter["results"][0]["country"] = true;
    filter["results"][0]["country_code"] = true;
    filter["results"][0]["admin1"] = true;

    JsonDocument doc;
    if (!httpGetJson(url, doc, filter)) {
        Serial.println("Error: no se pudo geocodificar la ciudad");
        geoValid = false;
        return false;
    }

    JsonArray results = doc["results"].as<JsonArray>();
    if (results.isNull() || results.size() == 0) {
        Serial.printf("Error: ciudad no encontrada: %s\n", WEATHER_CITY);
        geoValid = false;
        return false;
    }

    // When WEATHER_COUNTRY is set, prefer the result from that country
    JsonObject match = results[0];
    if (strlen(WEATHER_COUNTRY) > 0) {
        for (JsonObject result : results) {
            const char* country = result["country"] | "";
            const char* code = result["country_code"] | "";
            if (strcasecmp(country, WEATHER_COUNTRY) == 0 ||
                strcasecmp(code, WEATHER_COUNTRY) == 0) {
                match = result;
                break;
            }
        }
    }

    geoLat = match["latitude"].as<double>();
    geoLon = match["longitude"].as<double>();
    geoValid = true;

    Serial.printf("Ubicacion: %s, %s (%.4f, %.4f)\n",
        match["name"] | WEATHER_CITY, match["admin1"] | "-", geoLat, geoLon);
    return true;
}

// Fetch current weather from Open-Meteo for the resolved location
bool fetchWeather() {
    if (!geoValid) {
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m"
        "&hourly=precipitation_probability&forecast_days=1&timezone=auto",
        geoLat, geoLon);

    Serial.printf("Fetching weather: %s\n", url);

    JsonDocument filter;
    filter["current"]["time"] = true;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["hourly"]["precipitation_probability"] = true;

    JsonDocument doc;
    if (!httpGetJson(url, doc, filter)) {
        Serial.println("Error: no se pudo obtener el clima");
        weather.valid = false;
        return false;
    }

    weather.temperature = doc["current"]["temperature_2m"].as<float>();
    weather.humidity = doc["current"]["relative_humidity_2m"].as<int>();

    // precipitation_probability is an hourly series; pick the entry for the
    // current hour, which equals the array index when forecast_days=1.
    const char* currentTime = doc["current"]["time"] | "";
    int hour = 0;
    if (strlen(currentTime) >= 13) {
        hour = (currentTime[11] - '0') * 10 + (currentTime[12] - '0');
    }

    JsonArray probs = doc["hourly"]["precipitation_probability"].as<JsonArray>();
    weather.precipitationProb = 0;
    if (hour >= 0 && hour < (int) probs.size()) {
        weather.precipitationProb = probs[hour].as<int>();
    }

    weather.valid = true;
    Serial.printf("Clima: %.1fC, humedad %d%%, lluvia %d%%\n",
        weather.temperature, weather.humidity, weather.precipitationProb);
    Serial.printf("Memoria libre: %d bytes\n", ESP.getFreeHeap());

    return true;
}

// Format the current time for a POSIX timezone string as HH:MM
void formatTimeInZone(const char* tz, char* buffer, size_t bufferSize) {
    time_t now = time(nullptr);

    // If time is not synchronized yet, show a placeholder
    if (now < 24 * 3600) {
        strncpy(buffer, "--:--", bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        return;
    }

    setenv("TZ", tz, 1);
    tzset();

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(buffer, bufferSize, "%H:%M", &timeinfo);
}

// Draw text with word wrap
int drawWrappedText(const char* text, int x, int y, int maxWidth, int font) {
    String words = String(text);
    String line = "";
    int lineY = y;
    int spaceWidth = tft.textWidth(" ", font);
    int lineHeight = tft.fontHeight(font) + 4;

    int i = 0;
    while (i < words.length()) {
        // Extract next word
        String word = "";
        while (i < words.length() && words[i] != ' ') {
            word += words[i];
            i++;
        }
        i++; // skip space

        // Check if the word fits in the line
        String testLine = line.length() > 0 ? line + " " + word : word;

        if (tft.textWidth(testLine.c_str(), font) > maxWidth) {
            // Print current line and start a new one
            if (line.length() > 0) {
                tft.drawString(line.c_str(), x, lineY, font);
                lineY += lineHeight;
            }
            line = word;
        } else {
            line = testLine;
        }

        // Limit to screen
        if (lineY > 220) break;
    }

    // Print last line
    if (line.length() > 0 && lineY <= 220) {
        tft.drawString(line.c_str(), x, lineY, font);
        lineY += lineHeight;
    }

    return lineY;
}

// Convert a UTF-8 string to plain ASCII, folding accented Latin letters to
// their base letter (á -> a, ñ -> n, ...). The built-in TFT fonts only
// contain ASCII glyphs, so accented bytes would otherwise render as garbage.
void asciiFold(char* dst, size_t dstSize, const char* src) {
    static const char latin1[] =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPs"
        "aaaaaaaceeeeiiiidnooooo/ouuuuypy";

    size_t j = 0;
    size_t i = 0;
    while (src[i] != '\0' && j + 1 < dstSize) {
        unsigned char c = (unsigned char) src[i];

        if (c < 0x80) {
            // Plain ASCII passes through unchanged
            dst[j++] = (char) c;
            i++;
            continue;
        }

        // Multi-byte UTF-8: choose a replacement, then skip the whole sequence
        char replacement = ' ';
        if (c == 0xC3) {
            // Latin-1 Supplement U+00C0..U+00FF -> base letter
            unsigned char cont = (unsigned char) src[i + 1];
            if (cont >= 0x80 && cont <= 0xBF) {
                replacement = latin1[cont & 0x3F];
            }
        } else if (c == 0xC2) {
            unsigned char cont = (unsigned char) src[i + 1];
            if (cont == 0xBF) replacement = '?';        // inverted question mark
            else if (cont == 0xA1) replacement = '!';   // inverted exclamation
        } else if (c == 0xE2 && (unsigned char) src[i + 1] == 0x80) {
            // General punctuation: dashes, smart quotes, ellipsis
            unsigned char b = (unsigned char) src[i + 2];
            if (b >= 0x90 && b <= 0x95) replacement = '-';
            else if (b == 0x98 || b == 0x99) replacement = '\'';
            else if (b == 0x9C || b == 0x9D) replacement = '"';
            else if (b == 0xA6) replacement = '.';
        }
        dst[j++] = replacement;

        // Advance past the lead byte and any UTF-8 continuation bytes (0x80..0xBF)
        i++;
        while ((unsigned char) src[i] >= 0x80 && (unsigned char) src[i] <= 0xBF) {
            i++;
        }
    }
    dst[j] = '\0';
}

// Fetch news from API
bool fetchNews(const char* query) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi desconectado, reconectando...");
        connectWiFi();
    }

    char dateFrom[11];
    getYesterdayDateString(dateFrom, sizeof(dateFrom));

    char url[256];
    snprintf(url, sizeof(url), "%s%s&q=%s&from=%s&pageSize=10", apiBase, apiKey, query, dateFrom);

    Serial.printf("Fetching: %s\n", url);
    Serial.printf("Memoria libre: %d bytes\n", ESP.getFreeHeap());

    tft.fillScreen(TFT_WHITE);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);

    char buffer[50];
    snprintf(buffer, sizeof(buffer), "Cargando: %s...", query);
    tft.drawString(buffer, 10, 10, 2);

    totalArticles = 0;
    currentArticle = 0;

    // JSON parsing with filter to reduce memory usage
    JsonDocument filter;
    filter["totalResults"] = true;
    filter["articles"][0]["title"] = true;
    filter["articles"][0]["description"] = true;

    JsonDocument doc;
    if (!httpGetJson(url, doc, filter)) {
        return false;
    }

    Serial.printf("Total resultados API: %d\n", doc["totalResults"].as<int>());

    JsonArray arr = doc["articles"].as<JsonArray>();
    int count = 0;

    for (JsonObject article : arr) {
        if (count >= MAX_ARTICLES) break;

        const char* title = article["title"] | "Sin titulo";
        const char* desc = article["description"] | "Sin descripcion";

        // Fold UTF-8 accents to ASCII — the TFT fonts have no accented glyphs
        asciiFold(articles[count].title, MAX_TITLE, title);
        asciiFold(articles[count].description, MAX_DESC, desc);

        Serial.printf("  Artículo %d: %s\n", count + 1, articles[count].title);
        count++;
    }

    totalArticles = count;
    Serial.printf("Cargadas %d noticias sobre '%s'\n", totalArticles, query);

    return totalArticles > 0;
}

// Display article on screen
void displayArticle(int index) {
    if (index >= totalArticles) return;

    tft.fillScreen(TFT_WHITE);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);

    // Header with category and counter
    char header[60];
    snprintf(header, sizeof(header), "%s [%d/%d]",
        categories[currentCategory].query,
        index + 1,
        totalArticles
    );
    tft.drawString(header, 10, 4, 1);

    // Separator line
    tft.drawLine(5, 20, 315, 20, TFT_BLACK);

    // Title
    tft.setTextColor(TFT_BLUE, TFT_WHITE);
    int nextY = drawWrappedText(articles[index].title, 10, 30, 300, 2);

    // Description
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    drawWrappedText(articles[index].description, 10, nextY + 10, 300, 2);

    Serial.printf("Mostrando [%d/%d]: %s\n", index + 1, totalArticles, articles[index].title);
}

// Display the combined weather + world clock screen
void displayInfoScreen() {
    tft.fillScreen(TFT_WHITE);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);

    // Header
    tft.drawString("Clima y Hora Mundial", 10, 4, 2);
    tft.drawLine(5, 24, 315, 24, TFT_BLACK);

    int y = 32;

    // Weather block
    if (weather.valid) {
        tft.setTextColor(TFT_BLUE, TFT_WHITE);
        tft.drawString(WEATHER_CITY, 10, y, 2);
        y += 22;

        char line[48];
        snprintf(line, sizeof(line), "%.1f C", weather.temperature);
        tft.setTextColor(TFT_RED, TFT_WHITE);
        tft.drawString(line, 10, y, 4);
        y += 32;

        snprintf(line, sizeof(line), "Humedad %d%%   Lluvia %d%%",
            weather.humidity, weather.precipitationProb);
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.drawString(line, 10, y, 2);
        y += 20;
    } else {
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.drawString("Clima no disponible", 10, y, 2);
        y += 20;
    }

    // Separator
    tft.drawLine(5, y + 2, 315, y + 2, TFT_BLACK);
    y += 12;

    // World clocks: labels in black, times in yellow and right-aligned near the edge
    for (int i = 0; i < totalClocks; i++) {
        char timeStr[8];
        formatTimeInZone(worldClocks[i].tz, timeStr, sizeof(timeStr));

        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        tft.drawString(worldClocks[i].label, 10, y, 4);

        tft.setTextColor(TFT_YELLOW, TFT_WHITE);
        int timeX = 310 - tft.textWidth(timeStr, 4);
        tft.drawString(timeStr, timeX, y, 4);

        y += 28;
    }

    // Restore UTC so the rest of the firmware computes dates consistently
    setenv("TZ", "UTC0", 1);
    tzset();

    Serial.println("Mostrando pantalla de clima y hora");
}

// Show the current article and advance to the next one
void showNewsScreen() {
    displayArticle(currentArticle);
    currentArticle++;
    if (currentArticle >= totalArticles) {
        currentArticle = 0;
    }
}

// Switch to the next news category (refetches articles, does not draw)
void nextCategory() {
    currentCategory++;

    if (currentCategory >= totalCategories) {
        currentCategory = 0; // Restart cycle
        Serial.println("Ciclo completo, reiniciando...");
    }

    Serial.printf("Cambiando a categoría: %s\n", categories[currentCategory].query);
    fetchNews(categories[currentCategory].query);
    categoryStartTime = millis();
}

// Setup
void setup() {
    Serial.begin(115200);

    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);

    tft.init();
    tft.setRotation(1);

    connectWiFi();

    // Resolve location and fetch the first weather report
    resolveLocation();
    if (geoValid) {
        fetchWeather();
        lastWeatherFetch = millis();
    }

    // Load first category
    fetchNews(categories[0].query);
    categoryStartTime = millis();
    lastScreenSwitch = millis();

    // Start by showing the weather + clock screen
    showingInfo = true;
    displayInfoScreen();
}

// Main loop
void loop() {
    unsigned long now = millis();

    // Refresh weather data periodically
    if (geoValid && now - lastWeatherFetch >= WEATHER_INTERVAL) {
        fetchWeather();
        lastWeatherFetch = now;
    }

    // Every minute, alternate between the news and the weather + clock screen
    if (now - lastScreenSwitch >= SCREEN_INTERVAL) {
        lastScreenSwitch = now;

        // After a full category cycle (1 hour), move on to the next category
        if (now - categoryStartTime >= categories[currentCategory].durationMs) {
            nextCategory();
        }

        showingInfo = !showingInfo;
        if (showingInfo) {
            displayInfoScreen();
        } else {
            showNewsScreen();
        }
    }

    delay(100);
}
