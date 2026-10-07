#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

const int MOIST_PIN = 34;
const int RAIN_PIN = 36;
const int OVERRIDE_PIN = 19;
const int DHT_PIN = 32;

// Water level sensor
const int WATER_LEVEL_PIN = 35;

// State LEDs
const int IRRIGATE_LED = 16;
const int HOLD_LED = 17;
const int RAIN_LED = 4;
const int MANUAL_LED = 2;

// Tank empty buzzer
const int BUZZER_PIN = 15;

#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

const int PUMP_PIN = 23;
const int VALVE_PIN = 5;

// Active LOW relay
const int RELAY_ON = LOW;
const int RELAY_OFF = HIGH;

// Valve settling time before pump starts/stops
const unsigned long SETTLE_MS = 1500;

// Hysteresis margin
const int HYSTERESIS_MARGIN = 8;

// Firmware absolute maximum runtime
const int MAX_RUNTIME_MINUTES = 20;

// SOIL MOISTURE CALIBRATION
const int RAW_DRY = 4095;
const int RAW_WET = 2700;

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// KEYPAD
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

byte rowPins[ROWS] = {13, 14, 27, 26};
byte colPins[COLS] = {25, 33, 18};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);

// OVERRIDE
volatile bool overrideFlag = false;
volatile unsigned long lastIsr = 0;
volatile unsigned long overrideTime = 0;

bool manualOverride = false;

// Hysteresis state
bool moistureIrrigation = false;

// SETTINGS
struct Settings {
  int runMinutes;
  int setpointPct;
  int startHour;
};

Settings cfg = {5, 40, 6};

Preferences prefs;

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

// Weather rain hold
const int RAIN_HOLD_PCT = 1;

// Forecast becomes stale after 3 hours.
const unsigned long FORECAST_MAX_AGE =
  3UL * 60UL * 60UL * 1000UL;

// Fetch new forecast every 10 minutes.
const unsigned long FORECAST_FETCH_INTERVAL =
  10UL * 60UL * 1000UL;

int rainProbability = 0;

float forecastTemperature = 0;
float forecastHumidity = 0;
float forecastRain = 0;
float forecastSoilMoisture = 0;

String forecastTime = "";

unsigned long forecastFetchedAt = 0;
unsigned long lastForecastFetch = 0;

bool forecastAvailable = false;
bool forecastStaleDueToWiFi = false;
bool wifiWasConnected = false;

// MENU VARIABLES
int menuItem = 0;

bool menuActive = false;
bool enteringValue = false;

String inputValue = "";

// FARM STATES
enum FarmState {
  S_HOLD,
  S_IRRIGATE,
  S_RAIN,
  S_MANUAL
};

// ACTUATOR STATES
enum ActuatorPhase {
  PH_IDLE,
  PH_OPENING,
  PH_RUNNING,
  PH_CLOSING
};

ActuatorPhase actuatorPhase = PH_IDLE;
unsigned long phaseStartTime = 0;

// TIMED IRRIGATION
unsigned long irrigationStartTime = 0;
unsigned long irrigationDuration = 0;

bool irrigationActive = false;

unsigned long lastDisplayedSecond = 999999;

// OVERRIDE INTERRUPT
void IRAM_ATTR onOverride() {

  unsigned long now = millis();
  if (now - lastIsr < 200) {
    return;
  }

  lastIsr = now;

  overrideFlag = true;
  overrideTime = now;
}


// SOIL MOISTURE
int readMoistureRaw(int n = 10) {

  long sum = 0;

  for (int i = 0; i < n; i++) {

    sum += analogRead(MOIST_PIN);

    delay(5);
  }

  return sum / n;
}

float readMoisturePct() {

  int raw = readMoistureRaw();

  float pct =
    100.0 * (RAW_DRY - raw) /
    (float)(RAW_DRY - RAW_WET);

  return constrain(pct, 0.0, 100.0);
}

