/*
 * ESP32-CAM NICHOIR - PIR + MQTT + Batterie
 * VERSION CORRIGÉE GPIO 33 (BAT_HOLD_PIN)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "base64.h"

// ===== CONFIGURATION =====
const char* STORED_SSID = "electroProjectWifi";
const char* STORED_PASSWORD = "B1MesureEnv";
const char* mqtt_server = "192.168.2.30";
const int mqtt_port = 1883;
const char* mqtt_topic = "nichoir/photo";

// ===== PINS - CORRECTION CRITIQUE =====
const int PIR_PIN = 13;
const int LED_PIN = 2;              // ✅ LED rouge réelle (pas GPIO 33 !)
const int BAT_HOLD_PIN = 33;        // ✅ CRITIQUE: Maintien alimentation batterie
const int BATTERY_ADC_PIN = 38;

// Constantes batterie
const float VOLTAGE_CALIBRATION = 2.2;  // Ajustez selon multimètre
const float ADC_REFERENCE = 3.3;
const int ADC_RESOLUTION = 4095;
const unsigned long PHOTO_INTERVAL = 60000;
const unsigned long MIN_DELAY_PIR = 5000;

// Pins caméra
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

// Variables
WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
int photoCounter = 0, pirDetectionCount = 0, lastPirState = LOW;
unsigned long lastPhotoTime = 0, lastPirDetectionTime = 0;
bool wifiAuth = false, systemReady = false, pirCalibrated = false;
float currentVoltage = 0;
int currentPercent = 0;
bool isUsbPowered = false;

// ===== FONCTIONS BATTERIE =====
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
    isUsbPowered = (adcRaw < 50);  // Détection USB vs Batterie
    currentVoltage = readBatteryVoltage();
    currentPercent = getBatteryPercentage(currentVoltage);
    
    Serial.printf("🔋 %s | %.2fV (%d%%) | ADC:%d\n", 
                  isUsbPowered ? "USB" : "BAT", 
                  currentVoltage, currentPercent, adcRaw);
}

// ===== MQTT =====
bool reconnectMQTT() {
    String clientId = "ESP32CAM-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
        Serial.println("✅ MQTT OK");
        return true;
    }
    Serial.printf("❌ MQTT échec: %d\n", mqttClient.state());
    return false;
}

bool sendPhotoMQTT(bool isPir) {
    if(!mqttClient.connected() && !reconnectMQTT()) return false;
    
    updateBattery();
    digitalWrite(LED_PIN, HIGH);
    
    camera_fb_t * fb = esp_camera_fb_get();
    if(!fb) { 
        digitalWrite(LED_PIN, LOW); 
        Serial.println("❌ Capture échec");
        return false; 
    }
    
    String encoded = base64::encode(fb->buf, fb->len);
    String type = isPir ? "PIR" : "AUTO";
    
    String json = "{";
    json += "\"filename\":\"nichoir_" + type + "_" + String(photoCounter) + ".jpg\",";
    json += "\"trigger\":\"" + type + "\",";
    json += "\"pir_count\":" + String(pirDetectionCount) + ",";
    json += "\"battery_voltage\":" + String(currentVoltage, 2) + ",";
    json += "\"battery_percent\":" + String(currentPercent) + ",";
    json += "\"usb_powered\":" + String(isUsbPowered ? "true" : "false") + ",";
    json += "\"image\":\"" + encoded + "\"}";
    
    bool ok = mqttClient.publish(mqtt_topic, json.c_str(), false);
    esp_camera_fb_return(fb);
    
    // LED clignote selon état
    for(int i=0; i<3; i++) { 
        digitalWrite(LED_PIN, LOW); 
        delay(100); 
        digitalWrite(LED_PIN, HIGH); 
        delay(100); 
    }
    digitalWrite(LED_PIN, LOW);
    
    Serial.printf("📸 %s #%d | %s\n", type.c_str(), photoCounter, ok?"✅":"❌");
    return ok;
}

// ===== PIR =====
void handlePIR() {
    int state = digitalRead(PIR_PIN);
    unsigned long now = millis();
    
    if(state == HIGH && lastPirState == LOW && now - lastPirDetectionTime > MIN_DELAY_PIR) {
        pirDetectionCount++;
        Serial.printf("\n🔴 PIR #%d détecté\n", pirDetectionCount);
        if(sendPhotoMQTT(true)) photoCounter++;
        lastPirDetectionTime = now;
    }
    lastPirState = state;
}

// ===== CAMÉRA =====
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
    config.frame_size = psramFound() ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count = psramFound() ? 2 : 1;
    
    if(esp_camera_init(&config) != ESP_OK) {
        Serial.println("❌ Caméra échec");
        ESP.restart();
    }
    Serial.println("✅ Caméra OK");
}

// ===== WEB =====
void handleStatus() {
    updateBattery();
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'>";
    html += "<style>body{font-family:Arial;background:#f5f5f5;padding:20px;margin:0;}.container{max-width:600px;margin:0 auto;background:white;border-radius:15px;padding:30px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}";
    html += ".battery{background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:20px;border-radius:10px;margin-bottom:20px;}";
    html += ".stat{background:#f8f9fa;padding:15px;border-radius:8px;margin:10px 0;display:flex;justify-content:space-between;align-items:center;}";
    html += ".progress{width:100%;height:25px;background:rgba(255,255,255,0.2);border-radius:12px;overflow:hidden;margin-top:10px;}";
    html += ".fill{height:100%;background:white;display:flex;align-items:center;justify-content:center;font-weight:bold;color:#667eea;transition:width 0.3s;}";
    html += ".warning{background:#fff3cd;border-left:4px solid #ffc107;padding:10px;border-radius:5px;margin-top:10px;color:#856404;}</style></head><body>";
    html += "<div class='container'><h1 style='text-align:center;margin-bottom:20px;'>📊 Nichoir ESP32-CAM</h1>";
    
    // Section batterie
    html += "<div class='battery'><h2 style='margin:0 0 10px 0;'>🔋 ";
    html += isUsbPowered ? "Alimentation USB" : "Batterie";
    html += "</h2><div style='font-size:32px;font-weight:bold;'>" + String(currentVoltage, 2) + "V</div>";
    html += "<div class='progress'><div class='fill' style='width:" + String(currentPercent) + "%'>" + String(currentPercent) + "%</div></div>";
    
    if(!isUsbPowered && currentPercent < 20) {
        html += "<div class='warning'>⚠️ Batterie faible - Recharger bientôt</div>";
    }
    if(!isUsbPowered && currentPercent < 10) {
        html += "<div class='warning' style='background:#f8d7da;border-color:#dc3545;color:#721c24;'>🚨 CRITIQUE - Recharger maintenant !</div>";
    }
    html += "</div>";
    
    // Stats
    html += "<div class='stat'><span>📡 WiFi</span><b>" + String(WiFi.status() == WL_CONNECTED ? "✅" : "❌") + "</b></div>";
    html += "<div class='stat'><span>🔗 MQTT</span><b>" + String(mqttClient.connected() ? "✅" : "❌") + "</b></div>";
    html += "<div class='stat'><span>📸 Photos</span><b>" + String(photoCounter) + "</b></div>";
    html += "<div class='stat'><span>🎯 PIR</span><b>" + String(pirDetectionCount) + "</b></div>";
    html += "<div class='stat'><span>📡 PIR État</span><b>" + String(digitalRead(PIR_PIN) ? "🔴 ACTIF" : "🟢 REPOS") + "</b></div>";
    html += "<div class='stat'><span>⏰ Uptime</span><b>" + String(millis()/1000) + "s</b></div>";
    html += "<div class='stat'><span>📊 ADC</span><b>" + String(analogRead(BATTERY_ADC_PIN)) + "</b></div>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
}

void handleLogin() {
    if(server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String pass = server.arg("password");
        
        if(ssid == STORED_SSID && pass == STORED_PASSWORD) {
            WiFi.begin(STORED_SSID, STORED_PASSWORD);
            int i = 0;
            while(WiFi.status() != WL_CONNECTED && i++ < 30) { 
                delay(500); 
                Serial.print("."); 
            }
            
            if(WiFi.status() == WL_CONNECTED) {
                wifiAuth = true;
                Serial.println("\n✅ WiFi: " + WiFi.localIP().toString());
                
                mqttClient.setServer(mqtt_server, mqtt_port);
                mqttClient.setBufferSize(16384);
                mqttClient.setKeepAlive(60);
                reconnectMQTT();
                updateBattery();
                
                Serial.println("⏳ Calibration PIR 30s...");
                for(int j=30; j>0; j--) { 
                    Serial.printf("%ds ", j); 
                    delay(1000); 
                }
                pirCalibrated = true;
                Serial.println("\n✅ PIR calibré");
                
                server.send(200, "text/html", 
                    "<html><head><meta charset='UTF-8'></head><body style='text-align:center;padding:50px;font-family:Arial;'>"
                    "<h1 style='color:#38ef7d;'>✅ Connecté !</h1>"
                    "<p style='font-size:18px;'>IP: " + WiFi.localIP().toString() + "</p>"
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
    html += ".logo{text-align:center;font-size:64px;margin-bottom:20px;}h1{text-align:center;margin-bottom:30px;color:#333;}";
    html += ".form-group{margin-bottom:20px;}label{display:block;margin-bottom:8px;font-weight:600;color:#555;}";
    html += "input{width:100%;padding:12px;border:2px solid #e0e0e0;border-radius:8px;font-size:16px;}";
    html += "button{width:100%;padding:14px;background:linear-gradient(135deg,#667eea,#764ba2);color:white;border:none;border-radius:8px;font-size:18px;font-weight:bold;cursor:pointer;}";
    html += ".info{background:#e3f2fd;padding:15px;border-radius:8px;margin-top:20px;font-size:13px;text-align:center;color:#1976d2;}</style></head><body>";
    html += "<div class='container'><div class='logo'>🔐</div><h1>ESP32-CAM Nichoir</h1>";
    html += "<form action='/login' method='POST'><div class='form-group'><label>🌐 SSID</label>";
    html += "<input type='text' name='ssid' value='Orange-f6a09' required></div>";
    html += "<div class='form-group'><label>🔒 Password</label><input type='password' name='password' required></div>";
    html += "<button type='submit'>🚀 Connecter</button></form>";
    html += "<div class='info'>📡 MQTT: 192.168.2.30<br>🔋 Batterie 1S 3.7V<br>📸 PIR GPIO 13</div></div></body></html>";
    server.send(200, "text/html", html);
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n╔═══════════════════════════════╗");
    Serial.println("║  ESP32-CAM Nichoir Connecté  ║");
    Serial.println("║  PIR + MQTT + Batterie       ║");
    Serial.println("╚═══════════════════════════════╝\n");
    
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    // ✅ CRITIQUE: GPIO 33 pour maintien batterie (pas LED!)
    pinMode(BAT_HOLD_PIN, OUTPUT);
    digitalWrite(BAT_HOLD_PIN, HIGH);  // OBLIGATOIRE pour batterie
    delay(100);
    
    pinMode(PIR_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);  // GPIO 2 (vraie LED)
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetAttenuation(ADC_11db);
    analogSetWidth(12);
    
    Serial.printf("🔧 BAT_HOLD (GPIO %d): ACTIVÉ ✅\n", BAT_HOLD_PIN);
    Serial.printf("🔧 LED (GPIO %d): Configurée\n", LED_PIN);
    Serial.printf("🔧 PIR (GPIO %d): Configuré\n", PIR_PIN);
    
    config_camera();
    
    WiFi.softAP("ESP32-CAM-Login", "12345678");
    Serial.println("\n📡 AP: ESP32-CAM-Login / 12345678");
    Serial.println("🌐 Ouvrez: http://192.168.4.1\n");
    
    server.on("/", handleRoot);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/status", handleStatus);
    server.begin();
    
    updateBattery();
}

// ===== LOOP =====
void loop() {
    server.handleClient();
    
    if(systemReady && wifiAuth && WiFi.status() == WL_CONNECTED) {
        if(!mqttClient.connected()) {
            static unsigned long lastReconnect = 0;
            if(millis() - lastReconnect > 10000) { 
                reconnectMQTT(); 
                lastReconnect = millis(); 
            }
        } else {
            mqttClient.loop();
        }
        
        if(pirCalibrated) handlePIR();
        
        if(millis() - lastPhotoTime >= PHOTO_INTERVAL) {
            Serial.println("⏰ Photo auto");
            if(sendPhotoMQTT(false)) photoCounter++;
            lastPhotoTime = millis();
        }
    }
    delay(50);
}

/*
 * ═══════════════════════════════════════════════════
 * ✅ CORRECTIONS APPLIQUÉES:
 * ═══════════════════════════════════════════════════
 * 
 * 1. GPIO 33 = BAT_HOLD_PIN uniquement (pas LED!)
 * 2. GPIO 2 = LED rouge réelle
 * 3. Détection USB vs Batterie (ADC < 50)
 * 4. JSON MQTT inclut "usb_powered"
 * 5. Interface web affiche source alimentation
 * 
 * CALIBRATION:
 * - Mesurez tension avec multimètre
 * - Ajustez VOLTAGE_CALIBRATION (ligne 24)
 * - Formule: tension_réelle / tension_affichée
 * 
 * MQTT JSON:
 * {
 *   "trigger": "PIR"/"AUTO",
 *   "battery_voltage": 4.10,
 *   "battery_percent": 95,
 *   "usb_powered": false,
 *   "image": "base64..."
 * }
 */