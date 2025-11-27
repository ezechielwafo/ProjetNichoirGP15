#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ===== CONFIGURATION WiFi =====
const char* ssid = "electroProjectWifi";           // Remplace par ton SSID WiFi
const char* password = "B1MesureEnv";    // Remplace par ton mot de passe WiFi

// ===== CONFIGURATION RASPBERRY PI =====
const char* serverURL = "http://192.168.2.30:5000/upload";  // URL du serveur Flask sur le Raspberry

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
int photoCounter = 0;
unsigned long lastPhotoTime = 0;
const unsigned long PHOTO_INTERVAL = 60000;  // Intervalle entre photos (60000 ms = 1 minute)

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Démarrage M5Stack TimerCAM ---");

  // Désactiver le brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // LED interne (optionnelle - GPIO 2 pour TimerCAM)
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);

  // Configuration de la caméra
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Qualité de l'image (selon la RAM disponible)
  if(psramFound()){
    config.frame_size = FRAMESIZE_SVGA;   // 800x600 pour TimerCAM
    config.jpeg_quality = 12;              // 0-63 (plus bas = meilleure qualité)
    config.fb_count = 2;
    Serial.println("PSRAM détectée - Haute qualité");
  } else {
    config.frame_size = FRAMESIZE_VGA;    // 640x480
    config.jpeg_quality = 15;
    config.fb_count = 1;
    Serial.println("Pas de PSRAM - Qualité standard");
  }

  // Initialisation de la caméra
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Erreur caméra: 0x%x\n", err);
    ESP.restart();
  }
  Serial.println("✅ Caméra initialisée");

  // Connexion WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connexion WiFi");
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connecté");
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ Échec connexion WiFi");
    ESP.restart();
  }

  Serial.println("--- Système prêt - Capture automatique toutes les minutes ---");
  
  // Prendre une première photo au démarrage
  delay(2000);  // Attendre 2 secondes pour stabiliser la caméra
  Serial.println("\n📸 Capture de la première photo...");
  captureAndSendPhoto();
  lastPhotoTime = millis();
}

void loop() {
  unsigned long currentTime = millis();

  // Vérifier si 1 minute s'est écoulée depuis la dernière photo
  if (currentTime - lastPhotoTime >= PHOTO_INTERVAL) {
    Serial.println("\n⏰ 1 minute écoulée - Capture automatique");
    
    // Capture et envoi de la photo
    if(captureAndSendPhoto()) {
      photoCounter++;
      Serial.printf("✅ Photo #%d envoyée avec succès\n", photoCounter);
      Serial.printf("⏳ Prochaine photo dans 1 minute...\n");
    } else {
      Serial.println("❌ Échec de l'envoi de la photo - Nouvelle tentative dans 1 minute");
    }
    
    lastPhotoTime = currentTime;
  }

  delay(1000);  // Vérifier toutes les secondes
}

bool captureAndSendPhoto() {
  // Vérification de la connexion WiFi
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi déconnecté");
    return false;
  }

  // Allumer la LED pendant la capture (optionnel)
  digitalWrite(2, HIGH);
  
  // Capture de l'image
  camera_fb_t * fb = esp_camera_fb_get();
  if(!fb) {
    Serial.println("❌ Échec capture caméra");
    digitalWrite(2, LOW);
    return false;
  }

  Serial.printf("📸 Image capturée: %d octets\n", fb->len);

  // Préparation de la requête HTTP POST
  HTTPClient http;
  http.begin(serverURL);
  
  // Génération du nom de fichier
  String filename = "nichoir_" + String(photoCounter) + "_" + String(millis()) + ".jpg";
  
  // Headers HTTP
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("X-Filename", filename);
  http.addHeader("X-Battery-Level", String(getBatteryLevel()));  // Optionnel
  http.addHeader("X-Detection-Source", "AUTO");  // Mode automatique

  // Envoi de l'image
  int httpResponseCode = http.POST(fb->buf, fb->len);

  // Libération de la mémoire
  esp_camera_fb_return(fb);
  
  // Éteindre la LED
  digitalWrite(2, LOW);

  // Vérification de la réponse
  bool success = false;
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("📡 Code réponse HTTP: %d\n", httpResponseCode);
    Serial.println("Réponse serveur: " + response);
    success = (httpResponseCode == 200);
  } else {
    Serial.printf("❌ Erreur HTTP: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
  return success;
}

// Fonction pour lire le niveau de batterie (optionnel)
// À adapter selon ton circuit (diviseur de tension sur ADC)
int getBatteryLevel() {
  // Exemple simple - à adapter selon ton montage
  // int adcValue = analogRead(34);  // Pin ADC pour batterie
  // return map(adcValue, 0, 4095, 0, 100);
  return 85;  // Valeur fictive pour test
}