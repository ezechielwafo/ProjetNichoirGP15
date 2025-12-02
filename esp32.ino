#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ===== IDENTIFIANTS WiFi STOCKÉS (À COMPARER) =====
const char* STORED_SSID = "electroProjectWifi";
const char* STORED_PASSWORD = "B1MesureEnv";

// ===== CONFIGURATION RASPBERRY PI =====
const char* serverURL = "http://192.168.2.30:5000/upload";

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
int photoCounter = 0;
unsigned long lastPhotoTime = 0;
const unsigned long PHOTO_INTERVAL = 60000;
bool wifiAuthenticated = false;
bool systemReady = false;

// ===== PAGE HTML DE CONNEXION =====
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
            animation: slideIn 0.5s ease-out;
        }
        @keyframes slideIn {
            from { opacity: 0; transform: translateY(-30px); }
            to { opacity: 1; transform: translateY(0); }
        }
        .logo {
            text-align: center;
            font-size: 64px;
            margin-bottom: 20px;
            animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0%, 100% { transform: scale(1); }
            50% { transform: scale(1.1); }
        }
        h1 {
            color: #333;
            text-align: center;
            margin-bottom: 10px;
            font-size: 28px;
        }
        .subtitle {
            text-align: center;
            color: #666;
            margin-bottom: 30px;
            font-size: 14px;
        }
        .form-group {
            margin-bottom: 25px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            color: #444;
            font-weight: 600;
            font-size: 14px;
        }
        input[type='text'], input[type='password'] {
            width: 100%;
            padding: 14px;
            border: 2px solid #e0e0e0;
            border-radius: 10px;
            font-size: 16px;
            transition: all 0.3s;
            background: #f8f9fa;
        }
        input:focus {
            outline: none;
            border-color: #667eea;
            background: white;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
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
            transition: all 0.3s;
            box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4);
        }
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(102, 126, 234, 0.6);
        }
        button:active {
            transform: translateY(0);
        }
        .alert {
            padding: 12px;
            border-radius: 8px;
            margin-top: 20px;
            font-size: 14px;
            text-align: center;
            display: none;
        }
        .alert.error {
            background: #fee;
            color: #c33;
            border: 1px solid #fcc;
            display: block;
        }
        .alert.success {
            background: #efe;
            color: #3c3;
            border: 1px solid #cfc;
            display: block;
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
        <p class='subtitle'>ESP32-CAM TimerCam - Nichoir Connecté</p>
        
        <form action='/login' method='POST'>
            <div class='form-group'>
                <label for='ssid'>🌐 Nom du réseau WiFi (SSID)</label>
                <input type='text' id='ssid' name='ssid' placeholder='Entrez le SSID' required autocomplete='off'>
            </div>
            
            <div class='form-group'>
                <label for='password'>🔒 Mot de passe WiFi</label>
                <input type='password' id='password' name='password' placeholder='Entrez le mot de passe' required>
            </div>
            
            <button type='submit'>🚀 Se connecter</button>
        </form>
        
        <div class='info-box'>
            💡 Entrez les identifiants WiFi configurés sur l'ESP32
        </div>
        
        <div id='message' class='alert'></div>
    </div>
</body>
</html>
)rawliteral";

