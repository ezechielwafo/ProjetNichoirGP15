#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h> 
#include <PubSubClient.h> 
#include <ArduinoJson.h>  
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_sleep.h" 
#include <string.h> 

// ===== CONFIGURATION WI-FI & SERVEUR =====
const char* ssid = "electroProjectWifi";
const char* password = "B1MesureEnv";

// ⚠️ ADRESSE IP CIBLE DU RASPBERRY PI
const char* MQTT_SERVER = "192.168.2.46";
const int MQTT_PORT = 1883; 
const char* SERVER_URL_BASE = "http://192.168.2.46:5000/upload"; // URL Flask

// Topics et Broches Clés
const char* BATTERY_TOPIC = "nichoir/status/battery";
#define BUTTON_WAKEUP_PIN 39    // Pour le bouton (Généralement Button A sur M5Stack)
#define BATTERY_ADC_PIN 38      // Pin de lecture de la batterie

// ===== PINS CAMÉRA (M5Stack TimerCAM - Configuration stable) =====
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM 15
#define XCLK_GPIO_NUM 27
#define SIOD_GPIO_NUM 25
#define SIOC_GPIO_NUM 23
#define Y9_GPIO_NUM 19
#define Y8_GPIO_NUM 36
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 39
#define Y5_GPIO_NUM 5
#define Y4_GPIO_NUM 34
#define Y3_GPIO_NUM 35
#define Y2_GPIO_NUM 32
#define VSYNC_GPIO_NUM 22
#define HREF_GPIO_NUM 26
#define PCLK_GPIO_NUM 21
#define LED_GPIO_NUM 2 // LED Flash/Torche (GPIO 2)

// ===== VARIABLES GLOBALES ET CLIENTS =====
WiFiClient espClient;
PubSubClient client(espClient);

// Déclarations des fonctions
void setup_deep_sleep();
void reconnect();
bool captureAndSendPhoto();
int getBatteryLevel(); 
void publishBatteryStatus(int level);
void config_camera(); // Définition complète


// --- FONCTIONS DE GESTION DU SYSTÈME (MQTT/BATTERIE) ---

int getBatteryLevel() {
    // Fonction simple de lecture de batterie (à adapter si nécessaire)
    pinMode(BATTERY_ADC_PIN, INPUT); 
    int raw_adc = analogRead(BATTERY_ADC_PIN);
    
    // Simuler le niveau (0-100) pour le test
    int level = map(raw_adc, 0, 4095, 0, 100); 
    if (level > 100) level = 100;
    
    return level;
}

void publishBatteryStatus(int level) {
    reconnect(); // S'assure que MQTT est connecté

    char timestamp[20];
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 2025, 11, 27, (millis() / 3600000) % 24, (millis() / 60000) % 60, (millis() / 1000) % 60);

    DynamicJsonDocument doc(256);
    doc["timestamp"] = timestamp;
    doc["battery_level"] = level;
    doc["local_ip"] = WiFi.localIP().toString(); 
    doc["file_name"] = "N/A"; 

    String jsonString;
    serializeJson(doc, jsonString);

    client.publish(BATTERY_TOPIC, jsonString.c_str());
}

void reconnect() {
    while (!client.connected()) {
        String clientId = "TimerCam-";
        clientId += String(random(0xffff), HEX);
        client.connect(clientId.c_str());
    }
}

