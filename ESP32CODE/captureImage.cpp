#include "esp_camera.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Définitions pour le module caméra AI-Thinker
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Intervalle de temps pour la capture (en millisecondes)
const long captureInterval = 10000; // 10 secondes
unsigned long previousCaptureTime = 0;

// Configuration de la caméra
static camera_config_t config;

void setupCamera() {
    // Configuration de base du GPIO
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
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_siod = SIOD_GPIO_NUM;
    config.pin_sioc = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000; // 20 MHz

    // Paramètres d'image pour économiser de la mémoire (QVGA: 320x240)
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA; 
    config.jpeg_quality = 10; // Meilleure qualité
    config.fb_count = 1;

    // Initialisation de la caméra
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("❌ Erreur d'initialisation de la caméra: 0x%x\n", err);
        return;
    }
    Serial.println("✅ Caméra initialisée avec succès.");

    // Configurer la résolution pour améliorer la stabilité après l'initialisation
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_QVGA); 
}

void captureAndReport() {
    camera_fb_t *fb = NULL;

    // Capture de l'image
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("❌ Échec de la capture de l'image!");
        return;
    }

    // --- ENVOI DU RETOUR D'INFORMATION ---
    Serial.printf("📸 Image capturée ! Taille: %u octets. État: OK\n", fb->len);
    // Le tableau d'octets (fb->buf) contient les données JPEG de l'image.

    // Libérer le framebuffer (mémoire)
    esp_camera_fb_return(fb);

    // Mettre à jour le temps de la dernière capture
    previousCaptureTime = millis();
}

void setup() {
    // Désactiver le watchdog timers pour éviter les redémarrages
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 

    Serial.begin(115200);
    delay(2000);
    Serial.println("Démarrage du programme ESP32-CAM...");
    
    setupCamera();
}

void loop() {
    unsigned long currentMillis = millis();

    // Vérifier si l'intervalle de 10 secondes est écoulé
    if (currentMillis - previousCaptureTime >= captureInterval) {
        
        // Exécuter la capture et envoyer le rapport
        captureAndReport();
        
        // Note: previousCaptureTime est mis à jour dans captureAndReport()
    }
}