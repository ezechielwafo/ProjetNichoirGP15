/*
 * ESP32-CAM NICHOIR - VERSION FINALE TESTÉE
 * Basée sur le code qui fonctionnait + optimisations
 * 
 * ✅ MQTT Stable
 * ✅ PIR Détection
 * ✅ Batterie Monitoring
 * ✅ Photos Auto + Déclenchées
 */

#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "base64.h"

// ===== CONFIGURATION WiFi & MQTT =====
const char* STORED_SSID = "electroProjectWifi";
const char* STORED_PASSWORD = "B1MesureEnv";
const char* mqtt_server = "192.168.2.46";
const int mqtt_port = 1883;
const char* mqtt_topic = "nichoir/photo";
const char* mqtt_status_topic = "nichoir/status";

// ===== PINS =====
const int PIR_PIN = 13;              // Capteur de mouvement
const int LED_PIN = 2;               // LED rouge intégrée
const int BAT_HOLD_PIN = 33;         // Maintien alimentation batterie
const int BATTERY_ADC_PIN = 38;      // Lecture tension batterie

// ===== CONSTANTES =====
const float VOLTAGE_CALIBRATION = 2.2;   // À ajuster selon votre batterie
const float ADC_REFERENCE = 3.3;
const int ADC_RESOLUTION = 4095;
const unsigned long PHOTO_INTERVAL = 60000;        // Photo auto: 60 secondes
const unsigned long MIN_DELAY_PIR = 5000;          // Délai entre détections PIR: 5s
const unsigned long MQTT_RECONNECT_INTERVAL = 5000; // Reconnexion MQTT: 5s
const int MQTT_KEEPALIVE_SECONDS = 15;             // Keepalive MQTT (renommé)

// ===== PINS CAMÉRA M5Stack TimerCAM =====
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

// ===== VARIABLES GLOBALES =====
WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

int photoCounter = 0;
int pirDetectionCount = 0;
int lastPirState = LOW;
unsigned long lastPhotoTime = 0;
unsigned long lastPirDetectionTime = 0;
unsigned long lastMqttAttempt = 0;

bool wifiAuth = false;
bool systemReady = false;
bool pirCalibrated = false;

float currentVoltage = 0;
int currentPercent = 0;
bool isUsbPowered = false;

// ═════════════════════════════════════════════════════════
// FONCTIONS BATTERIE
// ═════════════════════════════════════════════════════════

float readBatteryVoltage() {
    long sum = 0;
    int validReadings = 0;
    
    for(int i = 0; i < 20; i++) {
        int reading = analogRead(BATTERY_ADC_PIN);
        if(reading > 0) {
            sum += reading;
            validReadings++;
        }
        delay(5);
    }
    
    if(validReadings == 0) return 0.0;
    return (sum / validReadings / (float)ADC_RESOLUTION) * ADC_REFERENCE * VOLTAGE_CALIBRATION;
}

int getBatteryPercentage(float v) {
    if(v >= 4.2) return 100;
    if(v >= 4.0) return 90;
    if(v >= 3.8) return 70;
    if(v >= 3.6) return 50;
    if(v >= 3.4) return 30;
    if(v >= 3.2) return 10;
    return v >= 3.0 ? 5 : 0;
}

void updateBattery() {
    int adcRaw = analogRead(BATTERY_ADC_PIN);
    isUsbPowered = (adcRaw < 50);
    currentVoltage = readBatteryVoltage();
    currentPercent = getBatteryPercentage(currentVoltage);
}

void displayBatteryInfo() {
    Serial.println("\n╔════════════════════════════════════╗");
    Serial.println("║      🔋 BATTERIE STATUS           ║");
    Serial.println("╚════════════════════════════════════╝");
    Serial.printf("  Source: %s\n", isUsbPowered ? "USB" : "Batterie");
    Serial.printf("  Tension: %.2f V\n", currentVoltage);
    Serial.printf("  Niveau: %d%%\n", currentPercent);
    Serial.printf("  ADC: %d\n", analogRead(BATTERY_ADC_PIN));
    
    if(!isUsbPowered && currentPercent < 20) {
        Serial.println("  ⚠️  BATTERIE FAIBLE !");
    }
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
}

