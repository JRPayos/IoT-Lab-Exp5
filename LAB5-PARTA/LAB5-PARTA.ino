// Pump Relay Test
const int PUMP_PIN = 25;

void setup() {
  Serial.begin(115200);
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, HIGH);
  delay(2000);
}

void loop() {

  Serial.println("PUMP ON");
  digitalWrite(PUMP_PIN, LOW);

  delay(5000);

  Serial.println("PUMP OFF");
  digitalWrite(PUMP_PIN, HIGH);

  delay(5000);  
}