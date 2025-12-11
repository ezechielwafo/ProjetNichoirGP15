/*
 * TEST BATTERIE TURNIGY 1S (3.7V) - M5Stack TimerCAM
 * VERSION AVEC SERVEUR WEB WiFi
 * 
 * Fonctionnalités:
 * - Lecture tension batterie réelle 1S Li-Po
 * - Photo automatique toutes les 10 secondes
 * - Serveur web pour visualiser données sur smartphone
 * - LED clignote selon niveau batterie
 * - Fonctionne sur batterie (sans USB)
 */

#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFi.h>
#include <WebServer.h>

// ===== CONFIGURATION WiFi =====
const char* ssid = "Pixel_4887";           // ⚠️ MODIFIEZ ICI
const char* password = ""; // ⚠️ MODIFIEZ ICI

WebServer server(80);

// ===== CONFIGURATION BATTERIE M5Stack TimerCAM =====
const int BATTERY_ADC_PIN = 38;
const int BAT_HOLD_PIN = 33;
const int LED_PIN = 2;

const float VOLTAGE_CALIBRATION = 2.0;  // Ajustez selon votre mesure
const float ADC_REFERENCE = 3.3;
const int ADC_RESOLUTION = 4095;

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

// ===== VARIABLES GLOBALES =====
int photoCounter = 0;
unsigned long lastPhotoTime = 0;
unsigned long startTime = 0;
const unsigned long PHOTO_INTERVAL = 10000;

float currentVoltage = 0;
int currentPercent = 0;

// ===== FONCTION: LECTURE TENSION BATTERIE =====
float readBatteryVoltage() {
    long adcSum = 0;
    for(int i = 0; i < 20; i++) {
        adcSum += analogRead(BATTERY_ADC_PIN);
        delay(5);
    }
    int adcValue = adcSum / 20;
    float voltage = (adcValue / (float)ADC_RESOLUTION) * ADC_REFERENCE * VOLTAGE_CALIBRATION;
    return voltage;
}

// ===== FONCTION: CALCUL POURCENTAGE BATTERIE =====
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

// ===== FONCTION: INDICATION LED NIVEAU BATTERIE =====
void indicateBatteryLevelLED() {
    if(currentPercent >= 60) {
        blinkLED(1, 500);
    } else if(currentPercent >= 30) {
        blinkLED(2, 250);
    } else if(currentPercent >= 10) {
        blinkLED(3, 150);
    } else {
        blinkLED(5, 100);
    }
}

// ===== PAGE WEB HTML =====
String getHTML() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>TimerCAM Battery Monitor</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); margin: 0; padding: 20px; color: white; }";
    html += ".container { max-width: 500px; margin: 0 auto; background: rgba(255,255,255,0.1); backdrop-filter: blur(10px); border-radius: 20px; padding: 30px; box-shadow: 0 8px 32px rgba(0,0,0,0.3); }";
    html += "h1 { text-align: center; margin-bottom: 30px; font-size: 28px; }";
    html += ".card { background: rgba(255,255,255,0.15); border-radius: 15px; padding: 20px; margin-bottom: 20px; }";
    html += ".label { font-size: 14px; opacity: 0.9; margin-bottom: 5px; }";
    html += ".value { font-size: 32px; font-weight: bold; margin: 10px 0; }";
    html += ".progress-bar { width: 100%; height: 30px; background: rgba(0,0,0,0.3); border-radius: 15px; overflow: hidden; margin-top: 10px; }";
    html += ".progress-fill { height: 100%; background: linear-gradient(90deg, #4CAF50, #8BC34A); transition: width 0.3s; display: flex; align-items: center; justify-content: center; font-weight: bold; }";
    html += ".warning { background: rgba(255,152,0,0.3); border-left: 4px solid #FF9800; padding: 15px; border-radius: 10px; margin-top: 20px; }";
    html += ".critical { background: rgba(244,67,54,0.3); border-left: 4px solid #F44336; }";
    html += ".info { display: flex; justify-content: space-between; margin: 10px 0; font-size: 16px; }";
    html += ".refresh { text-align: center; margin-top: 20px; opacity: 0.7; font-size: 12px; }";
    html += "@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }";
    html += ".pulse { animation: pulse 2s infinite; }";
    html += "</style>";
    html += "<script>";
    html += "setInterval(function(){ location.reload(); }, 5000);";
    html += "</script>";
    html += "</head><body>";
    
    html += "<div class='container'>";
    html += "<h1>🔋 TimerCAM Battery Monitor</h1>";
    
    // Card Batterie
    html += "<div class='card'>";
    html += "<div class='label'>TENSION BATTERIE</div>";
    html += "<div class='value'>" + String(currentVoltage, 2) + " V</div>";
    html += "<div class='progress-bar'>";
    
    String progressColor = "#4CAF50";
    if(currentPercent < 30) progressColor = "#FF9800";
    if(currentPercent < 10) progressColor = "#F44336";
    
    html += "<div class='progress-fill' style='width:" + String(currentPercent) + "%; background:" + progressColor + "'>";
    html += String(currentPercent) + "%";
    html += "</div></div></div>";
    
    // Card Photos
    html += "<div class='card'>";
    html += "<div class='info'><span>📸 Photos capturées</span><span><b>" + String(photoCounter) + "</b></span></div>";
    
    unsigned long uptimeSeconds = (millis() - startTime) / 1000;
    unsigned long hours = uptimeSeconds / 3600;
    unsigned long minutes = (uptimeSeconds % 3600) / 60;
    unsigned long seconds = uptimeSeconds % 60;
    
    html += "<div class='info'><span>⏰ Uptime</span><span><b>";
    if(hours > 0) html += String(hours) + "h ";
    if(minutes > 0) html += String(minutes) + "m ";
    html += String(seconds) + "s</b></span></div>";
    
    int adcRaw = analogRead(BATTERY_ADC_PIN);
    html += "<div class='info'><span>📊 ADC Raw</span><span><b>" + String(adcRaw) + "</b></span></div>";
    html += "</div>";
    
    // Avertissements
    if(currentPercent < 20 && currentPercent >= 10) {
        html += "<div class='warning'>⚠️ BATTERIE FAIBLE - Pensez à recharger bientôt</div>";
    }
    if(currentPercent < 10) {
        html += "<div class='warning critical pulse'>🚨 BATTERIE CRITIQUE - RECHARGEZ IMMÉDIATEMENT !</div>";
    }
    
    // Info refresh
    html += "<div class='refresh'>🔄 Actualisation automatique toutes les 5 secondes</div>";
    
    // Info WiFi
    html += "<div class='refresh'>📡 IP: " + WiFi.localIP().toString() + "</div>";
    
    html += "</div>";
    html += "</body></html>";
    
    return html;
}

