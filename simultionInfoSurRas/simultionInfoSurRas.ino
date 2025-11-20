#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- 1. CONFIGURATION DU RÉSEAU ET DU BROKER ---
// ⚠️ À REMPLACER PAR VOS INFORMATIONS WI-FI
const char* ssid = "electroProjectWifi";
const char* password = "B1MesureEnv";

// ⚠️ À REMPLACER PAR L'ADRESSE IP DE VOTRE RASPBERRY PI
const char* mqtt_server = "192.168.2.48"; 
const int mqtt_port = 1883;

// Topic pour l'envoi du statut de la batterie (doit correspondre au script Python)
const char* BATTERY_TOPIC = "nichoir/status/battery";

// --- 2. INSTANCIATION DES CLIENTS ---
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;

// --- 3. FONCTIONS D'ACTION ---

// Fonction pour se connecter au Wi-Fi
void setup_wifi() {
  Serial.print("Connexion a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connecte.");
  Serial.print("Adresse IP: ");
  Serial.println(WiFi.localIP());
}

// Fonction pour se connecter au Broker MQTT
void reconnect() {
  // Boucle jusqu'a ce que nous soyons reconnectes
  while (!client.connected()) {
    Serial.print("Tentative de connexion MQTT...");
    // Creer un client ID
    String clientId = "ESP32_TimerCam_";
    clientId += String(random(0xffff), HEX);
    
    // Tenter la connexion
    if (client.connect(clientId.c_str())) {
      Serial.println("Connecte!");
      // Pas d'abonnement car l'ESP32 est uniquement un publicateur ici
    } else {
      Serial.print("echec, rc=");
      Serial.print(client.state());
      Serial.println(" Nouvel essai dans 5 secondes");
      delay(5000);
    }
  }
}

// Fonction pour simuler la lecture de batterie et publier le message JSON
void publishBatteryStatus() {
  // SIMULATION DE LA LECTURE DE LA BATTERIE (en temps reel, vous liriez le pin ADC)
  // Valeur simulee entre 70.0 et 99.9%
  float batteryLevel = 70.0 + (float)(rand() % 300) / 10.0;
  
  // Formatage du Timestamp
  char timestamp[20];
  sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 
          2025, 11, 18, (millis() / 3600000) % 24, (millis() / 60000) % 60, (millis() / 1000) % 60);

  // Creation du document JSON
  DynamicJsonDocument doc(256);
  doc["timestamp"] = timestamp;
  doc["battery_level"] = batteryLevel;
  
  // Serialisation du JSON en une chaine
  String jsonString;
  serializeJson(doc, jsonString);

  // Publication sur le Topic MQTT
  Serial.print("Publication sur ");
  Serial.print(BATTERY_TOPIC);
  Serial.print(": ");
  Serial.println(jsonString);
  
  client.publish(BATTERY_TOPIC, jsonString.c_str());
}

// --- 4. SETUP ET LOOP ARDUINO ---

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0)); // Initialisation pour la simulation
  setup_wifi();
  
  // Configuration du client MQTT pour se connecter au Pi
  client.setServer(mqtt_server, mqtt_port);
  // Pas de client.setCallback car l'ESP32 ne recoit pas de message pour ce test.
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Traitement des connexions reseau

  long now = millis();
  // Envoi d'un message toutes les 10 secondes pour le test
  if (now - lastMsg > 10000) { 
    lastMsg = now;
    publishBatteryStatus();
  }
}
