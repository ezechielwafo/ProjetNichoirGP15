#include <WiFi.h>
#include <WiFiManager.h>  
#include <PubSubClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "base64.h"
#include "esp_sleep.h"
 
// ===== CONFIGURATION MQTT =====
const char* mqtt_server = "192.168.0.3";
const int mqtt_port = 1883;
const char* mqtt_topic = "nichoir/photo";
 
// ===== PINS =====
const int PIR_PIN = 13;
const int LED_PIN = 2;
const int BATTERY_ADC_PIN = 38;
const int BAT_HOLD_PIN = 33;
 
// ===== CONFIGURATION BATTERIE =====
const float ADC_REFERENCE = 3.3;
const int ADC_RESOLUTION = 4095;
const float VOLTAGE_CALIBRATION = 1.38;
 
// ===== COMPTEURS PERSISTANTS (RTC Memory) =====
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int photoCounter = 0;
RTC_DATA_ATTR int pirDetectionCount = 0;
RTC_DATA_ATTR unsigned long lastPhotoTime = 0;  // Timestamp de la dernière photo
 
// ===== CONFIGURATION TEMPORISATION =====
const unsigned long PHOTO_COOLDOWN_MS = 300000;  // 5 minutes en millisecondes (5 * 60 * 1000)
 
// ===== PINS CAMÉRA M5Stack TimerCAM =====
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    15
#define XCLK_GPIO_NUM     27
#define SIOD_GPIO_NUM     25
#define SIOC_GPIO_NUM     23
#define Y9_GPIO_NUM       19
#define Y8_GPIO_NUM       36
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       39
#define Y5_GPIO_NUM        5
#define Y4_GPIO_NUM       34
#define Y3_GPIO_NUM       35
#define Y2_GPIO_NUM       32
#define VSYNC_GPIO_NUM    22
#define HREF_GPIO_NUM     26
#define PCLK_GPIO_NUM     21
 
// ===== CLIENTS =====
WiFiClient espClient;
PubSubClient mqttClient(espClient);
 
// ===== LECTURE BATTERIE =====
float readBatteryVoltage() {
    pinMode(BAT_HOLD_PIN, OUTPUT);
    digitalWrite(BAT_HOLD_PIN, HIGH);
    delay(100);
   
    analogSetAttenuation(ADC_11db);
   
    long adcSum = 0;
    for(int i = 0; i < 10; i++) {
        adcSum += analogRead(BATTERY_ADC_PIN);
        delay(5);
    }
    int adcValue = adcSum / 10;
   
    float voltage = (adcValue / (float)ADC_RESOLUTION) * ADC_REFERENCE * VOLTAGE_CALIBRATION;
    return voltage;
}
 
int getBatteryPercentage(float voltage) {
    if(voltage >= 4.2) return 100;
    if(voltage >= 4.1) return 95;
    if(voltage >= 4.0) return 90;
    if(voltage >= 3.9) return 80;
    if(voltage >= 3.8) return 70;
    if(voltage >= 3.7) return 60;
    if(voltage >= 3.6) return 50;
    if(voltage >= 3.5) return 40;
    if(voltage >= 3.4) return 30;
    if(voltage >= 3.3) return 20;
    if(voltage >= 3.2) return 10;
    if(voltage >= 3.0) return 5;
    return 0;
}
 
// ===== CONFIGURATION CAMÉRA =====
bool initCamera() {
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
        config.fb_count = 1;
    } else {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
    }
 
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Erreur caméra: 0x%x\n", err);
        return false;
    }
    Serial.println("✅ Caméra OK");
    return true;
}
 