// ═════════════════════════════════════════════════════════
// MQTT
// ═════════════════════════════════════════════════════════

bool reconnectMQTT() {
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi déconnecté");
        return false;
    }
    
    Serial.println("\n🔄 Connexion MQTT...");
    Serial.printf("   Broker: %s:%d\n", mqtt_server, mqtt_port);
    
    // Test TCP
    WiFiClient testClient;
    Serial.print("   Test TCP... ");
    if (!testClient.connect(mqtt_server, mqtt_port, 3000)) {
        Serial.println("❌ ÉCHEC");
        return false;
    }
    Serial.println("✅ OK");
    testClient.stop();
    
    // Connexion MQTT
    String clientId = "ESP32CAM_" + String(ESP.getEfuseMac(), HEX);
    Serial.print("   Connexion MQTT... ");
    
    if (mqttClient.connect(clientId.c_str())) {
        Serial.println("✅ CONNECTÉ !");
        
        String statusMsg = "{\"status\":\"online\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
        mqttClient.publish(mqtt_status_topic, statusMsg.c_str(), true);
        
        return true;
    } else {
        Serial.printf("❌ ÉCHEC (code %d)\n", mqttClient.state());
        return false;
    }
}

bool sendPhotoMQTT(bool isPir) {
    if(!mqttClient.connected()) {
        Serial.println("⚠ MQTT déconnecté, reconnexion...");
        if(!reconnectMQTT()) return false;
    }
    
    updateBattery();
    digitalWrite(LED_PIN, HIGH);
    
    Serial.println("\n📸 Capture photo...");
    camera_fb_t * fb = esp_camera_fb_get();
    if(!fb) { 
        digitalWrite(LED_PIN, LOW); 
        Serial.println("❌ Échec capture");
        return false; 
    }
    
    Serial.printf("   Taille: %d octets\n", fb->len);
    
    // Vérification taille avec réduction progressive
    int quality = 20;
    while(fb->len > 12000 && quality < 50) {  // Limite à 12KB
        Serial.printf("⚠ Image trop grande (%d octets), qualité %d...\n", fb->len, quality);
        esp_camera_fb_return(fb);
        
        sensor_t * s = esp_camera_sensor_get();
        quality += 5;  // Augmenter compression
        s->set_quality(s, quality);
        
        fb = esp_camera_fb_get();
        if(!fb) {
            Serial.println("❌ Échec capture réduite");
            digitalWrite(LED_PIN, LOW);
            return false;
        }
    }
    
    if(fb->len > 12000) {
        Serial.printf("❌ Image trop grande même compressée (%d octets)\n", fb->len);
        Serial.println("   Solution: Baissez FRAMESIZE à QVGA dans config_camera()");
        esp_camera_fb_return(fb);
        digitalWrite(LED_PIN, LOW);
        return false;
    }
    
    // Encodage Base64
    Serial.println("   Encodage Base64...");
    String encoded = base64::encode(fb->buf, fb->len);
    
    String type = isPir ? "PIR" : "AUTO";
    
    // JSON
    String json = "{";
    json += "\"filename\":\"nichoir_" + type + "_" + String(photoCounter) + ".jpg\",";
    json += "\"trigger\":\"" + type + "\",";
    json += "\"timestamp\":" + String(millis()) + ",";
    json += "\"pir_count\":" + String(pirDetectionCount) + ",";
    json += "\"battery_voltage\":" + String(currentVoltage, 2) + ",";
    json += "\"battery_percent\":" + String(currentPercent) + ",";
    json += "\"usb_powered\":" + String(isUsbPowered ? "true" : "false") + ",";
    json += "\"image\":\"" + encoded + "\"}";
    
    Serial.printf("   JSON: %d octets\n", json.length());
    
    // Publication
    Serial.print("   Publication... ");
    bool ok = mqttClient.publish(mqtt_topic, json.c_str(), false);
    
    esp_camera_fb_return(fb);
    
    // LED feedback
    if(ok) {
        Serial.println("✅ OK");
        for(int i=0; i<3; i++) { 
            digitalWrite(LED_PIN, LOW); 
            delay(100); 
            digitalWrite(LED_PIN, HIGH); 
            delay(100); 
        }
    } else {
        Serial.println("❌ ÉCHEC");
        for(int i=0; i<5; i++) { 
            digitalWrite(LED_PIN, LOW); 
            delay(50); 
            digitalWrite(LED_PIN, HIGH); 
            delay(50); 
        }
    }
    
    digitalWrite(LED_PIN, LOW);
    
    Serial.printf("📊 Photo %s #%d | %s\n", type.c_str(), photoCounter, ok?"✅":"❌");
    return ok;
}