// ===== GESTIONNAIRE PAGE WEB =====
void handleRoot() {
    server.send(200, "text/html", getHTML());
}

// ===== GESTIONNAIRE API JSON =====
void handleAPI() {
    String json = "{";
    json += "\"voltage\":" + String(currentVoltage, 2) + ",";
    json += "\"percent\":" + String(currentPercent) + ",";
    json += "\"photos\":" + String(photoCounter) + ",";
    json += "\"uptime\":" + String((millis() - startTime) / 1000) + ",";
    json += "\"adc\":" + String(analogRead(BATTERY_ADC_PIN));
    json += "}";
    server.send(200, "application/json", json);
}

// ===== CONFIGURATION CAMÉRA =====
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
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Erreur caméra: 0x%x\n", err);
        ESP.restart();
    }
    Serial.println("✅ Caméra initialisée");
}

// ===== FONCTION: CLIGNOTEMENT LED =====
void blinkLED(int times, int delayMs) {
    for(int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(delayMs);
        digitalWrite(LED_PIN, LOW);
        delay(delayMs);
    }
}

// ===== FONCTION: CAPTURE PHOTO =====
void capturePhoto() {
    Serial.println("\n📸 Capture photo...");
    digitalWrite(LED_PIN, HIGH);
    
    camera_fb_t * fb = esp_camera_fb_get();
    if(!fb) {
        Serial.println("❌ Échec capture");
        digitalWrite(LED_PIN, LOW);
        return;
    }

    photoCounter++;
    Serial.printf("✅ Photo #%d - %d octets\n", photoCounter, fb->len);
    
    esp_camera_fb_return(fb);
    digitalWrite(LED_PIN, LOW);
    delay(100);
    blinkLED(3, 150);
}

// ===== FONCTION: MISE À JOUR DONNÉES BATTERIE =====
void updateBatteryData() {
    currentVoltage = readBatteryVoltage();
    currentPercent = getBatteryPercentage(currentVoltage);
    
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║      🔋 STATUS BATTERIE           ║");
    Serial.println("╚════════════════════════════════════╝");
    Serial.printf("  Tension: %.2f V\n", currentVoltage);
    Serial.printf("  Pourcentage: %d%%\n", currentPercent);
    Serial.printf("  Photos: %d\n", photoCounter);
    Serial.printf("  URL: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n╔═══════════════════════════════════════╗");
    Serial.println("║  TimerCAM Battery + WiFi Server      ║");
    Serial.println("║  Batterie 1S 3.7V 2000mAh            ║");
    Serial.println("╚═══════════════════════════════════════╝\n");
    
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    pinMode(BAT_HOLD_PIN, OUTPUT);
    digitalWrite(BAT_HOLD_PIN, HIGH);
    
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetAttenuation(ADC_11db);
    
    // Configuration caméra
    config_camera();
    
    // Connexion WiFi
    Serial.println("📡 Connexion WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        blinkLED(1, 100);
        attempts++;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi connecté !");
        Serial.print("📱 URL: http://");
        Serial.println(WiFi.localIP());
        blinkLED(5, 200);
    } else {
        Serial.println("\n❌ WiFi échoué - Mode autonome");
    }
    
    // Configuration serveur web
    server.on("/", handleRoot);
    server.on("/api", handleAPI);
    server.begin();
    
    Serial.println("🌐 Serveur web démarré");
    Serial.println("🚀 Système prêt !\n");
    
    startTime = millis();
    updateBatteryData();
}

// ===== LOOP =====
void loop() {
    server.handleClient();
    
    unsigned long currentTime = millis();
    
    if (currentTime - lastPhotoTime >= PHOTO_INTERVAL) {
        updateBatteryData();
        indicateBatteryLevelLED();
        capturePhoto();
        lastPhotoTime = currentTime;
    }
    
    delay(10);
}