// ===== CONNEXION WiFi AVEC WiFiManager =====
bool connectWiFi() {
    Serial.println("🔄 Tentative connexion WiFi...");
   
    WiFiManager wm;
   
    // Configuration du timeout (important pour le deep sleep)
    wm.setConfigPortalTimeout(180);  // 3 minutes max en mode portail
    wm.setConnectTimeout(20);        // 20 secondes pour se connecter
   
    // Désactiver le debug (optionnel)
    wm.setDebugOutput(false);
   
    // LED clignotante pendant la connexion
    digitalWrite(LED_PIN, HIGH);
   
    // Tentative de connexion auto, sinon démarre portail captif
    // Le SSID du portail sera "ESP32CAM-Config"
    bool connected = wm.autoConnect("ESP32CAM-Config", "nichoir2025");
   
    digitalWrite(LED_PIN, LOW);
   
    if(connected) {
        Serial.println("✅ WiFi connecté!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("SSID: ");
        Serial.println(WiFi.SSID());
        return true;
    }
   
    Serial.println("❌ WiFi échec (timeout portail)");
    return false;
}
 
// ===== CONNEXION MQTT =====
bool connectMQTT() {
    Serial.println("🔄 Connexion MQTT...");
   
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setBufferSize(20480);
    mqttClient.setKeepAlive(60);
    mqttClient.setSocketTimeout(15);
   
    String clientId = "ESP32CAM-" + String(random(0xffff), HEX);
   
    int attempts = 0;
    while (!mqttClient.connected() && attempts < 3) {
        if (mqttClient.connect(clientId.c_str())) {
            Serial.println("✅ MQTT connecté!");
            return true;
        }
        Serial.printf("❌ Tentative %d/3 échec, rc=%d\n", attempts+1, mqttClient.state());
        delay(1000);
        attempts++;
    }
   
    return false;
}
 
// ===== CAPTURE ET ENVOI PHOTO =====
bool captureAndSendPhoto() {
    digitalWrite(LED_PIN, HIGH);
   
    camera_fb_t * fb = esp_camera_fb_get();
    if(!fb) {
        Serial.println("❌ Échec capture");
        digitalWrite(LED_PIN, LOW);
        return false;
    }
 
    Serial.printf("📸 Capture: %d octets\n", fb->len);
 
    String encoded = base64::encode(fb->buf, fb->len);
   
    float voltage = readBatteryVoltage();
    int batteryPercent = getBatteryPercentage(voltage);
   
    String filename = "nichoir_PIR_" + String(photoCounter) + "_" + String(millis()) + ".jpg";
   
    String jsonMessage = "{";
    jsonMessage += "\"filename\":\"" + filename + "\",";
    jsonMessage += "\"timestamp\":\"" + String(millis()) + "\",";
    jsonMessage += "\"trigger_type\":\"PIR\",";
    jsonMessage += "\"pir_count\":" + String(pirDetectionCount) + ",";
    jsonMessage += "\"boot_count\":" + String(bootCount) + ",";
    jsonMessage += "\"battery_voltage\":" + String(voltage, 2) + ",";
    jsonMessage += "\"battery_percent\":" + String(batteryPercent) + ",";
    jsonMessage += "\"image\":\"" + encoded + "\"";
    jsonMessage += "}";
 
    Serial.printf("📦 JSON: %d octets | Batterie: %.2fV (%d%%)\n",
                  jsonMessage.length(), voltage, batteryPercent);
 
    bool success = mqttClient.publish(mqtt_topic, jsonMessage.c_str(), false);
   
    esp_camera_fb_return(fb);
   
    for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(100);
        digitalWrite(LED_PIN, HIGH);
        delay(100);
    }
    digitalWrite(LED_PIN, LOW);
 
    if(success) {
        Serial.println("✅ Photo envoyée!");
        photoCounter++;
        return true;
    } else {
        Serial.println("❌ Échec publication MQTT");
        return false;
    }
}
 
// ===== MODE DEEP SLEEP =====
void goToDeepSleep() {
    Serial.println("\n💤 Entrée en DEEP SLEEP...");
    Serial.println("   🔴 Réveil UNIQUEMENT sur détection PIR");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
   
    delay(100);
   
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 1);
    esp_deep_sleep_start();
}
 
// ===== SETUP =====
void setup() {
    bootCount++;
    pirDetectionCount++;
   
    Serial.begin(115200);
    delay(500);
   
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
   
    pinMode(PIR_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BAT_HOLD_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
   
    Serial.println("\n╔═══════════════════════════════════╗");
    Serial.println("║  🔴 RÉVEIL PIR DÉTECTION !        ║");
    Serial.println("╚═══════════════════════════════════╝");
    Serial.printf("Boot #%d | Photos: %d | Détections PIR: %d\n\n",
                  bootCount, photoCounter, pirDetectionCount);
   
    // ===== VÉRIFICATION COOLDOWN 5 MINUTES =====
    unsigned long currentTime = millis() + (bootCount * 10000);  // Approximation du temps réel
    unsigned long timeSinceLastPhoto = currentTime - lastPhotoTime;
   
    if(lastPhotoTime > 0 && timeSinceLastPhoto < PHOTO_COOLDOWN_MS) {
        unsigned long remainingTime = (PHOTO_COOLDOWN_MS - timeSinceLastPhoto) / 1000;
        Serial.println("⏳ COOLDOWN ACTIF !");
        Serial.printf("   Temps écoulé: %lu secondes\n", timeSinceLastPhoto / 1000);
        Serial.printf("   Temps restant: %lu secondes (~%lu minutes)\n",
                      remainingTime, remainingTime / 60);
        Serial.println("   ➡️ Retour en sleep sans prendre de photo\n");
        delay(2000);
        goToDeepSleep();
    }
   
    Serial.println("✅ Cooldown OK - Autorisation de photographier\n");
   
    if(!initCamera()) {
        Serial.println("❌ Échec init caméra - redémarrage");
        delay(2000);
        ESP.restart();
    }
   
    if(!connectWiFi()) {
        Serial.println("❌ Échec WiFi - passage en sleep");
        delay(2000);
        goToDeepSleep();
    }
   
    if(!connectMQTT()) {
        Serial.println("❌ Échec MQTT - passage en sleep");
        delay(2000);
        goToDeepSleep();
    }
   
    Serial.println("\n📸 Capture et envoi photo...");
    if(captureAndSendPhoto()) {
        Serial.println("✅ Mission accomplie!");
        lastPhotoTime = currentTime;  // Enregistrer le timestamp
    } else {
        Serial.println("⚠️ Échec envoi photo");
    }
   
    mqttClient.disconnect();
    WiFi.disconnect(true);
    delay(1000);
   
    goToDeepSleep();
}
 
// ===== LOOP =====
void loop() {
    delay(1000);
}