#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// Wi-Fi
const char* WIFI_SSID = "DRDOOM";
const char* WIFI_PASSWORD = "i-am-doom";

// Network Time
const char* NTP_SERVER = "pool.ntp.org";

const long GMT_OFFSET_SEC = 8 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// Open-Meteo, Manila
const char* WEATHER_URL =
  "https://api.open-meteo.com/v1/forecast?"
  "latitude=14.6042&"
  "longitude=120.9822&"
  "hourly=temperature_2m,relative_humidity_2m,"
  "rain,soil_moisture_0_to_1cm,precipitation_probability";


// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// If rain probability reaches this value,
// irrigation should be held.
const int RAIN_HOLD_PCT = 1;

// Forecast becomes stale after 3 hours.
const unsigned long FORECAST_MAX_AGE =
  3UL * 60UL * 60UL * 1000UL;

// Fetch new forecast every 10 minutes.
const unsigned long FORECAST_FETCH_INTERVAL =
  10UL * 60UL * 1000UL;

// Forecast Variables
int rainProbability = 0;

float forecastTemperature = 0;
float forecastHumidity = 0;
float forecastRain = 0;
float forecastSoilMoisture = 0;

String forecastTime = "";

unsigned long forecastFetchedAt = 0;
unsigned long lastForecastFetch = 0;

bool forecastAvailable = false;

// Connect to Wi-Fi
void connectWiFi() {

  Serial.println();
  Serial.println("Connecting to Wi-Fi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

// Get Network Time
void setupTime() {

  configTime(
    GMT_OFFSET_SEC,
    DAYLIGHT_OFFSET_SEC,
    NTP_SERVER
  );

  Serial.println("Getting network time...");

  struct tm timeInfo;

  if (getLocalTime(&timeInfo)) {

    Serial.println("Time synchronized!");

    Serial.print("Date: ");

    Serial.printf(
      "%04d-%02d-%02d\n",
      timeInfo.tm_year + 1900,
      timeInfo.tm_mon + 1,
      timeInfo.tm_mday
    );

    Serial.print("Time: ");

    Serial.printf(
      "%02d:%02d:%02d\n",
      timeInfo.tm_hour,
      timeInfo.tm_min,
      timeInfo.tm_sec
    );

  } else {

    Serial.println("Failed to obtain time.");
  }
}

// Fetch Weather Forecast
bool fetchForecast() {

  Serial.println();
  Serial.println("Requesting weather forecast...");

  HTTPClient http;

  http.begin(WEATHER_URL);

  int httpCode = http.GET();

  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);

  // Check HTTP request
  if (httpCode <= 0) {

    Serial.print("HTTP request failed: ");
    Serial.println(http.errorToString(httpCode));

    http.end();

    return false;
  }

  // Get response
  String payload = http.getString();

  Serial.print("Response size: ");
  Serial.print(payload.length());
  Serial.println(" bytes");

  // Parse JSON
  DynamicJsonDocument doc(30000);

  DeserializationError error =
    deserializeJson(doc, payload);

  if (error) {

    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());

    http.end();

    return false;
  }

  Serial.println("JSON parsing successful!");


  // Access hourly data
  JsonObject hourly = doc["hourly"];

  // First hourly forecast
  forecastTime =
    hourly["time"][0].as<String>();

  forecastTemperature =
    hourly["temperature_2m"][0];

  forecastHumidity =
    hourly["relative_humidity_2m"][0];

  forecastRain =
    hourly["rain"][0];

  forecastSoilMoisture =
    hourly["soil_moisture_0_to_1cm"][0];

  rainProbability =
    hourly["precipitation_probability"][0];

  // Record successful fetch time
  forecastFetchedAt = millis();
  lastForecastFetch = millis();
  forecastAvailable = true;

  // Print parsed data
  Serial.println();
  Serial.println("========== FORECAST ==========");

  Serial.print("Forecast time: ");
  Serial.println(forecastTime);

  Serial.print("Temperature: ");
  Serial.print(forecastTemperature);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(forecastHumidity);
  Serial.println(" %");

  Serial.print("Rain: ");
  Serial.print(forecastRain);
  Serial.println(" mm");

  Serial.print("Soil moisture: ");
  Serial.println(forecastSoilMoisture);

  Serial.print("Rain probability: ");
  Serial.print(rainProbability);
  Serial.println(" %");

  Serial.println("==============================");

  http.end();

  return true;
}

// Check Forecast Age
bool forecastIsFresh() {

  if (!forecastAvailable) {
    return false;
  }

  unsigned long age =
    millis() - forecastFetchedAt;

  return age <= FORECAST_MAX_AGE;
}

// Display Forecast Information
void updateLCD() {

  lcd.clear();

  // Calculate forecast age
  unsigned long ageSeconds = 0;

  if (forecastAvailable) {

    ageSeconds =
      (millis() - forecastFetchedAt) / 1000UL;
  }

  unsigned long ageMinutes =
    ageSeconds / 60UL;


  // Line 1: Rain probability
  lcd.setCursor(0, 0);

  lcd.print("Rain:");
  lcd.print(rainProbability);
  lcd.print("%");

  // Line 2: Forecast age
  lcd.setCursor(0, 1);

  lcd.print("Age:");
  lcd.print(ageMinutes);
  lcd.print("m ");

  // Fresh / stale indicator
  if (forecastIsFresh()) {

    lcd.print("FRESH");

  } else {

    lcd.print("STALE");
  }
}

// Check Rain Hold
void checkRainCondition() {

  Serial.println();

  if (!forecastAvailable) {

    Serial.println(
      "No forecast available."
    );

    Serial.println(
      "Weather cannot authorize irrigation."
    );

    return;
  }

  if (!forecastIsFresh()) {

    Serial.println(
      "Forecast is STALE."
    );

    Serial.println(
      "Use local sensors."
    );

    return;
  }

  if (rainProbability >= RAIN_HOLD_PCT) {

    Serial.println(
      "RAIN HOLD - irrigation suppressed."
    );

  } else {

    Serial.println(
      "NO RAIN HOLD - weather allows local decision."
    );
  }
}

void setup() {

  Serial.begin(115200);

  // LCD
  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Farming");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  delay(1500);

  // Wi-Fi
  connectWiFi();

  // Network time
  setupTime();

  // First forecast fetch
  if (fetchForecast()) {

    Serial.println(
      "Initial forecast received."
    );

  } else {

    Serial.println(
      "Initial forecast failed."
    );
  }

  // Display initial data
  updateLCD();

  // Check rain condition
  checkRainCondition();
}

void loop() {

  // Fetch forecast periodically
  if (
    forecastAvailable == false ||
    millis() - lastForecastFetch >=
    FORECAST_FETCH_INTERVAL
  ) {

    if (WiFi.status() == WL_CONNECTED) {

      if (fetchForecast()) {

        Serial.println(
          "Scheduled forecast update successful."
        );

      } else {

        Serial.println(
          "Scheduled forecast update failed."
        );
      }

    } else {

      Serial.println(
        "Wi-Fi disconnected. Forecast not updated."
      );
    }

    checkRainCondition();
  }

  // Update LCD

  updateLCD();

  delay(1000);
}