// ═════════════════════════════════════════════════════════
// PIR
// ═════════════════════════════════════════════════════════

void handlePIR() {
    int state = digitalRead(PIR_PIN);
    unsigned long now = millis();
    
    if(state == HIGH && lastPirState == LOW && now - lastPirDetectionTime > MIN_DELAY_PIR) {
        pirDetectionCount++;
        
        Serial.println("\n╔════════════════════════════════╗");
        Serial.printf("║  🔴 MOUVEMENT #%d détecté      ║\n", pirDetectionCount);
        Serial.println("╚════════════════════════════════╝");
        
        if(sendPhotoMQTT(true)) {
            photoCounter++;
        }
        lastPirDetectionTime = now;
    }
    lastPirState = state;
}

// ═════════════════════════════════════════════════════════
// CAMÉRA
// ═════════════════════════════════════════════════════════

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
    
    // ✅ SOLUTION: Résolution plus petite pour MQTT
    config.frame_size = FRAMESIZE_VGA;     // 640x480 (au lieu de SVGA 800x600)
    config.jpeg_quality = 20;              // Qualité réduite (10=meilleur, 63=pire)
    config.fb_count = psramFound() ? 2 : 1;
    
    if(esp_camera_init(&config) != ESP_OK) {
        Serial.println("❌ Caméra échec");
        ESP.restart();
    }
    
    // Réglages supplémentaires pour réduire la taille
    sensor_t * s = esp_camera_sensor_get();
    if(s != NULL) {
        s->set_brightness(s, 0);     // -2 à 2
        s->set_contrast(s, 0);       // -2 à 2
        s->set_saturation(s, 0);     // -2 à 2
        s->set_quality(s, 20);       // 10-63, plus haut = plus compressé
    }
    
    Serial.println("✅ Caméra OK (VGA 640x480, qualité optimisée)");
}

// ═════════════════════════════════════════════════════════
// WEB INTERFACE
// ═════════════════════════════════════════════════════════

void handleStatus() {
    updateBattery();
    
    String mqttStatus = mqttClient.connected() ? "✅ Connecté" : "❌ Déconnecté";
    String mqttColor = mqttClient.connected() ? "#28a745" : "#dc3545";
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'>";
    html += "<style>body{font-family:Arial;background:#f5f5f5;padding:20px;margin:0;}";
    html += ".container{max-width:600px;margin:0 auto;background:white;border-radius:15px;padding:30px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}";
    html += ".mqtt-box{padding:15px;border-radius:8px;margin:10px 0;text-align:center;font-weight:bold;color:white;background:" + mqttColor + ";}";
    html += ".battery{background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:20px;border-radius:10px;margin:20px 0;}";
    html += ".stat{background:#f8f9fa;padding:15px;border-radius:8px;margin:10px 0;display:flex;justify-content:space-between;}";
    html += ".progress{width:100%;height:25px;background:rgba(255,255,255,0.2);border-radius:12px;overflow:hidden;margin-top:10px;}";
    html += ".fill{height:100%;background:white;display:flex;align-items:center;justify-content:center;font-weight:bold;color:#667eea;}</style></head><body>";
    html += "<div class='container'><h1 style='text-align:center;margin-bottom:20px;'>📊 Nichoir ESP32-CAM</h1>";
    
    html += "<div class='mqtt-box'>🔗 MQTT: " + mqttStatus + "</div>";
    
    html += "<div class='battery'><h2 style='margin:0 0 10px 0;'>🔋 ";
    html += isUsbPowered ? "USB" : "Batterie";
    html += "</h2><div style='font-size:32px;font-weight:bold;'>" + String(currentVoltage, 2) + "V</div>";
    html += "<div class='progress'><div class='fill' style='width:" + String(currentPercent) + "%'>" + String(currentPercent) + "%</div></div></div>";
    
    html += "<div class='stat'><span>📡 WiFi IP</span><b>" + WiFi.localIP().toString() + "</b></div>";
    html += "<div class='stat'><span>🎯 Broker</span><b>" + String(mqtt_server) + "</b></div>";
    html += "<div class='stat'><span>📸 Photos</span><b>" + String(photoCounter) + "</b></div>";
    html += "<div class='stat'><span>🎯 PIR</span><b>" + String(pirDetectionCount) + "</b></div>";
    html += "<div class='stat'><span>📡 PIR État</span><b>" + String(digitalRead(PIR_PIN) ? "🔴" : "🟢") + "</b></div>";
    html += "<div class='stat'><span>⏰ Uptime</span><b>" + String(millis()/1000) + "s</b></div>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
}

