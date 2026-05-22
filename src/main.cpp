#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

#ifndef WIFI_SSID
#include "secrets.h"
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
#define MAX_ARTICLES 25
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

// Timing variables
unsigned long lastNewsSwitch = 0;
unsigned long categoryStartTime = 0;
const unsigned long NEWS_INTERVAL = 60 * 1000UL; // 1 minute

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

    // NewsAPI is served over HTTPS; setInsecure() skips TLS certificate
    // validation to avoid bundling a CA cert. Traffic is still encrypted.
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(15000);

    int httpCode = http.GET();
    totalArticles = 0;
    currentArticle = 0;

    Serial.printf("HTTP Code: %d\n", httpCode);

    if (httpCode != 200) {
        Serial.printf("HTTP Error: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("Payload size: %d bytes\n", payload.length());
    Serial.printf("Memoria tras payload: %d bytes\n", ESP.getFreeHeap());

    // JSON parsing with filter to reduce memory usage
    JsonDocument filter;
    filter["totalResults"] = true;
    filter["articles"][0]["title"] = true;
    filter["articles"][0]["description"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload,
        DeserializationOption::Filter(filter));

    // Clear payload string to free memory before parsing JSON
    payload = String();

    if (error) {
        Serial.printf("JSON Error: %s\n", error.c_str());
        return false;
    }

    Serial.printf("Total resultados API: %d\n", doc["totalResults"].as<int>());

    JsonArray arr = doc["articles"].as<JsonArray>();
    int count = 0;

    for (JsonObject article : arr) {
        if (count >= MAX_ARTICLES) break;

        const char* title = article["title"] | "Sin título";
        const char* desc = article["description"] | "Sin descripción";

        strncpy(articles[count].title, title, MAX_TITLE - 1);
        articles[count].title[MAX_TITLE - 1] = '\0';

        strncpy(articles[count].description, desc, MAX_DESC - 1);
        articles[count].description[MAX_DESC - 1] = '\0';

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

// Switch to next category
void nextCategory() {
    currentCategory++;

    if (currentCategory >= totalCategories) {
        currentCategory = 0; // Restart cycle
        Serial.println("Ciclo completo, reiniciando...");
    }

    Serial.printf("Cambiando a categoría: %s\n", categories[currentCategory].query);
    fetchNews(categories[currentCategory].query);
    categoryStartTime = millis();
    lastNewsSwitch = millis();
    displayArticle(0);
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

    // Load first category
    fetchNews(categories[0].query);
    categoryStartTime = millis();
    lastNewsSwitch = millis();

    if (totalArticles > 0) {
        displayArticle(0);
    }
}

// Main loop
void loop() {
    unsigned long now = millis();

    // Check whether it's time to change category
    if (now - categoryStartTime >= categories[currentCategory].durationMs) {
        nextCategory();
    }

    // Check whether it's time to change article (every 1 minute)
    if (now - lastNewsSwitch >= NEWS_INTERVAL) {
        lastNewsSwitch = now;
        currentArticle++;

        // If articles finished, wrap to start or change category
        if (currentArticle >= totalArticles) {
            currentArticle = 0;
        }

        displayArticle(currentArticle);
    }

    delay(100);
}
