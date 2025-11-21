#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h" 
#include <PubSubClient.h>   
#include <ArduinoJson.h>    

// --- 1. CONFIGURATION DU RÉSEAU ET MQTT ---
// ⚠️ À REMPLACER PAR VOS IDENTIFIANTS WI-FI
const char *ssid = "electroProjectWifi";
const char *password = "B1MesureEnv";

// ⚠️ À REMPLACER PAR L'ADRESSE IP RÉELLE DE VOTRE RASPBERRY PI
const char* MQTT_SERVER = "192.168.2.29"; 
const int MQTT_PORT = 1883;
const char* BATTERY_TOPIC = "nichoir/status/battery";

// Pin pour la lecture de la batterie (GPIO 38 est courant pour le TimerCam)
#define BATTERY_ADC_PIN 38 

// Durée du délai entre chaque envoi MQTT (10 secondes)
const unsigned long MQTT_INTERVAL_MS = 10000;

// --- 2. DÉFINITIONS DES BROCHES (CONFIGURATION M5STACK TimerCam OV3660) ---
// Cette configuration corrige l'erreur 0x106
#define PWDN_GPIO_NUM -1 
#define RESET_GPIO_NUM 15 // G15 pour Reset
#define XCLK_GPIO_NUM 27  // G27 pour XCLK
#define SIOD_GPIO_NUM 25  // G25 pour SDA (Data)
#define SIOC_GPIO_NUM 23  // G23 pour SCL (Clock)
#define Y9_GPIO_NUM 35    // Data
#define Y8_GPIO_NUM 34    // Data
#define Y7_GPIO_NUM 39    // Data
#define Y6_GPIO_NUM 36    // Data
#define Y5_GPIO_NUM 21    // Data
#define Y4_GPIO_NUM 19    // Data
#define Y3_GPIO_NUM 18    // Data
#define Y2_GPIO_NUM 5     // Data
#define VSYNC_GPIO_NUM 22 // VSYNC
#define HREF_GPIO_NUM 26  // HREF
#define PCLK_GPIO_NUM 23  // PCLK
#define LED_GPIO_NUM 33 

// --- 3. INSTANCIATION DES CLIENTS ET DÉCLARATIONS ---
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0; 

// Déclarations de fonctions complètes (définies plus bas)
void startCameraServer(); 
void config_camera();
esp_err_t capture_handler(httpd_req_t *req);


// --- 4. FONCTIONS DE GESTION DU SYSTÈME ET DE LA BATTERIE (MQTT) ---

float read_battery_level() {
    // ... (Code read_battery_level) ...
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
    float batteryLevel = read_battery_level();
    
    char timestamp[20];
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 
            2025, 11, 20, (millis() / 3600000) % 24, (millis() / 60000) % 60, (millis() / 1000) % 60);

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


// --- 5. GESTIONNAIRES HTTP (Capture) ---

// Gestionnaire HTTP pour la CAPTURE D'IMAGE (/capture)
esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    
    #if defined(LED_GPIO_NUM)
    digitalWrite(LED_GPIO_NUM, HIGH); 
    #endif

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Echec de la capture de l'image");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

    size_t fb_len = fb->len;
    res = httpd_resp_send(req, (const char *)fb->buf, fb_len);

    esp_camera_fb_return(fb);

    #if defined(LED_GPIO_NUM)
    digitalWrite(LED_GPIO_NUM, LOW); 
    #endif

    return res;
}

// Déclaration Globale de la Route /capture 
httpd_uri_t uri_capture = {
    .uri = "/capture", 
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
};

// Démarrage du serveur web et enregistrement de la route
void startCameraServer()
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_capture);
    }
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
    config.frame_size = FRAMESIZE_SVGA; 
    config.jpeg_quality = 10; 
    config.fb_count = 1;

    // --- Configuration PSRAM ---
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
    // --- FIN Configuration PSRAM ---

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Echec de l'initialisation de la camera: 0x%x\n", err);
        return; 
    }

    sensor_t *s = esp_camera_sensor_get();
    // Ajustements des capteurs OV3660
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1); 
        s->set_brightness(s, 1); 
        s->set_saturation(s, -2);
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

    startCameraServer(); 

    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("/capture' to view the image.");
}

void loop() {
    // 1. Maintien de la connexion MQTT
    if (!client.connected()) {
        reconnect();
    }
    client.loop(); 

    // 2. Publication périodique du statut (toutes les 10 secondes)
    long now = millis();
    if (now - lastMsg >= MQTT_INTERVAL_MS) { 
        lastMsg = now;
        publishBatteryStatus(); 
    }
}