// RAIN SENSOR
bool isRaining() {

  return digitalRead(RAIN_PIN) == LOW;
}

// WATER LEVEL
bool tankEmpty() {

  return digitalRead(WATER_LEVEL_PIN) == LOW;
}

// UPDATE STATE LEDs
void updateStateLEDs(FarmState state) {

  digitalWrite(IRRIGATE_LED, LOW);
  digitalWrite(HOLD_LED, LOW);
  digitalWrite(RAIN_LED, LOW);
  digitalWrite(MANUAL_LED, LOW);

  if (state == S_IRRIGATE) {

    digitalWrite(IRRIGATE_LED, HIGH);
  }

  else if (state == S_HOLD) {

    digitalWrite(HOLD_LED, HIGH);
  }

  else if (state == S_RAIN) {

    digitalWrite(RAIN_LED, HIGH);
  }

  else if (state == S_MANUAL) {

    digitalWrite(MANUAL_LED, HIGH);
  }
}

// UPDATE TANK BUZZER
void updateTankBuzzer() {

  if (tankEmpty()) {

    digitalWrite(BUZZER_PIN, HIGH);

  }

  else {

    digitalWrite(BUZZER_PIN, LOW);
  }
}

// DECIDE FARM STATE
FarmState decide() {

  float moisture = readMoisturePct();

  if (manualOverride) {

    return S_MANUAL;
  }

  if (isRaining()) {

    return S_RAIN;
  }

  if (weatherRainHold()) {

    return S_RAIN;
  }

  if (moistureIrrigation) {

    if (moisture >= cfg.setpointPct + HYSTERESIS_MARGIN) {

      moistureIrrigation = false;
    }
  }

  else {

    if (moisture < cfg.setpointPct) {

      moistureIrrigation = true;
    }
  }

  if (moistureIrrigation) {

    return S_IRRIGATE;
  }

  return S_HOLD;
}

// DISPLAY FARM STATE
void displayState(FarmState state) {

  lcd.clear();

  if (state == S_MANUAL) {
    lcd.setCursor(0, 0);
    lcd.print("State: MANUAL");
    lcd.setCursor(0, 1);
    lcd.print("By: Override");
  }

  else if (state == S_RAIN) {
    lcd.setCursor(0, 0);
    lcd.print("State: RAIN");
    lcd.setCursor(0, 1);
    lcd.print("By: Rain Sensor");
  }

  else if (state == S_IRRIGATE) {
    lcd.setCursor(0, 0);
    lcd.print("State: IRRIGATE");
    lcd.setCursor(0, 1);
    lcd.print("By: Soil Moist");
  }

  else {
    lcd.setCursor(0, 0);
    lcd.print("State: HOLD");
    lcd.setCursor(0, 1);
    lcd.print("By: Moisture OK");
  }
}


// SAVE SETTINGS
void saveSettings() {

  prefs.begin("irrig", false);

  prefs.putInt("run", cfg.runMinutes);
  prefs.putInt("sp", cfg.setpointPct);
  prefs.putInt("hr", cfg.startHour);

  prefs.end();
}

// LOAD SETTINGS
void loadSettings() {

  prefs.begin("irrig", true);

  cfg.runMinutes = prefs.getInt("run", 5);
  cfg.setpointPct = prefs.getInt("sp", 40);
  cfg.startHour = prefs.getInt("hr", 6);

  prefs.end();
}

// APPLY SETTING
bool applySetting(int which, int value) {

  switch (which) {

    case 0:

      if (value < 1 || value > 30) {
        return false;
      }

      cfg.runMinutes = value;

      break;

    case 1:

      if (value < 10 || value > 90) {
        return false;
      }

      cfg.setpointPct = value;

      break;

    case 2:

      if (value < 0 || value > 23) {
        return false;
      }

      cfg.startHour = value;

      break;
  }

  saveSettings();

  return true;
}

