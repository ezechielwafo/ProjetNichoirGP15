#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h" 
#include <PubSubClient.h>   
#include <ArduinoJson.h>    
#include <HTTPClient.h>     // Pour l'envoi HTTP POST

// --- 1. CONFIGURATION DU RÉSEAU ET SERVEUR ---
const char *ssid = "electroProjectWifi";
const char *password = "B1MesureEnv";

// ⚠️ ADRESSE IP DU RASPBERRY PI (Mosquitto/Flask)
const char* MQTT_SERVER = "192.168.2.46"; 
const int MQTT_PORT = 1883;
const char* HTTP_SERVER_IP = "192.168.2.46"; 
const int HTTP_SERVER_PORT = 5000;
const char* UPLOAD_PATH = "/upload";

// Topics et Périodicité
const char* BATTERY_TOPIC = "nichoir/status/battery";
const unsigned long MQTT_INTERVAL_MS = 10000; 
const unsigned long CAPTURE_INTERVAL_MS = 5000; 

// Pin pour la lecture de la batterie
#define BATTERY_ADC_PIN 38 

// --- 2. DÉFINITIONS DES BROCHES M5STACK (CORRIGÉES) ---
#define PWDN_GPIO_NUM -1 
#define RESET_GPIO_NUM 15 
#define XCLK_GPIO_NUM 27  
#define SIOD_GPIO_NUM 25  
#define SIOC_GPIO_NUM 23  
#define Y9_GPIO_NUM 35    
#define Y8_GPIO_NUM 34    
#define Y7_GPIO_NUM 39    
#define Y6_GPIO_NUM 36    
#define Y5_GPIO_NUM 21    
#define Y4_GPIO_NUM 19    
#define Y3_GPIO_NUM 18    
#define Y2_GPIO_NUM 5     
#define VSYNC_GPIO_NUM 22 
#define HREF_GPIO_NUM 26  
#define PCLK_GPIO_NUM 23  
#define LED_GPIO_NUM 33 

// --- 3. INSTANCIATION DES CLIENTS ET DÉCLARATIONS ---
WiFiClient espClient;
PubSubClient client(espClient);
long lastCaptureTime = 0; 
long lastMsg = 0; 

// Déclarations des fonctions
void startCameraServer(); 
void config_camera();
float read_battery_level();
void publishBatteryStatus();
void reconnect();
void sendImageToServer();


// --- 4. FONCTIONS DE GESTION DU SYSTÈME ET DE LA BATTERIE (MQTT) ---
// (Ces fonctions sont conservées et fonctionnent)

float read_battery_level() {
    pinMode(BATTERY_ADC_PIN, INPUT); 
    int raw_adc = analogRead(BATTERY_ADC_PIN);
    
    float voltage = (float)raw_adc / 4095.0 * 3.3 * 2.0; 
    float min_voltage = 3.3; 
    float max_voltage = 4.2; 
    float level = ((voltage - min_voltage) / (max_voltage - min_voltage)) * 100.0;
    
    if (level > 100.0) level = 100.0;
    if (level < 0.0) level = 0.0;
    
    return level;
}

void publishBatteryStatus() {
    // ... (Logique MQTT de publication du statut) ...
    float batteryLevel = read_battery_level();
    
    char timestamp[20];
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 
            2025, 11, 27, (millis() / 3600000) % 24, (millis() / 60000) % 60, (millis() / 1000) % 60);

    DynamicJsonDocument doc(256);
    doc["timestamp"] = timestamp;
    doc["battery_level"] = batteryLevel;
    doc["local_ip"] = WiFi.localIP().toString(); 
    
    String jsonString;
    serializeJson(doc, jsonString);

    if (client.publish(BATTERY_TOPIC, jsonString.c_str())) {
        Serial.print("MQTT OK: ");
        Serial.println(jsonString);
    } else {
        Serial.println("MQTT Echec de publication.");
    }
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("Tentative de connexion MQTT...");
        String clientId = "TimerCam-";
        clientId += String(random(0xffff), HEX);
        
        if (client.connect(clientId.c_str())) {
            Serial.println("Connecte!");
            publishBatteryStatus(); 
        } else {
            Serial.print("echec, rc=");
            Serial.print(client.state());
            Serial.println(" Nouvel essai dans 5 secondes");
            delay(5000);
        }
    }
}


// --- 5. FONCTION D'ENVOI D'IMAGE HTTP POST (CORRIGÉE POUR VIEILLES LIBRAIRIES) ---

