#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>


// Wi-Fi
const char* WIFI_SSID = "DRDOOM";
const char* WIFI_PASSWORD = "i-am-doom";

// Network time
const char* NTP_SERVER = "pool.ntp.org";

const long GMT_OFFSET_SEC = 8 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;


// Open-Meteo API
const char* WEATHER_URL =
  "https://api.open-meteo.com/v1/forecast?"
  "latitude=14.6042&"
  "longitude=120.9822&"
  "hourly=temperature_2m,relative_humidity_2m,rain,soil_moisture_0_to_1cm";


// Setup
void setup() {

  Serial.begin(115200);


  //Connect to Wi-Fi
  Serial.println();
  Serial.println("Connecting to Wi-Fi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");

  //Print IP and RSSI
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  //Get network time
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

  //Request weather forecast
  Serial.println();
  Serial.println("Requesting weather forecast...");

  HTTPClient http;

  http.begin(WEATHER_URL);

  int httpCode = http.GET();

  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {

    String payload = http.getString();

    //Print raw response
    Serial.println();
    Serial.println("RAW API RESPONSE:");
    Serial.print(payload);
    
    
    //Parse JSON
    DynamicJsonDocument doc(30000);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {

      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());

    } else {

      Serial.println();
      Serial.println("JSON parsing successful!");

      //hourly data
      JsonObject hourly = doc["hourly"];

      // Get first hour's values
      const char* time = hourly["time"][0];

      float temperature =
        hourly["temperature_2m"][0];

      float humidity =
        hourly["relative_humidity_2m"][0];

      float rain =
        hourly["rain"][0];

      float soilMoisture =
        hourly["soil_moisture_0_to_1cm"][0];

      // Display parsed values
      Serial.println();
      Serial.println("FIRST HOURLY FORECAST:");

      Serial.print("Time: ");
      Serial.println(time);

      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.println(" °C");

      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");

      Serial.print("Rain: ");
      Serial.print(rain);
      Serial.println(" mm");

      Serial.print("Soil Moisture: ");
      Serial.println(soilMoisture);
    }
  } else {
    Serial.print("HTTP request failed: ");
    Serial.println(http.errorToString(httpCode));
  }
  http.end();
}
void loop() {
}