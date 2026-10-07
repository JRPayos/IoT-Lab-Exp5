const int PUMP_PIN = 23;
const int VALVE_PIN = 19;

const unsigned long SETTLE_MS = 3000;

//States
enum PumpPhase {
  PH_IDLE,
  PH_OPENING,
  PH_RUNNING,
  PH_CLOSING
};

PumpPhase phase = PH_IDLE;
unsigned long tPhase = 0;

void setup() {

  Serial.begin(115200);

  pinMode(PUMP_PIN, OUTPUT);
  pinMode(VALVE_PIN, OUTPUT);

  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(VALVE_PIN, LOW);

  delay(2000);
}

void servicePump(bool wantRun) {

  unsigned long now = millis();

  switch (phase) {

    case PH_IDLE:
      if (wantRun) {
        Serial.println("Opening valve...");
        digitalWrite(VALVE_PIN, HIGH);

        tPhase = now;
        phase = PH_OPENING;
      }

      break;

    case PH_OPENING:
      if (now - tPhase >= SETTLE_MS) {
        Serial.println("Valve settled.");
        Serial.println("Starting pump...");

        digitalWrite(PUMP_PIN, HIGH);
        phase = PH_RUNNING;
      }

      break;

    case PH_RUNNING:
      if (!wantRun) {
        Serial.println("Stopping pump...");
        digitalWrite(PUMP_PIN, LOW);

        tPhase = now;
        phase = PH_CLOSING;
      }

      break;

    case PH_CLOSING:
      if (now - tPhase >= SETTLE_MS) {
        Serial.println("Closing valve...");
        digitalWrite(VALVE_PIN, LOW);

        phase = PH_IDLE;
        Serial.println("System idle.");
      }

      break;
  }
}

void loop() {
  static bool wantRun = false;
  static unsigned long lastChange = 0;

  unsigned long now = millis();

  if (now - lastChange >= 5000) {
    wantRun = !wantRun;
    lastChange = now;

    if (wantRun) {
      Serial.println();
      Serial.println("=== IRRIGATION REQUESTED ===");
    }
    else {
      Serial.println();
      Serial.println("=== IRRIGATION STOP REQUESTED ===");
    }
  }
  servicePump(wantRun);
}