// CONNECT TO WI-FI
void connectWiFi() {

  Serial.println();
  Serial.println("Connecting to Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 10000UL
  ) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("WiFi connected!");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    wifiWasConnected = true;

  }

  else {

    Serial.println("WiFi unavailable. Using local sensors.");
  }
}

// GET NETWORK TIME
void setupTime() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  configTime(
    GMT_OFFSET_SEC,
    DAYLIGHT_OFFSET_SEC,
    NTP_SERVER
  );

  Serial.println("Getting network time...");

  struct tm timeInfo;

  if (getLocalTime(&timeInfo)) {

    Serial.println("Time synchronized!");

  } else {

    Serial.println("Failed to obtain time.");
  }
}

// FETCH WEATHER FORECAST
bool fetchForecast() {

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("Wi-Fi disconnected. Forecast not updated.");

    return false;
  }

  Serial.println();
  Serial.println("Requesting weather forecast...");

  HTTPClient http;

  http.begin(WEATHER_URL);

  int httpCode = http.GET();

  Serial.print("HTTP Response Code: ");
  Serial.println(httpCode);

  if (httpCode <= 0) {

    Serial.print("HTTP request failed: ");
    Serial.println(http.errorToString(httpCode));

    http.end();

    return false;
  }

  String payload = http.getString();

  Serial.print("Response size: ");
  Serial.print(payload.length());
  Serial.println(" bytes");

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

  JsonObject hourly = doc["hourly"];

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

  forecastFetchedAt = millis();
  lastForecastFetch = millis();
  forecastAvailable = true;
  forecastStaleDueToWiFi = false;

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

// CHECK FORECAST AGE
bool forecastIsFresh() {

  if (!forecastAvailable) {
    return false;
  }

  if (forecastStaleDueToWiFi) {
    return false;
  }

  unsigned long age =
    millis() - forecastFetchedAt;

  return age <= FORECAST_MAX_AGE;
}

// HANDLE WI-FI STATUS
void serviceWiFi() {

  bool wifiConnected =
    WiFi.status() == WL_CONNECTED;

  if (wifiWasConnected && !wifiConnected) {

    forecastStaleDueToWiFi = true;

    Serial.println();
    Serial.println("Wi-Fi disconnected.");
    Serial.println("Forecast marked STALE.");
    Serial.println("Using local sensors.");
  }

  if (!wifiWasConnected && wifiConnected) {

    Serial.println();
    Serial.println("Wi-Fi reconnected.");
    Serial.println("Waiting for scheduled forecast update...");
  }

  wifiWasConnected = wifiConnected;
}

// DISPLAY WEATHER STATUS
void displayWeatherStatus() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Rain:");
  lcd.print(rainProbability);
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("Age:");

  if (forecastAvailable) {

    unsigned long ageMinutes =
      (millis() - forecastFetchedAt) / 60000UL;

    lcd.print(ageMinutes);
    lcd.print("m ");

  }

  else {

    lcd.print("-- ");
  }

  if (forecastIsFresh()) {

    lcd.print("FRESH");

  }

  else {

    lcd.print("STALE");
  }
}

// CHECK WEATHER RAIN HOLD
bool weatherRainHold() {

  if (!forecastIsFresh()) {
    return false;
  }

  return rainProbability >= RAIN_HOLD_PCT;
}

// DISPLAY MENU
void showMenu() {

  lcd.clear();

  if (menuItem == 0) {

    lcd.setCursor(0, 0);
    lcd.print("1:Pump Runtime");

    lcd.setCursor(0, 1);
    lcd.print(cfg.runMinutes);
    lcd.print(" min");
  }

  else if (menuItem == 1) {

    lcd.setCursor(0, 0);
    lcd.print("2:Moisture Set");

    lcd.setCursor(0, 1);
    lcd.print(cfg.setpointPct);
    lcd.print("%");
  }

  else if (menuItem == 2) {

    lcd.setCursor(0, 0);
    lcd.print("3:Start Hour");

    lcd.setCursor(0, 1);
    lcd.print(cfg.startHour);
    lcd.print(":00");
  }
}

