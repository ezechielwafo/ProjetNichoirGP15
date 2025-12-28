#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "base64.h"

// ===== IDENTIFIANTS WiFi STOCKÉS =====
const char* STORED_SSID = "electroProjectWifi";
const char* STORED_PASSWORD = "B1MesureEnv";

// ===== CONFIGURATION MQTT =====
const char* mqtt_server = "192.168.2.30";
const int mqtt_port = 1883;
const char* mqtt_topic = "nichoir/photo";

// ===== CONFIGURATION PIR =====
const int PIR_PIN = 13;
const int LED_PIN = 33;
const int CALIBRATION_TIME = 30;
const unsigned long MIN_DELAY_PIR = 5000;

// ===== CONFIGURATION BATTERIE =====
const int BATTERY_PIN = 34;           // GPIO35 (essayer 33, 34, 35, 36, 39)
const float VOLTAGE_DIVIDER = 2.0;    // Pont diviseur (ajuster selon schéma)
const float VREF = 3.3;               // Tension de référence ADC
const int ADC_MAX = 4095;             // Résolution 12 bits
const float BATTERY_MAX = 4.2;        // LiPo complètement chargée (V)
const float BATTERY_MIN = 3.0;        // LiPo vide (V)

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

int photoCounter = 0;
int pirDetectionCount = 0;
unsigned long lastPhotoTime = 0;
unsigned long lastPirDetectionTime = 0;
const unsigned long PHOTO_INTERVAL = 60000;
bool wifiAuthenticated = false;
bool systemReady = false;
bool pirCalibrated = false;
int lastPirState = LOW;

