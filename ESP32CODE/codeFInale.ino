#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "base64.h"
#include "esp_sleep.h"

// ===== IDENTIFIANTS WiFi STOCKÉS =====
const char* STORED_SSID = "electroProjectWifi";
const char* STORED_PASSWORD = "B1MesureEnv";

// ===== CONFIGURATION MQTT =====
const char* mqtt_server = "192.168.2.30";
const int mqtt_port = 1883;
const char* mqtt_topic = "nichoir/photo";

// ===== CONFIGURATION PIR =====
const int PIR_PIN = 13;               // Pin du capteur PIR
const int LED_PIN = 2;                // LED GPIO 2 (pas 33!)
const int CALIBRATION_TIME = 30;      // Temps de calibration en secondes
const unsigned long MIN_DELAY_PIR = 5000;  // Délai minimum entre 2 détections (5s)

// ===== CONFIGURATION BATTERIE M5Stack TimerCAM =====
const int BATTERY_ADC_PIN = 38;
const int BAT_HOLD_PIN = 33;
const float ADC_REFERENCE = 3.3;
const int ADC_RESOLUTION = 4095;
const float VOLTAGE_CALIBRATION = 1.38;

// ===== CONFIGURATION SLEEP MODE =====
const uint64_t TIME_TO_SLEEP = 60;  // Réveil toutes les 60 secondes pour photo auto
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int photoCounter = 0;
RTC_DATA_ATTR int pirDetectionCount = 0;

// ===== PINS CAMÉRA (M5Stack TimerCAM) =====
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
WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool wifiAuthenticated = false;
bool systemReady = false;
bool pirCalibrated = false;
esp_sleep_wakeup_cause_t wakeup_reason;

// ===== FONCTIONS BATTERIE =====
float readBatteryVoltage() {
    pinMode(BAT_HOLD_PIN, OUTPUT);
    digitalWrite(BAT_HOLD_PIN, HIGH);  // Active lecture batterie
    delay(100);  // Temps de stabilisation
    
    analogSetAttenuation(ADC_11db);
    
    long adcSum = 0;
    for(int i = 0; i < 20; i++) {
        adcSum += analogRead(BATTERY_ADC_PIN);
        delay(5);
    }
    int adcValue = adcSum / 20;
    
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

// ===== PAGES HTML (simplifiées) =====
const char* loginPage = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset='UTF-8'><title>Login</title></head>
<body style='font-family:Arial;text-align:center;padding:50px;background:#667eea;color:white;'>
<h1>ESP32-CAM Login</h1>
<form action='/login' method='POST'>
<input type='text' name='ssid' placeholder='SSID' value='Orange-f6a09' style='margin:10px;padding:10px;width:200px;'><br>
<input type='password' name='password' placeholder='Password' style='margin:10px;padding:10px;width:200px;'><br>
<button type='submit' style='padding:15px 30px;font-size:18px;'>Connecter</button>
</form>
</body></html>
)rawliteral";

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
        config.frame_size = FRAMESIZE_SVGA;  // Réduit à SVGA pour stabilité
        config.jpeg_quality = 12;
        config.fb_count = 1;  // Réduit à 1 pour économie mémoire
    } else {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Erreur caméra: 0x%x\n", err);
        delay(1000);
        ESP.restart();
    }
    Serial.println("✅ Caméra OK");
}

// ===== CALIBRATION PIR =====
void calibratePIR() {
    Serial.println("\n⏳ Calibration du PIR...");
    Serial.println("   (Attendre 30 secondes sans bouger)");
    
    for (int i = CALIBRATION_TIME; i > 0; i--) {
        if(i % 10 == 0 || i <= 5) {
            Serial.printf("   %d secondes...\n", i);
        }
        delay(1000);
    }
    
    pirCalibrated = true;
    Serial.println("✅ Calibration PIR terminée!");
}

// ===== RECONNEXION MQTT =====
bool reconnectMQTT() {
    Serial.println("\n🔄 Connexion MQTT...");
    
    String clientId = "ESP32CAM-" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
        Serial.println("✅ MQTT connecté !");
        return true;
    } else {
        Serial.printf("❌ MQTT échec, rc=%d\n", mqttClient.state());
        return false;
    }
}