// START NUMERIC ENTRY
void startEntry() {

  enteringValue = true;
  inputValue = "";

  lcd.clear();

  if (menuItem == 0) {

    lcd.print("Pump Time:");
  }

  else if (menuItem == 1) {

    lcd.print("Setpoint:");
  }

  else if (menuItem == 2) {

    lcd.print("Start Hour:");
  }

  lcd.setCursor(0, 1);
}


// HANDLE ENTRYs 
void handleEntry(char key) {

  if (key >= '0' && key <= '9') {

    if (inputValue.length() < 2) {

      inputValue += key;

      lcd.setCursor(0, 1);
      lcd.print("                ");

      lcd.setCursor(0, 1);
      lcd.print(inputValue);
    }
  }


  else if (key == '#') {

    if (inputValue.length() == 0) {
      return;
    }

    int value = inputValue.toInt();

    if (applySetting(menuItem, value)) {

      lcd.clear();
      lcd.print("Value accepted!");

      delay(1000);

      enteringValue = false;
      inputValue = "";

      showMenu();
    }

    else {

      lcd.clear();
      lcd.print("INVALID VALUE");

      lcd.setCursor(0, 1);

      if (menuItem == 0) {

        lcd.print("Range: 1-30");
      }

      else if (menuItem == 1) {

        lcd.print("Range: 10-90");
      }

      else if (menuItem == 2) {

        lcd.print("Range: 0-23");
      }

      delay(2000);

      enteringValue = false;
      inputValue = "";

      showMenu();
    }
  }

  else if (key == '*') {

    enteringValue = false;
    inputValue = "";

    showMenu();
  }
}

// HANDLE MENU
void handleMenu(char key) {

  if (key == '1') {

    menuItem = 0;
    startEntry();
  }

  else if (key == '2') {

    menuItem = 1;
    startEntry();
  }

  else if (key == '3') {

    menuItem = 2;
    startEntry();
  }

  else if (key == '4') {

    menuItem = 0;
    showMenu();
  }

  else if (key == '5') {

    menuItem = 1;
    showMenu();
  }

  else if (key == '6') {

    menuItem = 2;
    showMenu();
  }

  else if (key == '#') {

    menuActive = false;

    lcd.clear();
  }
}

// START IRRIGATION
void startIrrigation() {

  if (irrigationActive) {
    return;
  }

  Serial.println();
  Serial.println("=== IRRIGATION START ===");

  Serial.print("Runtime setting: ");
  Serial.print(cfg.runMinutes);
  Serial.println(" minute(s)");

  // Show when the firmware safety limit overrides the keypad setting
  if (cfg.runMinutes > MAX_RUNTIME_MINUTES) {
    Serial.print("Firmware limit applied: ");
    Serial.print(MAX_RUNTIME_MINUTES);
    Serial.println(" minute(s)");
  }

  // Runtime from keypad setting
  int effectiveRuntime = cfg.runMinutes;

  // Limit runtime to the firmware maximum
  if (effectiveRuntime > MAX_RUNTIME_MINUTES) {
    effectiveRuntime = MAX_RUNTIME_MINUTES;
  }

  irrigationDuration =
    (unsigned long)effectiveRuntime * 60000UL;

  irrigationActive = true;

  lastDisplayedSecond = 999999;

  // Start by opening valve
  digitalWrite(VALVE_PIN, RELAY_ON);

  Serial.println("VALVE ON");
  Serial.println("Waiting for valve to settle...");

  phaseStartTime = millis();

  actuatorPhase = PH_OPENING;
}

// STOP IRRIGATION
void stopIrrigation() {

  if (!irrigationActive) {
    return;
  }

  Serial.println();
  Serial.println("=== IRRIGATION STOPPING ===");

  // Pump OFF first
  digitalWrite(PUMP_PIN, RELAY_OFF);

  Serial.println("PUMP OFF");

  phaseStartTime = millis();

  actuatorPhase = PH_CLOSING;
}