void handleLogin() {
    if(server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String pass = server.arg("password");
        
        if(ssid == STORED_SSID && pass == STORED_PASSWORD) {
            WiFi.begin(STORED_SSID, STORED_PASSWORD);
            int attempts = 0;
            while(WiFi.status() != WL_CONNECTED && attempts++ < 30) { 
                delay(500); 
                Serial.print("."); 
            }
            
            if(WiFi.status() == WL_CONNECTED) {
                wifiAuth = true;
                Serial.println("\n✅ WiFi connecté: " + WiFi.localIP().toString());
                
                mqttClient.setServer(mqtt_server, mqtt_port);
                mqttClient.setBufferSize(20000);
                mqttClient.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
                mqttClient.setSocketTimeout(15);
                
                delay(1000);
                reconnectMQTT();
                updateBattery();
                displayBatteryInfo();
                
                Serial.println("\n⏳ Calibration PIR (30s)...");
                for(int j=30; j>0; j--) { 
                    Serial.printf("%ds ", j); 
                    delay(1000); 
                }
                pirCalibrated = true;
                Serial.println("\n✅ PIR calibré !\n");
                
                server.send(200, "text/html", 
                    "<html><head><meta charset='UTF-8'></head><body style='text-align:center;padding:50px;font-family:Arial;'>"
                    "<h1 style='color:#38ef7d;'>✅ Connecté !</h1>"
                    "<p>IP: " + WiFi.localIP().toString() + "</p>"
                    "<script>setTimeout(()=>location.href='/status',2000)</script></body></html>");
                
                systemReady = true;
            } else {
                server.send(200, "text/html", "<html><body style='text-align:center;padding:50px;'><h1>❌ WiFi échec</h1><a href='/'>Retour</a></body></html>");
            }
        } else {
            server.send(200, "text/html", "<html><body style='text-align:center;padding:50px;'><h1>❌ Identifiants incorrects</h1><a href='/'>Retour</a></body></html>");
        }
    }
}

void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>*{margin:0;padding:0;box-sizing:border-box;}body{font-family:Arial;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;display:flex;justify-content:center;align-items:center;padding:20px;}";
    html += ".container{background:white;padding:40px;border-radius:20px;max-width:400px;width:100%;box-shadow:0 20px 60px rgba(0,0,0,0.3);}";
    html += ".logo{text-align:center;font-size:64px;margin-bottom:20px;}h1{text-align:center;margin-bottom:30px;}";
    html += ".form-group{margin-bottom:20px;}label{display:block;margin-bottom:8px;font-weight:600;}";
    html += "input{width:100%;padding:12px;border:2px solid #e0e0e0;border-radius:8px;font-size:16px;}";
    html += "button{width:100%;padding:14px;background:linear-gradient(135deg,#667eea,#764ba2);color:white;border:none;border-radius:8px;font-size:18px;font-weight:bold;cursor:pointer;}";
    html += ".info{background:#e3f2fd;padding:15px;border-radius:8px;margin-top:20px;font-size:13px;text-align:center;}</style></head><body>";
    html += "<div class='container'><div class='logo'>🏡</div><h1>Nichoir Connecté</h1>";
    html += "<form action='/login' method='POST'><div class='form-group'><label>🌐 SSID</label>";
    html += "<input type='text' name='ssid' value='Orange-f6a09' required></div>";
    html += "<div class='form-group'><label>🔒 Password</label><input type='password' name='password' required></div>";
    html += "<button type='submit'>🚀 Connecter</button></form>";
    html += "<div class='info'>📡 MQTT: " + String(mqtt_server) + "<br>📸 PIR GPIO 13</div></div></body></html>";
    server.send(200, "text/html", html);
}

