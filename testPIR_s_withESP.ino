#define PIN_POWER_HOLD 33
#define PIN_PIR 13
#define PIN_LED_IR 14   // GPIO pour la LED IR

void setup() {
  Serial.begin(115200);

  pinMode(PIN_POWER_HOLD, OUTPUT);
  digitalWrite(PIN_POWER_HOLD, HIGH);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LED_IR, OUTPUT);

  Serial.println("Test ESP32 + PIR + LED IR");
}

void loop() {
  int pirState = digitalRead(PIN_PIR);

  if (pirState == HIGH) {
    Serial.println(">>> MOUVEMENT DÉTECTÉ ! <<<");
    digitalWrite(PIN_LED_IR, HIGH);   // Allume la LED IR
    while(digitalRead(PIN_PIR) == HIGH) {
      delay(100);
    }
    digitalWrite(PIN_LED_IR, LOW);    // Éteint la LED IR
    Serial.println("Fin du mouvement");
  }

  delay(60000);
}