// SERVICE ACTUATORS
void serviceActuators() {

  unsigned long now = millis();

  // VALVE OPENING
  if (actuatorPhase == PH_OPENING) {

    if (now - phaseStartTime >= SETTLE_MS) {

      // Start pump after valve settles
      digitalWrite(PUMP_PIN, RELAY_ON);

      Serial.println("PUMP ON");
      Serial.println("Timed irrigation running...");

      irrigationStartTime = now;

      actuatorPhase = PH_RUNNING;
    }
  }

  // PUMP RUNNING

  else if (actuatorPhase == PH_RUNNING) {

    // Check runtime
    if (now - irrigationStartTime >= irrigationDuration) {

      Serial.println("Runtime reached zero.");

      stopIrrigation();
    }

    // Display countdown
    else {

      unsigned long elapsed =
        now - irrigationStartTime;

      unsigned long remaining =
        irrigationDuration - elapsed;

      unsigned long totalSeconds =
        remaining / 1000UL;

      // Only update LCD when the second changes
      if (totalSeconds != lastDisplayedSecond) {

        lastDisplayedSecond = totalSeconds;

        unsigned int minutes =
          totalSeconds / 60;

        unsigned int seconds =
          totalSeconds % 60;

        lcd.setCursor(0, 0);
        lcd.print("IRRIGATION      ");

        lcd.setCursor(0, 1);

        lcd.print("Time: ");
        if (minutes < 10) {
          lcd.print("0");
        }

        lcd.print(minutes);
        lcd.print(":");

        if (seconds < 10) {
          lcd.print("0");
        }

        lcd.print(seconds);
        lcd.print("        ");
      }
    }
  }

  // VALVE CLOSING
  else if (actuatorPhase == PH_CLOSING) {

    if (now - phaseStartTime >= SETTLE_MS) {

      // Close valve after pump has stopped
      digitalWrite(VALVE_PIN, RELAY_OFF);

      Serial.println("VALVE OFF");
      Serial.println("=== IRRIGATION COMPLETE ===");

      irrigationActive = false;

      actuatorPhase = PH_IDLE;

      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("Irrigation");
      lcd.setCursor(0, 1);
      lcd.print("Complete");

      delay(1000);
    }
  }
}