// ===== ENVOI PHOTO MQTT =====
bool captureAndSendPhotoMQTT(bool isPirTriggered = false) {
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi déconnecté");
        return false;
    }

    if (!mqttClient.connected()) {
        if (!reconnectMQTT()) {
            return false;
        }
    }

    // LED indication
    digitalWrite(LED_PIN, HIGH);
    
    camera_fb_t * fb = esp_camera_fb_get();
    if(!fb) {
        Serial.println("❌ Échec capture");
        digitalWrite(LED_PIN, LOW);
        return false;
    }

    Serial.printf("📸 Capture: %d octets\n", fb->len);

    // Encodage Base64
    String encoded = base64::encode(fb->buf, fb->len);
    
    // Lecture batterie
    float voltage = readBatteryVoltage();
    int batteryPercent = getBatteryPercentage(voltage);
    
    String triggerType = isPirTriggered ? "PIR" : "AUTO";
    String filename = "nichoir_" + triggerType + "_" + String(photoCounter) + "_" + String(millis()) + ".jpg";
    
    String jsonMessage = "{";
    jsonMessage += "\"filename\":\"" + filename + "\",";
    jsonMessage += "\"timestamp\":\"" + String(millis()) + "\",";
    jsonMessage += "\"trigger\":\"" + triggerType + "\",";
    jsonMessage += "\"pir_count\":" + String(pirDetectionCount) + ",";
    jsonMessage += "\"boot_count\":" + String(bootCount) + ",";
    jsonMessage += "\"battery_voltage\":" + String(voltage, 2) + ",";
    jsonMessage += "\"battery_percent\":" + String(batteryPercent) + ",";
    jsonMessage += "\"image\":\"" + encoded + "\"";
    jsonMessage += "}";

    Serial.printf("📦 JSON: %d octets, Batterie: %.2fV (%d%%)\n", 
                  jsonMessage.length(), voltage, batteryPercent);

    bool success = mqttClient.publish(mqtt_topic, jsonMessage.c_str(), false);
    
    esp_camera_fb_return(fb);
    
    // LED feedback
    for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(100);
        digitalWrite(LED_PIN, HIGH);
        delay(100);
    }
    digitalWrite(LED_PIN, LOW);

    if(success) {
        Serial.println("✅ Photo envoyée MQTT");
        photoCounter++;
        return true;
    } else {
        Serial.println("❌ Échec publication MQTT");
        return false;
    }
}

// ===== CONFIGURATION WAKE-UP PIR =====
void setupPIRWakeup() {
    // Configure PIR comme source de réveil (EXT0)
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 1);  // 1 = HIGH level
    Serial.println("✅ PIR configuré comme wake-up source");
}

// ===== MODE DEEP SLEEP =====
void goToSleep(uint64_t time_to_sleep) {
    Serial.println("\n💤 Entrée en mode SLEEP...");
    Serial.printf("   ⏰ Réveil dans %llu secondes (ou sur détection PIR)\n", time_to_sleep);
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    delay(100);
    
    // Configure les sources de réveil
    esp_sleep_enable_timer_wakeup(time_to_sleep * 1000000ULL);  // Timer: 60s
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 1);  // PIR: niveau HIGH
    
    // Mise en veille
    esp_deep_sleep_start();
}

// ===== ANALYSE CAUSE DE RÉVEIL =====
void print_wakeup_reason() {
    wakeup_reason = esp_sleep_get_wakeup_cause();
    
    Serial.println("\n╔════════════════════════════════════╗");
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("║  🔴 RÉVEIL: PIR DÉTECTION !       ║");
            pirDetectionCount++;
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("║  ⏰ RÉVEIL: TIMER (Photo auto)    ║");
            break;
        default:
            Serial.println("║  🔌 RÉVEIL: POWER ON               ║");
            break;
    }
    Serial.println("╚════════════════════════════════════╝");
    Serial.printf("Boot #%d | Photos: %d | PIR: %d\n\n", 
                  bootCount, photoCounter, pirDetectionCount);
}

// ===== HANDLERS =====
void handleRoot() {
    server.send(200, "text/html", loginPage);
}