// --- FONCTION DE CAPTURE ET ENVOI (CORRIGÉE POUR MULTIPART) ---
bool captureAndSendPhoto() {
    if(WiFi.status() != WL_CONNECTED) return false;

    // Allumer la LED pendant la capture
    digitalWrite(LED_GPIO_NUM, HIGH);
    
    // 1. CAPTURE
    camera_fb_t * fb = esp_camera_fb_get();
    if(!fb) { 
        Serial.println("❌ Échec capture caméra"); 
        digitalWrite(LED_GPIO_NUM, LOW); 
        return false; 
    }

    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    
    // Génération du nom de fichier
    char timestamp_s[20];
    sprintf(timestamp_s, "%04d%02d%02d_%02d%02d%02d", 2025, 11, 27, (millis() / 3600000) % 24, (millis() / 60000) % 60, (millis() / 1000) % 60);
    String filename = "capture_" + String(timestamp_s) + ".jpg";

    // --- CONSTRUCTION DU CORPS MULTIPART ---
    String header = "--" + boundary + "\r\n";
    header += "Content-Disposition: form-data; name=\"image\"; filename=\"" + filename + "\"\r\n";
    header += "Content-Type: image/jpeg\r\n\r\n";
    String trailer = "\r\n--" + boundary + "--\r\n";
    
    size_t headerSize = header.length();
    size_t imageSize = fb->len;
    size_t trailerSize = trailer.length();
    size_t totalPayloadSize = headerSize + imageSize + trailerSize;

    // Allouer un buffer pour l'ensemble du payload (Méthode stable)
    uint8_t *payloadBuffer = (uint8_t*) malloc(totalPayloadSize); 
    if (payloadBuffer == NULL) {
        Serial.println("ERREUR RAM: Echec d'allocation du buffer.");
        esp_camera_fb_return(fb); return false;
    }

    // Copie des morceaux dans le buffer
    size_t offset = 0;
    memcpy(payloadBuffer + offset, header.c_str(), headerSize);
    offset += headerSize;
    memcpy(payloadBuffer + offset, fb->buf, imageSize);
    offset += imageSize;
    memcpy(payloadBuffer + offset, trailer.c_str(), trailerSize);

    // Envoi du POST
    HTTPClient http;
    http.begin(SERVER_URL_BASE);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http.addHeader("X-Filename", filename); // Envoi du nom de fichier par header HTTP (pour le débogage)
    http.addHeader("X-Battery-Level", String(getBatteryLevel())); // Envoi de la batterie par header HTTP
    
    int httpResponseCode = http.POST(payloadBuffer, totalPayloadSize); 
    
    // Libération et Nettoyage
    free(payloadBuffer);
    esp_camera_fb_return(fb); 
    digitalWrite(LED_GPIO_NUM, LOW);

    bool success = false;
    if (httpResponseCode == 200) {
        Serial.printf("✅ HTTP OK. Code: %d\n", httpResponseCode);
        
        // PUBLICATION MQTT DES METADONNÉES APRES LA RÉUSSITE DU POST
        // Utilisation du même JSON pour l'enregistrement BDD sur le Pi
        DynamicJsonDocument doc(512);
        doc["timestamp"] = timestamp_s;
        doc["file_name"] = filename;
        doc["file_path"] = "/home/eze/images/" + filename; 
        doc["battery_level"] = getBatteryLevel();
        doc["local_ip"] = WiFi.localIP().toString();

        String jsonString;
        serializeJson(doc, jsonString);
        client.publish(BATTERY_TOPIC, jsonString.c_str()); // Envoi du JSON complet

        success = true;
    } else {
        Serial.printf("❌ Erreur HTTP: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    return success;
}


// --- 6. CONFIGURATION DE LA CAMÉRA (Initialisation complète) ---

void config_camera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    
    // Assignation des broches M5Stack corrigées
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
    
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG; 
    config.frame_size = FRAMESIZE_SVGA; 
    config.jpeg_quality = 10; 
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;

    if (psramFound()) {
      config.fb_location = CAMERA_FB_IN_PSRAM;
      config.jpeg_quality = 10;
      config.fb_count = 2;
    } 

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Echec de l'initialisation de la camera au SETUP: 0x%x\n", err);
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL && s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1); s->set_brightness(s, 1); s->set_saturation(s, -2);
    }
}


// --- 7. FONCTION DE SOMMEIL ET RÉVEIL ---

void setup_deep_sleep() {
    Serial.println("Systeme en Deep Sleep. Attente du Bouton (GPIO39)...");
    
    esp_camera_deinit(); 
    
    // Configurer la broche GPIO39 comme source de réveil sur LOW (0)
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(BUTTON_WAKEUP_PIN), 0);
    
    esp_deep_sleep_start();
}


// --- 8. SETUP (Logique de Déclenchement) ---

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();
    
    // Désactiver le brownout detector
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Déterminer la cause du réveil
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("--- REVEIL PAR BOUTON DETECTÉ ---");
        
        // 1. Initialiser le système pour la tâche
        config_camera(); 
        pinMode(BATTERY_ADC_PIN, INPUT); 
        pinMode(LED_GPIO_NUM, OUTPUT);
        digitalWrite(LED_GPIO_NUM, LOW); 

        // 2. Connexion Wi-Fi rapide
        WiFi.begin(ssid, password);
        int timeout = 0;
        while (WiFi.status() != WL_CONNECTED && timeout < 20) { // 10 secondes max
             delay(500); 
             timeout++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi OK.");
            client.setServer(MQTT_SERVER, MQTT_PORT);
            reconnect(); 
            
            captureAndSendPhoto(); // Capture, envoi HTTP et publication MQTT

        } else {
            Serial.println("❌ Echec Wi-Fi. Pas d'envoi.");
        }

        // 3. Retour en veille (critique)
        setup_deep_sleep();
        
    } else {
        // Premier démarrage (mise en place initiale)
        Serial.println("--- DÉMARRAGE/RESET INITIAL ---");
        
        // Préparer les broches LED/Bouton
        pinMode(LED_GPIO_NUM, OUTPUT);
        digitalWrite(LED_GPIO_NUM, LOW); 
        pinMode(BUTTON_WAKEUP_PIN, INPUT); // INPUT simple corrigé
        
        // Aller directement en veille 
        setup_deep_sleep();
    }
}

void loop() {
    // Le code n'atteint jamais cette boucle.
    delay(10); 
}