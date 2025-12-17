// Test ESP32 + PIR avec LED externe sur breadboard
// LED connectée au GPIO 12

#define PIN_POWER_HOLD 33
#define PIN_PIR 13
#define PIN_LED 12 // LED externe sur breadboard

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_POWER_HOLD, OUTPUT);
  digitalWrite(PIN_POWER_HOLD, HIGH);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Serial.println("\n========================================");
  Serial.println("   TEST ESP32 + PIR + LED EXTERNE");
  Serial.println("========================================\n");
  
  Serial.println("📍 CÂBLAGE :");
  Serial.println("   ESP32 GPIO 12 → Résistance 220Ω → LED Anode (+, patte longue)");
  Serial.println("   LED Cathode (-, patte courte) → GND ESP32");
  Serial.println("   PIR OUT → ESP32 GPIO 13");
  Serial.println("   PIR VCC → ESP32 5V");
  Serial.println("   PIR GND → ESP32 GND\n");
  Serial.println("\n========================================");
  Serial.println("   ✅ SYSTÈME PRÊT !");
  Serial.println("========================================");
  Serial.println("\n👋 Bougez devant le capteur PIR!\n");
}

void loop() {
  int pirState = digitalRead(PIN_PIR);

  if (pirState == HIGH) {
    Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("🚨 MOUVEMENT DÉTECTÉ!");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    digitalWrite(PIN_LED, HIGH);
    Serial.println("💡 LED allumée");
    
    unsigned long startTime = millis();
    
    // Tant qu'il y a du mouvement
    while(digitalRead(PIN_PIR) == HIGH) {
      delay(100);
    }
    
    unsigned long duration = millis() - startTime;
    
    digitalWrite(PIN_LED, LOW);
    Serial.println("💡 LED éteinte");
    Serial.print("⏱  Durée: ");
    Serial.print(duration);
    Serial.println(" ms");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    delay(500);
  }

  delay(100);
}

/*
========================================
   📋 SCHÉMA DE CÂBLAGE COMPLET
========================================

BREADBOARD :
┌─────────────────────────────────┐
│                                 │
│  ESP32 GPIO 12 ──┬─────────┐   │
│                  │         │   │
│              [Résistance]  │   │
│               220Ω - 1kΩ   │   │
│                  │         │   │
│                  └────> LED (+) │  ← Patte LONGUE (Anode)
│                          │      │
│                         LED (-) │  ← Patte COURTE (Cathode)
│                          │      │
│                          └──────┼─→ GND ESP32
│                                 │
└─────────────────────────────────┘

PIR (module séparé) :
  PIR VCC → ESP32 5V (ou 3.3V selon modèle)
  PIR GND → ESP32 GND
  PIR OUT → ESP32 GPIO 13

========================================
   🎯 RÉSUMÉ DES CONNEXIONS
========================================

LED externe sur breadboard :
  ✓ GPIO 12 → Résistance → LED (+)
  ✓ LED (-) → GND

PIR :
  ✓ GPIO 13 → PIR OUT
  ✓ 5V → PIR VCC
  ✓ GND → PIR GND

Alimentation maintenue :
  ✓ GPIO 33 → Circuit de power hold
  
========================================
*/