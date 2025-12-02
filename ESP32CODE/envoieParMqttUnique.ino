#include <WiFi.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "Arduino.h"
#include "base64.h"  // Librairie pour encoder Base64

// ===== WIFI =====
const char* WIFI_SSID = "electroProjectWifi";
const char* WIFI_PASSWORD = "B1MesureEnv";

// ===== MQTT =====
const char* MQTT_SERVER = "192.168.2.46"; // IP du Raspberry Pi
const int MQTT_PORT = 1883;
const char* MQTT_USER = "mqtt_user";       // si auth
const char* MQTT_PASS = "mqtt_password";   // si auth
const char* MQTT_TOPIC_PHOTO = "nichoir/photo/b64";
const char* MQTT_TOPIC_INFO  = "nichoir/info";

// ===== CAMÉRA PINS (M5Stack TimerCAM) =====
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   15
#define XCLK_GPIO_NUM    27
#define SIOD_GPIO_NUM    25
#define SIOC_GPIO_NUM    23
#define Y9_GPIO_NUM      19
#define Y8_GPIO_NUM      36
#define Y7_GPIO_NUM      18
#define Y6_GPIO_NUM      39
#define Y5_GPIO_NUM      5
#define Y4_GPIO_NUM      34
#define Y3_GPIO_NUM      35
#define Y2_GPIO_NUM      32
#define VSYNC_GPIO_NUM   22
#define HREF_GPIO_NUM    26
#define PCLK_GPIO_NUM    21

// ===== VARIABLES =====
WiFiClient espClient;
PubSubClient mqttClient(espClient);
int photoCounter = 0;
unsigned long lastPhotoTime = 0;
const unsigned long PHOTO_INTERVAL = 60000; // 1 min

// ===== CONFIG CAMÉRA =====
void config_camera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if(psramFound()){
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
    }

    if(esp_camera_init(&config) != ESP_OK){
        Serial.println("❌ Erreur caméra");
        ESP.restart();
    }
    Serial.println("✅ Caméra initialisée");
}

// ===== WIFI =====
void connectWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("📡 Connexion WiFi");
    int attempts = 0;
    while(WiFi.status() != WL_CONNECTED && attempts < 30){
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if(WiFi.status() == WL_CONNECTED){
        Serial.println("\n✅ WiFi connecté !");
        Serial.print("IP: "); Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n❌ Échec WiFi");
        ESP.restart();
    }
}

// ===== MQTT =====
void connectMQTT() {
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    while(!mqttClient.connected()){
        Serial.print("🔌 Connexion MQTT...");
        if(mqttClient.connect("ESP32CAM", MQTT_USER, MQTT_PASS)){
            Serial.println("✅ Connecté !");
        } else {
            Serial.print("❌ Échec, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" -> Nouvelle tentative dans 5s");
            delay(5000);
        }
    }
}

// ===== CAPTURE ET ENVOI =====
void captureAndSendPhoto() {
    digitalWrite(2, HIGH);
    camera_fb_t * fb = esp_camera_fb_get();
    if(!fb){
        Serial.println("❌ Échec capture");
        digitalWrite(2, LOW);
        return;
    }

    // Encodage Base64
    String photoBase64 = base64::encode(fb->buf, fb->len);

    // Publier la photo
    if(mqttClient.connected()){
        mqttClient.publish(MQTT_TOPIC_PHOTO, photoBase64.c_str());
        Serial.printf("📸 Photo #%d envoyée (%d octets)\n", photoCounter, fb->len);
    } else {
        Serial.println("❌ MQTT non connecté");
    }

    // Publier les métadonnées
    String metadata = "{";
    metadata += "\"filename\":\"nichoir_" + String(photoCounter) + ".jpg\",";
    metadata += "\"battery_level\":" + String(85) + ",";
    metadata += "\"photo_counter\":" + String(photoCounter) + ",";
    metadata += "\"detection_source\":\"AUTO\",";
    metadata += "\"uptime_seconds\":" + String(millis() / 1000);
    metadata += "}";
    mqttClient.publish(MQTT_TOPIC_INFO, metadata.c_str());
    Serial.println("ℹ️ Métadonnées envoyées");

    esp_camera_fb_return(fb);
    digitalWrite(2, LOW);
    photoCounter++;
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);

    config_camera();
    connectWiFi();
    connectMQTT();
}

// ===== LOOP =====
void loop() {
    if(!mqttClient.connected()) connectMQTT();
    mqttClient.loop();

    unsigned long currentTime = millis();
    if(currentTime - lastPhotoTime >= PHOTO_INTERVAL){
        captureAndSendPhoto();
        lastPhotoTime = currentTime;
    }

    delay(100);
}