// SETUP
void setup() {

  Serial.begin(115200);

  while (!Serial) {
    delay(10);
  }

  // Wi-Fi
  connectWiFi();

  // Network time
  setupTime();

  // First forecast fetch
  if (WiFi.status() == WL_CONNECTED) {

    if (fetchForecast()) {

      Serial.println("Initial forecast received.");

    }

    else {

      Serial.println("Initial forecast failed.");
    }
  }

  lastForecastFetch = millis();
  //Relay
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(VALVE_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, RELAY_OFF);
  digitalWrite(VALVE_PIN, RELAY_OFF);

  // Part F outputs
  pinMode(IRRIGATE_LED, OUTPUT);
  pinMode(HOLD_LED, OUTPUT);
  pinMode(RAIN_LED, OUTPUT);
  pinMode(MANUAL_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(IRRIGATE_LED, LOW);
  digitalWrite(HOLD_LED, LOW);
  digitalWrite(RAIN_LED, LOW);
  digitalWrite(MANUAL_LED, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(RAIN_PIN, INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);
  pinMode(OVERRIDE_PIN, INPUT_PULLUP);

  attachInterrupt(
    digitalPinToInterrupt(OVERRIDE_PIN),
    onOverride,
    FALLING
  );

  dht.begin();

  lcd.init();
  lcd.backlight();
  // Load settings
  loadSettings();

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Smart Farm");

  lcd.setCursor(0, 1);
  lcd.print("System Ready");

  delay(1500);

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Runtime: ");

  lcd.print(cfg.runMinutes);

  lcd.print(" min");

  lcd.setCursor(0, 1);
  lcd.print("Press * Menu");
}

void loop() {

  // Wi-Fi status
  serviceWiFi();

  // Fetch forecast periodically
  if (
    millis() - lastForecastFetch >=
    FORECAST_FETCH_INTERVAL
  ) {

    if (WiFi.status() == WL_CONNECTED) {

      if (fetchForecast()) {

        Serial.println("Scheduled forecast update successful.");

      }

      else {

        Serial.println("Scheduled forecast update failed.");
      }

    }

    else {

      Serial.println("Wi-Fi disconnected. Forecast not updated.");
      forecastStaleDueToWiFi = true;
    }
  }

  // OVERRIDE FLAG
  if (overrideFlag) {

    overrideFlag = false;

    manualOverride = !manualOverride;

    Serial.print("OVERRIDE SWITCH PRESSED -> ");

    if (manualOverride) {

      Serial.println("MANUAL OVERRIDE ON");
    }

    else {

      Serial.println("MANUAL OVERRIDE OFF");
    }
  }

  // KEYPAD
  char key = keypad.getKey();

  if (key) {

    if (key == '*') {

      if (!menuActive) {

        menuActive = true;
        enteringValue = false;

        showMenu();

        return;
      }

      else if (!enteringValue) {

        menuActive = false;

        lcd.clear();

        lcd.setCursor(0, 0);
        lcd.print("Runtime: ");

        lcd.print(cfg.runMinutes);

        lcd.print(" min");
      }

      else {

        handleEntry(key);
      }
    }


    else if (menuActive) {

      if (enteringValue) {

        handleEntry(key);
      }

      else {

        handleMenu(key);
      }
    }
  }

  // MENU ACTIVE

  if (menuActive) {

    return;
  }

  // ACTUATOR CONTROL
  serviceActuators();

  if (irrigationActive) {

    return;
  }

  // READ FARM STATE

  FarmState currentState = decide();

  // UPDATE STATE LEDs
  updateStateLEDs(currentState);

  // UPDATE TANK BUZZER
  updateTankBuzzer();

  // START IRRIGATION WHEN STATE = IRRIGATE
  if (currentState == S_IRRIGATE) {

    Serial.println();
    Serial.println("Farm State: IRRIGATE");
    Serial.println("Deciding Input: Soil Moisture");

    startIrrigation();

    return;
  }

  // DISPLAY OTHER STATES
  displayState(currentState);

  // SERIAL INFORMATION
  float moisture = readMoisturePct();

  Serial.print("Moisture: ");
  Serial.print(moisture, 1);
  Serial.println("%");

  Serial.print("Tank: ");

  if (tankEmpty()) {

    Serial.println("EMPTY");
  }

  else {

    Serial.println("OK");
  }


  if (currentState == S_MANUAL) {

    Serial.println("Farm State: MANUAL_OVERRIDE");
    Serial.println("Deciding Input: Manual Override");
  }

  else if (currentState == S_RAIN) {

    Serial.println("Farm State: RAIN_DETECTED");
    Serial.println("Deciding Input: Rain Sensor");
  }

  else {

    Serial.println("Farm State: HOLD");
    Serial.println("Deciding Input: Moisture OK");
  }


  Serial.print("Pump Runtime Setting: ");
  Serial.print(cfg.runMinutes);
  Serial.println(" min");

  Serial.print("Hysteresis Margin: ");
  Serial.print(HYSTERESIS_MARGIN);
  Serial.println("%");

  Serial.print("Wi-Fi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");

  Serial.print("Forecast: ");
  Serial.println(forecastIsFresh() ? "FRESH" : "STALE");

  Serial.print("Rain Probability: ");
  Serial.print(rainProbability);
  Serial.println("%");

  Serial.println("-----------------------------");

  delay(2000);
}