void handleLogin() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String pass = server.arg("password");
        
        Serial.println("\n=== AUTHENTIFICATION ===");
        
        if (ssid == STORED_SSID && pass == STORED_PASSWORD) {
            Serial.println("✅ Identifiants OK");
            
            WiFi.begin(STORED_SSID, STORED_PASSWORD);
            
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 30) {
                delay(500);
                Serial.print(".");
                attempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                wifiAuthenticated = true;
                Serial.println("\n✅ WiFi connecté !");
                Serial.print("IP: ");
                Serial.println(WiFi.localIP());
                
                mqttClient.setServer(mqtt_server, mqtt_port);
                mqttClient.setBufferSize(20480);  // 20KB buffer
                mqttClient.setKeepAlive(60);
                mqttClient.setSocketTimeout(15);
                
                delay(500);
                reconnectMQTT();
                
                // Calibration PIR (première fois seulement)
                if(bootCount == 0) {
                    calibratePIR();
                    pirCalibrated = true;
                }
                
                server.send(200, "text/html", 
                    "<html><body style='text-align:center;padding:50px;'>"
                    "<h1>✅ Connecté!</h1>"
                    "<p>IP: " + WiFi.localIP().toString() + "</p>"
                    "<p>Système prêt - Mode SLEEP activé</p>"
                    "</body></html>");
                
                delay(2000);
                systemReady = true;
            } else {
                Serial.println("\n❌ WiFi échec");
                server.send(200, "text/html", 
                    "<html><body><h1>❌ Échec WiFi</h1><a href='/'>Retour</a></body></html>");
            }
        } else {
            server.send(200, "text/html", 
                "<html><body><h1>❌ Mauvais identifiants</h1><a href='/'>Retour</a></body></html>");
        }
    }
}

void setup() {
    // Incrémenter boot counter
    bootCount++;
    
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n╔═══════════════════════════════════╗");
    Serial.println("║  ESP32-CAM + PIR + DEEP SLEEP     ║");
    Serial.println("╚═══════════════════════════════════╝");
    
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    // Configuration des pins
    pinMode(PIR_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BAT_HOLD_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BAT_HOLD_PIN, HIGH);

    // Analyse cause de réveil
    print_wakeup_reason();

    // Si premier boot, lancer mode AP pour configuration
    if(bootCount == 1 || !wifiAuthenticated) {
        config_camera();
        
        WiFi.softAP("ESP32-CAM-Login", "12345678");
        Serial.println("\n📡 AP: ESP32-CAM-Login / 12345678");
        Serial.println("IP: " + WiFi.softAPIP().toString());

        server.on("/", handleRoot);
        server.on("/login", HTTP_POST, handleLogin);
        server.begin();

        Serial.println("✅ Serveur démarré");
        Serial.println("http://192.168.4.1\n");
        
        // Attente configuration (max 5 minutes)
        unsigned long startTime = millis();
        while(!systemReady && (millis() - startTime < 300000)) {
            server.handleClient();
            delay(10);
        }
        
        if(!systemReady) {
            Serial.println("⏱ Timeout configuration - redémarrage");
            ESP.restart();
        }
    } 
    // Si déjà configuré, connexion directe
    else {
        config_camera();
        
        Serial.println("🔄 Connexion WiFi...");
        WiFi.begin(STORED_SSID, STORED_PASSWORD);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if(WiFi.status() == WL_CONNECTED) {
            Serial.println("\n✅ WiFi OK");
            
            mqttClient.setServer(mqtt_server, mqtt_port);
            mqttClient.setBufferSize(20480);
            mqttClient.setKeepAlive(60);
            
            reconnectMQTT();
            systemReady = true;
            wifiAuthenticated = true;
        } else {
            Serial.println("\n❌ WiFi échec - redémarrage");
            ESP.restart();
        }
    }

    // CAPTURE PHOTO
    if(systemReady && wifiAuthenticated) {
        bool isPirTrigger = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);
        
        Serial.println("\n📸 Capture photo...");
        if(captureAndSendPhotoMQTT(isPirTrigger)) {
            Serial.println("✅ Photo envoyée avec succès!");
        } else {
            Serial.println("❌ Échec envoi photo");
        }
        
        // Attendre quelques secondes pour voir le résultat
        delay(2000);
        
        // Entrer en mode SLEEP
        goToSleep(TIME_TO_SLEEP);
    }
}

void loop() {
    // Le loop ne sera jamais atteint car on entre en deep sleep dans setup()
    // Mais on le garde pour compatibilité
    delay(1000);
}
