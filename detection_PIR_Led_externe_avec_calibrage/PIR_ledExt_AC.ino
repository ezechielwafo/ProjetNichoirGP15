// Test ESP32 + PIR avec LED externe sur breadboard
// LED connectée au GPIO 12

#define PIN_POWER_HOLD 33
#define PIN_PIR 13
#define PIN_LED 12   // LED externe sur breadboard

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

  // TEST 1: LED fonctionne ?
  Serial.println("TEST 1: Vérification LED sur GPIO 12");
  Serial.println("  La LED devrait clignoter 5 fois...\n");
  for(int i = 0; i < 5; i++) {
    Serial.print("  💡 Clignotement ");
    Serial.println(i + 1);
    digitalWrite(PIN_LED, HIGH);
    delay(500);
    digitalWrite(PIN_LED, LOW);
    delay(500);
  }
  
  Serial.println("\n  ✓ Si la LED a clignoté, le câblage est OK!\n");
  delay(1000);

  // TEST 2: Calibration PIR
  Serial.println("TEST 2: Calibration du PIR");
  Serial.println("  ⏱  NE BOUGEZ PAS pendant 30 secondes...\n");
  
  for(int i = 30; i > 0; i--) {
    if(i % 5 == 0 || i <= 3) {
      Serial.print("  ");
      Serial.print(i);
      Serial.println(" secondes...");
    }
    delay(1000);
  }
  
  Serial.println("\n========================================");
  Serial.println("   ✅ SYSTÈME PRÊT !");
  Serial.println("========================================");
  Serial.println("\n👋 Bougez devant le capteur PIR!\n");
}

void loop() {
  int pirState = digitalRead(PIN_PIR);
  
  // Affichage d'état toutes les 5 secondes pour debug
  static unsigned long lastCheck = 0;
  if(millis() - lastCheck > 5000) {
    Serial.print("État PIR: ");
    Serial.println(pirState == HIGH ? "HIGH (détection)" : "LOW (rien)");
    lastCheck = millis();
  }

  // Détection uniquement sur front montant (passage de LOW à HIGH)
  static int lastPirState = LOW;
  
  if (pirState == HIGH && lastPirState == LOW) {
    // Nouveau mouvement détecté !
    Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("🚨 MOUVEMENT DÉTECTÉ!");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    digitalWrite(PIN_LED, HIGH);
    Serial.println("💡 LED allumée");
    
    unsigned long startTime = millis();
    
    // Attendre la fin du mouvement
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
    
    delay(1000);  // Anti-rebond de 1 seconde
  }
  
  lastPirState = pirState;
  delay(50);
}

/*
========================================
   📋 RÉGLAGES DU MODULE PIR
========================================

Votre module PIR a 2 potentiomètres à ajuster :

1. **Sx (SENSIBILITÉ)** :
   • Tourner à GAUCHE = moins sensible (3-5 mètres)
   • Tourner à DROITE = plus sensible (7 mètres)
   • Recommandation: Position MILIEU pour test

2. **Tx (TEMPS/DURÉE)** :
   • Tourner à GAUCHE = courte durée (~3 secondes)
   • Tourner à DROITE = longue durée (~300 secondes)
   • Recommandation: Position GAUCHE (minimum) pour test

3. **Cavalier (JUMPER)** :
   • Position H = Répétable (signal reste HIGH si mouvement continu)
   • Position L = Non-répétable (pulse unique par détection)
   • Recommandation: Position H

========================================
   🔧 SI LE PIR DÉTECTE TOUT LE TEMPS
========================================

Problème: Le PIR reste en HIGH constant

Solutions :
1. ⏱  ATTENDRE la calibration complète (60 secondes SANS BOUGER)
2. 🔄 RÉDUIRE la sensibilité (Sx) en tournant vers la gauche
3. ⏰ RÉDUIRE le temps (Tx) en tournant à fond vers la gauche
4. 🌡️  ÉLOIGNER le PIR des sources de chaleur (radiateur, soleil)
5. 💡 ÉLOIGNER le PIR des lumières vives/néons
6. 🏠 STABILISER l'environnement (pas de courants d'air)
7. 🔌 VÉRIFIER l'alimentation (5V stable, pas de chutes de tension)
8. ⚡ AJOUTER un condensateur 100µF entre VCC et GND du PIR

Si le PIR continue à détecter en permanence :
• Laissez-le 2-3 minutes sans bouger pour calibration complète
• Le PIR peut être défectueux ou trop sensible pour l'environnement

========================================
   📊 COMMENT TESTER CORRECTEMENT
========================================

1. Téléversez le code
2. NE BOUGEZ PAS pendant 60 secondes (calibration)
3. Attendez le message "CALIBRATION TERMINÉE"
4. Passez votre main devant le PIR (30-50 cm)
5. La LED doit s'allumer UNE FOIS par passage
6. Attendez 2-3 secondes entre chaque test

========================================
*/