// ===== FONCTION LECTURE BATTERIE =====
void testAllADCPins() {
    Serial.println("\n🔍 TEST DE TOUS LES GPIO ADC :");
    int adcPins[] = {32, 33, 34, 35, 36, 39};
    
    for(int i = 0; i < 6; i++) {
        int pin = adcPins[i];
        int sum = 0;
        for(int j = 0; j < 10; j++) {
            sum += analogRead(pin);
            delay(10);
        }
        int adcValue = sum / 10;
        float voltage = (adcValue / (float)ADC_MAX) * VREF * VOLTAGE_DIVIDER;
        Serial.printf("   GPIO %d: ADC=%d, Voltage=%.2fV\n", pin, adcValue, voltage);
    }
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

float readBatteryVoltage() {
    // Moyenne de 10 lectures pour stabilité
    int sum = 0;
    for(int i = 0; i < 10; i++) {
        sum += analogRead(BATTERY_PIN);
        delay(10);
    }
    int adcValue = sum / 10;
    
    // Conversion ADC → Tension
    float voltage = (adcValue / (float)ADC_MAX) * VREF * VOLTAGE_DIVIDER;
    
    return voltage;
}

int getBatteryPercentage() {
    float voltage = readBatteryVoltage();
    
    // Conversion tension → pourcentage (0-100%)
    if (voltage >= BATTERY_MAX) return 100;
    if (voltage <= BATTERY_MIN) return 0;
    
    float percentage = ((voltage - BATTERY_MIN) / (BATTERY_MAX - BATTERY_MIN)) * 100.0;
    return (int)percentage;
}

// ===== PAGES HTML =====
const char* loginPage = R"rawliteral(
<!DOCTYPE html>
<html lang='fr'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Connexion WiFi - ESP32-CAM</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            background: white;
            padding: 40px;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            max-width: 420px;
            width: 100%;
        }
        .logo { text-align: center; font-size: 64px; margin-bottom: 20px; }
        h1 { color: #333; text-align: center; margin-bottom: 10px; font-size: 28px; }
        .subtitle { text-align: center; color: #666; margin-bottom: 30px; font-size: 14px; }
        .form-group { margin-bottom: 25px; }
        label { display: block; margin-bottom: 8px; color: #444; font-weight: 600; font-size: 14px; }
        input[type='text'], input[type='password'] {
            width: 100%;
            padding: 14px;
            border: 2px solid #e0e0e0;
            border-radius: 10px;
            font-size: 16px;
            background: #f8f9fa;
        }
        button {
            width: 100%;
            padding: 16px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 10px;
            font-size: 18px;
            font-weight: bold;
            cursor: pointer;
        }
        .info-box {
            background: #e3f2fd;
            padding: 15px;
            border-radius: 10px;
            margin-top: 20px;
            font-size: 13px;
            color: #1976d2;
            text-align: center;
        }
    </style>
</head>
<body>
    <div class='container'>
        <div class='logo'>🔐</div>
        <h1>Authentification WiFi</h1>
        <p class='subtitle'>ESP32-CAM TimerCam - Nichoir Connecté (PIR + MQTT)</p>
        <form action='/login' method='POST'>
            <div class='form-group'>
                <label for='ssid'>🌐 SSID</label>
                <input type='text' id='ssid' name='ssid' value='electroProjectWifi' required>
            </div>
            <div class='form-group'>
                <label for='password'>🔒 Mot de passe</label>
                <input type='password' id='password' name='password' required>
            </div>
            <button type='submit'>🚀 Se connecter</button>
        </form>
        <div class='info-box'>
            💡 Broker MQTT: 192.168.2.30:1883<br>
            📡 Détection PIR: GPIO 13<br>
            🔋 Mesure batterie: GPIO 38
        </div>
    </div>
</body>
</html>
)rawliteral";

const char* successPage = R"rawliteral(
<!DOCTYPE html>
<html lang='fr'>
<head>
    <meta charset='UTF-8'>
    <title>Connexion réussie</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: Arial, sans-serif;
            background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        .container {
            background: white;
            padding: 50px;
            border-radius: 20px;
            text-align: center;
        }
        .icon { font-size: 96px; margin-bottom: 20px; }
        h1 { color: #11998e; margin-bottom: 15px; }
        .ip { background: #f0f0f0; padding: 15px; border-radius: 10px; margin: 25px 0; font-family: monospace; font-size: 20px; }
    </style>
</head>
<body>
    <div class='container'>
        <div class='icon'>✅</div>
        <h1>Connexion WiFi réussie !</h1>
        <div class='ip'>IP: %IP_ADDRESS%</div>
        <p>%MQTT_STATUS%</p>
    </div>
    <script>setTimeout(() => window.location.href = '/status', 3000);</script>
</body>
</html>
)rawliteral";

const char* statusPage = R"rawliteral(
<!DOCTYPE html>
<html lang='fr'>
<head>
    <meta charset='UTF-8'>
    <title>Statut ESP32-CAM</title>
    <meta http-equiv='refresh' content='5'>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: Arial; background: #f5f5f5; padding: 20px; }
        .container { max-width: 800px; margin: 0 auto; background: white; border-radius: 15px; padding: 30px; }
        h1 { color: #333; margin-bottom: 30px; text-align: center; }
        .stat {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 15px;
            display: flex;
            justify-content: space-between;
        }
        .stat-label { font-weight: bold; color: #666; }
        .stat-value { color: #667eea; font-size: 18px; font-weight: bold; }
        .status-ok { color: #38ef7d; }
        .status-error { color: #f5576c; }
        .status-warning { color: #ff9800; }
        .pir-section { background: #e3f2fd; padding: 20px; border-radius: 10px; margin-top: 20px; }
        .battery-bar {
            width: 100%;
            height: 20px;
            background: #e0e0e0;
            border-radius: 10px;
            overflow: hidden;
            margin-top: 10px;
        }
        .battery-fill {
            height: 100%;
            transition: width 0.3s;
        }
    </style>
</head>
<body>
    <div class='container'>
        <h1>📊 Statut ESP32-CAM + PIR</h1>
        <div class='stat'>
            <span class='stat-label'>🌐 WiFi</span>
            <span class='stat-value status-ok'>%WIFI_STATUS%</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>📡 IP</span>
            <span class='stat-value'>%IP_ADDRESS%</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>🔗 MQTT</span>
            <span class='stat-value %MQTT_CLASS%'>%MQTT_STATUS%</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>🔋 Batterie</span>
            <span class='stat-value %BATTERY_CLASS%'>%BATTERY_PERCENT%% (%BATTERY_VOLTAGE%V)</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>📸 Photos totales</span>
            <span class='stat-value'>%PHOTO_COUNT%</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>🎯 Détections PIR</span>
            <span class='stat-value'>%PIR_COUNT%</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>⏰ Uptime</span>
            <span class='stat-value'>%UPTIME%s</span>
        </div>
        <div class='pir-section'>
            <h3>📡 État PIR</h3>
            <p>Calibration: %PIR_CALIBRATION%</p>
            <p>État actuel: %PIR_STATE%</p>
            <p>Dernière détection: %LAST_DETECTION%</p>
        </div>
    </div>
</body>
</html>
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
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 15;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 20;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Erreur caméra: 0x%x\n", err);
        ESP.restart();
    }
    Serial.println("✅ Caméra OK");
}

// ===== CALIBRATION PIR =====
void calibratePIR() {
    Serial.println("\n⏳ Calibration du PIR...");
    Serial.println("   (Attendre 30 secondes sans bouger)");
    
    for (int i = CALIBRATION_TIME; i > 0; i--) {
        Serial.printf("   Calibration: %d secondes restantes\n", i);
        delay(1000);
    }
    
    pirCalibrated = true;
    Serial.println("✅ Calibration PIR terminée!");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("📡 Détection PIR active - Prêt à capturer !");
}

// ===== RECONNEXION MQTT =====
bool reconnectMQTT() {
    Serial.println("\n🔄 Tentative connexion MQTT...");
    Serial.printf("   Broker: %s:%d\n", mqtt_server, mqtt_port);
    
    WiFiClient testClient;
    Serial.print("   Test TCP... ");
    if (!testClient.connect(mqtt_server, mqtt_port, 5000)) {
        Serial.println("❌ ÉCHEC");
        return false;
    }
    Serial.println("✅ OK");
    testClient.stop();
    
    String clientId = "ESP32CAM-" + String(random(0xffff), HEX);
    Serial.print("   Connexion MQTT (ID: " + clientId + ")... ");
    
    if (mqttClient.connect(clientId.c_str())) {
        Serial.println("✅ CONNECTÉ !");
        return true;
    } else {
        Serial.print("❌ ÉCHEC, rc=");
        Serial.println(mqttClient.state());
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

    // Lecture batterie AVANT capture
    float batteryVoltage = readBatteryVoltage();
    int batteryPercent = getBatteryPercentage();
    
    Serial.printf("🔋 Batterie: %.2fV (%d%%)\n", batteryVoltage, batteryPercent);

    // Allumer la LED pendant la capture
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
    
    String triggerType = isPirTriggered ? "PIR" : "AUTO";
    String filename = "nichoir_" + triggerType + "_" + String(photoCounter) + "_" + String(millis()) + ".jpg";
    
    // JSON avec mesures réelles
    String jsonMessage = "{";
    jsonMessage += "\"filename\":\"" + filename + "\",";
    jsonMessage += "\"timestamp\":\"" + String(millis()) + "\",";
    jsonMessage += "\"trigger\":\"" + triggerType + "\",";
    jsonMessage += "\"pir_count\":" + String(pirDetectionCount) + ",";
    jsonMessage += "\"battery_percent\":" + String(batteryPercent) + ",";
    jsonMessage += "\"battery_voltage\":" + String(batteryVoltage, 2) + ",";
    jsonMessage += "\"image\":\"" + encoded + "\"";
    jsonMessage += "}";

    Serial.printf("📦 Taille JSON: %d octets\n", jsonMessage.length());

    bool success = mqttClient.publish(mqtt_topic, jsonMessage.c_str(), false);
    
    esp_camera_fb_return(fb);
    
    // LED clignote 3 fois pour indiquer succès
    for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(100);
        digitalWrite(LED_PIN, HIGH);
        delay(100);
    }
    digitalWrite(LED_PIN, LOW);

    if(success) {
        Serial.println("✅ Photo envoyée via MQTT");
        return true;
    } else {
        Serial.println("❌ Échec publication MQTT");
        return false;
    }
}

// ===== GESTION DÉTECTION PIR =====
void handlePIRDetection() {
    int currentPirState = digitalRead(PIR_PIN);
    unsigned long currentTime = millis();
    
    if (currentPirState == HIGH && lastPirState == LOW) {
        if (currentTime - lastPirDetectionTime > MIN_DELAY_PIR) {
            pirDetectionCount++;
            
            Serial.println("\n╔════════════════════════════════════╗");
            Serial.println("║   🔴 MOUVEMENT DÉTECTÉ !          ║");
            Serial.println("╚════════════════════════════════════╝");
            Serial.printf("📊 Détection PIR #%d\n", pirDetectionCount);
            Serial.printf("⏰ Timestamp: %lu ms\n", currentTime);
            Serial.println("📸 Capture photo en cours...");
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            
            if(captureAndSendPhotoMQTT(true)) {
                photoCounter++;
                Serial.printf("✅ Photo PIR #%d envoyée\n", photoCounter);
            } else {
                Serial.println("❌ Échec envoi photo PIR");
            }
            
            lastPirDetectionTime = currentTime;
        }
    }
    else if (currentPirState == LOW && lastPirState == HIGH) {
        Serial.println("   ✓ Fin de mouvement détecté");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    }
    
    lastPirState = currentPirState;
}

// ===== HANDLERS =====
void handleRoot() {
    server.send(200, "text/html", loginPage);
}

void handleLogin() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String pass = server.arg("password");
        
        // Nettoyage des espaces
        ssid.trim();
        pass.trim();
        
        Serial.println("\n=== AUTHENTIFICATION ===");
        Serial.println("SSID reçu: [" + ssid + "]");
        Serial.println("SSID stocké: [" + String(STORED_SSID) + "]");
        Serial.println("Pass reçu: [" + pass + "]");
        Serial.println("Pass stocké: [" + String(STORED_PASSWORD) + "]");
        Serial.printf("Longueurs: ssid=%d/%d, pass=%d/%d\n", 
                      ssid.length(), strlen(STORED_SSID),
                      pass.length(), strlen(STORED_PASSWORD));
        
        if (ssid == STORED_SSID && pass == STORED_PASSWORD) {
            Serial.println("✅ OK !");
            
            WiFi.begin(STORED_SSID, STORED_PASSWORD);
            Serial.print("WiFi");
            
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
                mqttClient.setBufferSize(16384);
                mqttClient.setKeepAlive(60);
                mqttClient.setSocketTimeout(10);
                
                delay(1000);
                bool mqttOk = reconnectMQTT();
                
                calibratePIR();
                
                String response = successPage;
                response.replace("%IP_ADDRESS%", WiFi.localIP().toString());
                if(mqttOk) {
                    response.replace("%MQTT_STATUS%", "✅ MQTT connecté + PIR calibré");
                } else {
                    response.replace("%MQTT_STATUS%", "⚠ MQTT non accessible");
                }
                server.send(200, "text/html", response);
                
                delay(3000);
                systemReady = true;
            } else {
                Serial.println("\n❌ WiFi échec");
                server.send(200, "text/html", 
                    "<html><body style='text-align:center; padding:50px;'>"
                    "<h1>❌ Échec WiFi</h1>"
                    "<a href='/'>Retour</a></body></html>");
            }
        } else {
            Serial.println("❌ Mauvais identifiants");
            server.send(200, "text/html", 
                "<html><body style='text-align:center; padding:50px;'>"
                "<h1>❌ Identifiants incorrects</h1>"
                "<a href='/'>Retour</a></body></html>");
        }
    }
}

void handleStatus() {
    float voltage = readBatteryVoltage();
    int percent = getBatteryPercentage();
    
    String response = statusPage;
    response.replace("%WIFI_STATUS%", WiFi.status() == WL_CONNECTED ? "Connecté" : "Déconnecté");
    response.replace("%IP_ADDRESS%", WiFi.localIP().toString());
    response.replace("%MQTT_STATUS%", mqttClient.connected() ? "Connecté" : "Déconnecté");
    response.replace("%MQTT_CLASS%", mqttClient.connected() ? "status-ok" : "status-error");
    
    // Affichage batterie avec code couleur
    String batteryClass = "status-ok";
    if(percent < 20) batteryClass = "status-error";
    else if(percent < 40) batteryClass = "status-warning";
    
    response.replace("%BATTERY_CLASS%", batteryClass);
    response.replace("%BATTERY_PERCENT%", String(percent));
    response.replace("%BATTERY_VOLTAGE%", String(voltage, 2));
    
    response.replace("%PHOTO_COUNT%", String(photoCounter));
    response.replace("%PIR_COUNT%", String(pirDetectionCount));
    response.replace("%UPTIME%", String(millis() / 1000));
    response.replace("%PIR_CALIBRATION%", pirCalibrated ? "✅ OK" : "⏳ En cours...");
    response.replace("%PIR_STATE%", digitalRead(PIR_PIN) ? "🔴 DÉTECTION" : "🟢 REPOS");
    
    if(lastPirDetectionTime > 0) {
        unsigned long timeSince = (millis() - lastPirDetectionTime) / 1000;
        response.replace("%LAST_DETECTION%", String(timeSince) + "s");
    } else {
        response.replace("%LAST_DETECTION%", "Aucune");
    }
    
    server.send(200, "text/html", response);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n╔═══════════════════════════════════╗");
    Serial.println("║  ESP32-CAM + PIR + MQTT + BAT    ║");
    Serial.println("╚═══════════════════════════════════╝");

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    // Configuration des pins
    pinMode(PIR_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // Configuration ADC pour batterie
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    Serial.printf("🔧 Configuration PIR: GPIO %d\n", PIR_PIN);
    Serial.printf("🔧 LED: GPIO %d\n", LED_PIN);
    Serial.printf("🔧 Batterie: GPIO %d\n", BATTERY_PIN);
    
    // Test de tous les GPIO ADC
    testAllADCPins();
    
    // Test initial batterie
    float initialVoltage = readBatteryVoltage();
    int initialPercent = getBatteryPercentage();
    Serial.printf("🔋 Niveau initial: %.2fV (%d%%)\n", initialVoltage, initialPercent);

    config_camera();

    WiFi.softAP("ESP32-CAM-Login", "12345678");
    Serial.println("\n📡 AP: ESP32-CAM-Login / 12345678");
    Serial.println("IP: " + WiFi.softAPIP().toString());

    server.on("/", handleRoot);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/status", handleStatus);
    server.begin();

    Serial.println("✅ Serveur démarré");
    Serial.println("http://192.168.4.1\n");
}

void loop() {
    server.handleClient();

    if (systemReady && wifiAuthenticated && WiFi.status() == WL_CONNECTED) {
        if (!mqttClient.connected()) {
            static unsigned long lastReconnect = 0;
            if (millis() - lastReconnect > 10000) {
                reconnectMQTT();
                lastReconnect = millis();
            }
        } else {
            mqttClient.loop();
        }

        if(pirCalibrated) {
            handlePIRDetection();
        }

        unsigned long currentTime = millis();
        if (currentTime - lastPhotoTime >= PHOTO_INTERVAL) {
            Serial.println("\n⏰ Capture auto programmée");
            
            if(captureAndSendPhotoMQTT(false)) {
                photoCounter++;
                Serial.printf("✅ Photo auto #%d OK\n", photoCounter);
            }
            
            lastPhotoTime = currentTime;
        }
    }

    delay(50);
}