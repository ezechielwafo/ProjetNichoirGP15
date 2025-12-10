#define PIR_PIN 33   // GPIO disponible sur TimerCAM

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  Serial.println("Test PIR TimerCAM : 1 = mouvement, 0 = aucun mouvement");
}

void loop() {
  int etat = digitalRead(PIR_PIN);

  if (etat == HIGH) {
    Serial.println(1);
  } else {
    Serial.println(0);
  }

  delay(200);
}