// ===== PAGE DE SUCCÈS =====
const char* successPage = R"rawliteral(
<!DOCTYPE html>
<html lang='fr'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
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
            padding: 20px;
        }
        .container {
            background: white;
            padding: 50px;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            max-width: 500px;
            width: 100%;
            text-align: center;
            animation: zoomIn 0.5s ease-out;
        }
        @keyframes zoomIn {
            from { opacity: 0; transform: scale(0.8); }
            to { opacity: 1; transform: scale(1); }
        }
        .icon { 
            font-size: 96px; 
            margin-bottom: 20px;
            animation: bounce 1s infinite;
        }
        @keyframes bounce {
            0%, 100% { transform: translateY(0); }
            50% { transform: translateY(-10px); }
        }
        h1 { color: #11998e; margin-bottom: 15px; font-size: 32px; }
        p { color: #666; margin-bottom: 15px; line-height: 1.8; font-size: 16px; }
        .ip { 
            background: #f0f0f0; 
            padding: 15px; 
            border-radius: 10px; 
            margin: 25px 0;
            font-family: 'Courier New', monospace;
            font-size: 20px;
            color: #333;
            font-weight: bold;
        }
        .info {
            background: #e8f5e9;
            padding: 20px;
            border-radius: 10px;
            margin-top: 25px;
            font-size: 15px;
            color: #2e7d32;
        }
        .status {
            color: #999;
            margin-top: 30px;
            font-size: 13px;
        }
    </style>
</head>
<body>
    <div class='container'>
        <div class='icon'>✅</div>
        <h1>Connexion WiFi réussie !</h1>
        <p>L'ESP32-CAM est maintenant connecté au réseau WiFi</p>
        <div class='ip'>IP: %IP_ADDRESS%</div>
        <div class='info'>
            <strong>📸 Système opérationnel</strong><br><br>
            ✓ Photos envoyées toutes les minutes<br>
            ✓ Destination: 192.168.2.30:5000<br>
            ✓ Caméra TimerCAM active
        </div>
        <p class='status'>Le système démarre dans 3 secondes...</p>
    </div>
    <script>
        setTimeout(function() {
            window.location.href = '/status';
        }, 3000);
    </script>
</body>
</html>
)rawliteral";

// ===== PAGE D'ERREUR =====
const char* errorPage = R"rawliteral(
<!DOCTYPE html>
<html lang='fr'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Erreur d'authentification</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: Arial, sans-serif;
            background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            background: white;
            padding: 50px;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            max-width: 500px;
            width: 100%;
            text-align: center;
        }
        .icon { font-size: 96px; margin-bottom: 20px; }
        h1 { color: #f5576c; margin-bottom: 15px; }
        p { color: #666; margin-bottom: 30px; line-height: 1.8; }
        a {
            display: inline-block;
            padding: 15px 40px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            text-decoration: none;
            border-radius: 10px;
            font-weight: bold;
            transition: transform 0.3s;
        }
        a:hover { transform: translateY(-2px); }
    </style>
</head>
<body>
    <div class='container'>
        <div class='icon'>❌</div>
        <h1>Authentification échouée</h1>
        <p>Les identifiants WiFi sont incorrects.<br>
        Veuillez vérifier le SSID et le mot de passe.</p>
        <a href='/'>🔄 Réessayer</a>
    </div>
</body>
</html>
)rawliteral";

// ===== PAGE DE STATUT =====
const char* statusPage = R"rawliteral(
<!DOCTYPE html>
<html lang='fr'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Statut ESP32-CAM</title>
    <meta http-equiv='refresh' content='60'>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: Arial, sans-serif;
            background: #f5f5f5;
            padding: 20px;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            background: white;
            border-radius: 15px;
            padding: 30px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.1);
        }
        h1 { color: #333; margin-bottom: 30px; text-align: center; }
        .stat {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 15px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .stat-label { font-weight: bold; color: #666; }
        .stat-value { color: #667eea; font-size: 18px; font-weight: bold; }
        .status-ok { color: #38ef7d; }
        .status-error { color: #f5576c; }
    </style>
</head>
<body>
    <div class='container'>
        <h1>📊 Statut du système ESP32-CAM</h1>
        <div class='stat'>
            <span class='stat-label'>🌐 État WiFi</span>
            <span class='stat-value status-ok'>%WIFI_STATUS%</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>📡 Adresse IP</span>
            <span class='stat-value'>%IP_ADDRESS%</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>📸 Photos envoyées</span>
            <span class='stat-value'>%PHOTO_COUNT%</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>⏰ Uptime</span>
            <span class='stat-value'>%UPTIME%</span>
        </div>
        <div class='stat'>
            <span class='stat-label'>🎯 Serveur destination</span>
            <span class='stat-value'>%SERVER_URL%</span>
        </div>
        <p style='text-align: center; margin-top: 30px; color: #999; font-size: 14px;'>
            Actualisation automatique toutes les 60 secondes
        </p>
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
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 2;
        Serial.println("PSRAM détectée - Haute qualité");
    } else {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
        Serial.println("Pas de PSRAM - Qualité standard");
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Erreur caméra: 0x%x\n", err);
        ESP.restart();
    }
    Serial.println("✅ Caméra initialisée");
}

// ===== HANDLERS WEB SERVER =====
void handleRoot() {
    server.send(200, "text/html", loginPage);
}

void handleLogin() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String entered_ssid = server.arg("ssid");
        String entered_password = server.arg("password");
        
        Serial.println("\n=== TENTATIVE D'AUTHENTIFICATION ===");
        Serial.println("SSID entré: " + entered_ssid);
        Serial.println("Password entré: " + String(entered_password.length()) + " caractères");
        
        // COMPARAISON AVEC LES IDENTIFIANTS STOCKÉS
        if (entered_ssid == STORED_SSID && entered_password == STORED_PASSWORD) {
            Serial.println("✅ AUTHENTIFICATION RÉUSSIE !");
            
            // Tentative de connexion WiFi
            WiFi.begin(STORED_SSID, STORED_PASSWORD);
            Serial.print("Connexion WiFi");
            
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
                
                String response = successPage;
                response.replace("%IP_ADDRESS%", WiFi.localIP().toString());
                server.send(200, "text/html", response);
                
                delay(3000);
                systemReady = true;
            } else {
                Serial.println("\n❌ Échec connexion WiFi");
                server.send(200, "text/html", 
                    "<html><body style='text-align:center; padding:50px;'>"
                    "<h1>❌ Erreur de connexion</h1>"
                    "<p>Impossible de se connecter au réseau WiFi</p>"
                    "<a href='/' style='padding:10px 20px; background:#667eea; color:white; text-decoration:none; border-radius:5px;'>Retour</a>"
                    "</body></html>");
            }
        } else {
            Serial.println("❌ AUTHENTIFICATION ÉCHOUÉE - Identifiants incorrects");
            server.send(200, "text/html", errorPage);
        }
    } else {
        server.send(400, "text/plain", "Paramètres manquants");
    }
}

void handleStatus() {
    String response = statusPage;
    response.replace("%WIFI_STATUS%", WiFi.status() == WL_CONNECTED ? "Connecté" : "Déconnecté");
    response.replace("%IP_ADDRESS%", WiFi.localIP().toString());
    response.replace("%PHOTO_COUNT%", String(photoCounter));
    response.replace("%UPTIME%", String(millis() / 1000) + " secondes");
    response.replace("%SERVER_URL%", serverURL);
    server.send(200, "text/html", response);
}

// ===== CAPTURE ET ENVOI PHOTO =====
bool captureAndSendPhoto() {
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi déconnecté");
        return false;
    }

    digitalWrite(2, HIGH);
    camera_fb_t * fb = esp_camera_fb_get();
    if(!fb) {
        Serial.println("❌ Échec capture caméra");
        digitalWrite(2, LOW);
        return false;
    }

    Serial.printf("📸 Image capturée: %d octets\n", fb->len);

    HTTPClient http;
    http.begin(serverURL);
    String filename = "nichoir_" + String(photoCounter) + "_" + String(millis()) + ".jpg";
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("X-Filename", filename);
    http.addHeader("X-Battery-Level", "85");
    http.addHeader("X-Detection-Source", "AUTO");

    int httpResponseCode = http.POST(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    digitalWrite(2, LOW);

    bool success = false;
    if (httpResponseCode > 0) {
        Serial.printf("📡 Code réponse HTTP: %d\n", httpResponseCode);
        success = (httpResponseCode == 200);
    } else {
        Serial.printf("❌ Erreur HTTP: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    return success;
}

int getBatteryLevel() {
    return 85;
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    Serial.println("\n╔═══════════════════════════════════════╗");
    Serial.println("║  ESP32-CAM avec Authentification Web ║");
    Serial.println("╚═══════════════════════════════════════╝");

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);

    config_camera();

    // Démarrer en mode Point d'Accès
    WiFi.softAP("ESP32-CAM-Login", "12345678");
    Serial.println("\n📡 Point d'accès WiFi créé");
    Serial.println("SSID: ESP32-CAM-Login");
    Serial.println("Password: 12345678");
    Serial.print("IP du portail: ");
    Serial.println(WiFi.softAPIP());

    // Configuration du serveur web
    server.on("/", handleRoot);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/status", handleStatus);
    server.begin();

    Serial.println("\n✅ Serveur web démarré");
    Serial.println("Accédez à http://192.168.4.1 pour vous connecter");
    Serial.println("═══════════════════════════════════════");
}

// ===== LOOP =====
void loop() {
    server.handleClient();

    // Si authentifié et WiFi connecté, commencer les captures
    if (systemReady && wifiAuthenticated && WiFi.status() == WL_CONNECTED) {
        unsigned long currentTime = millis();
        
        if (currentTime - lastPhotoTime >= PHOTO_INTERVAL) {
            Serial.println("\n⏰ Capture automatique");
            
            if(captureAndSendPhoto()) {
                photoCounter++;
                Serial.printf("✅ Photo #%d envoyée\n", photoCounter);
            } else {
                Serial.println("❌ Échec envoi photo");
            }
            
            lastPhotoTime = currentTime;
        }
    }

    delay(100);
}