void sendImageToServer() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Echec de la capture pour l'envoi.");
        return;
    }

    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    
    char timestamp_s[20];
    sprintf(timestamp_s, "%04d%02d%02d_%02d%02d%02d", 
            2025, 11, 27, (millis() / 3600000) % 24, (millis() / 60000) % 60, (millis() / 1000) % 60);

    String fileName = "capture_";
    fileName += String(timestamp_s);
    fileName += ".jpg";

    String serverPath = "http://" + String(HTTP_SERVER_IP) + ":" + String(HTTP_SERVER_PORT) + String(UPLOAD_PATH); 
    
    HTTPClient http;

    // --- CONSTRUCTION DU CORPS COMPLET (Méthode de la chaîne d'octets) ---
    // Cette méthode est plus gourmande en RAM mais contourne les problèmes de sendRequest séquentiel
    
    // 1. Définition du corps de la requête (header + image + trailer)
    String header = "--" + boundary + "\r\n";
    header += "Content-Disposition: form-data; name=\"image\"; filename=\"" + fileName + "\"\r\n";
    header += "Content-Type: image/jpeg\r\n\r\n";
    String trailer = "\r\n--" + boundary + "--\r\n";
    
    // 2. Calculer la taille totale
    size_t headerSize = header.length();
    size_t imageSize = fb->len;
    size_t trailerSize = trailer.length();
    size_t totalPayloadSize = headerSize + imageSize + trailerSize;

    // 3. Allouer un buffer pour l'ensemble du payload (ATTENTION RAM !)
    // Note: Utiliser ps_malloc pour la PSRAM si disponible, sinon DRAM
    uint8_t *payloadBuffer = (uint8_t*) malloc(totalPayloadSize); 
    if (payloadBuffer == NULL) {
        Serial.println("ERREUR RAM: Echec d'allocation du buffer pour l'image.");
        esp_camera_fb_return(fb);
        return;
    }

    // 4. Copier les morceaux dans le buffer
    size_t offset = 0;
    memcpy(payloadBuffer + offset, header.c_str(), headerSize);
    offset += headerSize;
    memcpy(payloadBuffer + offset, fb->buf, imageSize);
    offset += imageSize;
    memcpy(payloadBuffer + offset, trailer.c_str(), trailerSize);

    // 5. Envoyer en une seule fois avec la méthode POST simple
    http.begin(serverPath);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    
    int httpResponseCode = http.POST(payloadBuffer, totalPayloadSize); 

    // 6. Nettoyage
    free(payloadBuffer);
    esp_camera_fb_return(fb); 

    // 7. Logique MQTT
    if (httpResponseCode == 200) {
        Serial.printf("✅ HTTP OK. Image envoyee. Code: %d\n", httpResponseCode);
        
        // --- MQTT METADONNÉES ---
        DynamicJsonDocument doc(512);
        doc["timestamp"] = timestamp_s;
        doc["file_name"] = fileName;
        doc["file_path"] = "/home/eze/images/" + fileName; 
        doc["battery_level"] = read_battery_level();
        doc["local_ip"] = WiFi.localIP().toString();

        String jsonString;
        serializeJson(doc, jsonString);
        
        if (client.publish(BATTERY_TOPIC, jsonString.c_str())) {
            Serial.println("MQTT Metadonnees OK.");
        }
    } else {
        Serial.printf("❌ ECHEC HTTP. Code: %d, Message: %s\n", httpResponseCode, http.getString().c_str());
    }
    
    http.end();
}


// --- 6. CONFIGURATION DE LA CAMÉRA (M5Stack OV3660) ---

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
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 40;
    config.fb_count = 1;

    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Echec de l'initialisation de la camera: 0x%x\n", err);
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1); s->set_brightness(s, 1); s->set_saturation(s, -2);
        s->set_framesize(s, FRAMESIZE_QVGA);
    }
}


// --- 7. SETUP et LOOP ---

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();
    
    pinMode(BATTERY_ADC_PIN, INPUT); 
    #if defined(LED_GPIO_NUM)
    pinMode(LED_GPIO_NUM, OUTPUT);
    digitalWrite(LED_GPIO_NUM, LOW); 
    #endif

    config_camera();

    client.setServer(MQTT_SERVER, MQTT_PORT);
    
    WiFi.begin(ssid, password);
    WiFi.setSleep(false);

    Serial.print("WiFi connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");
    Serial.print("Adresse IP Locale: ");
    Serial.println(WiFi.localIP());
    Serial.println("Pret a envoyer.");
}

void loop() {
    // 1. Maintien de la connexion MQTT
    if (!client.connected()) {
        reconnect();
    }
    client.loop(); 

    long now = millis();
    
    // 2. Publication périodique du statut (toutes les 10 secondes)
    if (now - lastMsg >= MQTT_INTERVAL_MS) { 
        lastMsg = now;
        publishBatteryStatus(); 
    }

    // 3. Envoi périodique de l'image (toutes les 5 secondes)
    if (now - lastCaptureTime >= CAPTURE_INTERVAL_MS) {
        lastCaptureTime = now;
        sendImageToServer(); 
    }
    
    delay(10);
}