// ═════════════════════════════════════════════════════════
// SETUP
// ═════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n╔═══════════════════════════════╗");
    Serial.println("║  NICHOIR CONNECTÉ v2.0       ║");
    Serial.println("║  ESP32-CAM + PIR + MQTT      ║");
    Serial.println("╚═══════════════════════════════╝\n");
    
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    // Configuration GPIO
    pinMode(BAT_HOLD_PIN, OUTPUT);
    digitalWrite(BAT_HOLD_PIN, HIGH);
    delay(100);
    
    pinMode(PIR_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetAttenuation(ADC_11db);
    analogSetWidth(12);
    
    Serial.println("🔧 Configuration:");
    Serial.printf("   BAT_HOLD (GPIO %d) ✅\n", BAT_HOLD_PIN);
    Serial.printf("   LED (GPIO %d) ✅\n", LED_PIN);
    Serial.printf("   PIR (GPIO %d) ✅\n", PIR_PIN);
    Serial.printf("   ADC (GPIO %d) ✅\n\n", BATTERY_ADC_PIN);
    
    config_camera();
    
    WiFi.softAP("ESP32-CAM-Login", "12345678");
    Serial.println("📡 Point d'accès:");
    Serial.println("   SSID: ESP32-CAM-Login");
    Serial.println("   Pass: 12345678");
    Serial.println("   URL:  http://192.168.4.1\n");
    
    server.on("/", handleRoot);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/status", handleStatus);
    server.begin();
    
    updateBattery();
    displayBatteryInfo();
    
    Serial.println("✅ Système prêt !\n");
}

// ═════════════════════════════════════════════════════════
// LOOP
// ═════════════════════════════════════════════════════════

void loop() {
    server.handleClient();
    
    if(systemReady && wifiAuth && WiFi.status() == WL_CONNECTED) {
        // Reconnexion MQTT automatique
        if(!mqttClient.connected()) {
            if(millis() - lastMqttAttempt > MQTT_RECONNECT_INTERVAL) {
                reconnectMQTT();
                lastMqttAttempt = millis();
            }
        } else {
            mqttClient.loop();
        }
        
        // Détection PIR
        if(pirCalibrated) {
            handlePIR();
        }
        
        // Photo automatique toutes les 10 secondes
        if(millis() - lastPhotoTime >= PHOTO_INTERVAL) {
            Serial.println("\n⏰ Photo automatique");
            if(sendPhotoMQTT(false)) {
                photoCounter++;
            }
            displayBatteryInfo();
            lastPhotoTime = millis();
        }
    }
    
    delay(50);
}

/*
 * ═══════════════════════════════════════════════════
 * 📝 RÉSUMÉ FONCTIONNALITÉS
 * ═══════════════════════════════════════════════════
 * 
 * ✅ Photos automatiques toutes les 10 secondes
 * ✅ Photos déclenchées par PIR (mouvement)
 * ✅ Monitoring batterie en temps réel
 * ✅ LED clignote à chaque photo
 * ✅ Interface web avec status complet
 * ✅ Reconnexion MQTT automatique
 * ✅ Détection USB vs Batterie
 * ✅ JSON complet envoyé via MQTT
 * 
 * MQTT JSON:
 * {
 *   "filename": "nichoir_PIR_5.jpg",
 *   "trigger": "PIR" ou "AUTO",
 *   "timestamp": 123456,
 *   "pir_count": 5,
 *   "battery_voltage": 4.10,
 *   "battery_percent": 95,
 *   "usb_powered": false,
 *   "image": "base64..."
 * }
 */