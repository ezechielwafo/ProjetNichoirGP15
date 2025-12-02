#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h> // Librairie pour envoyer des requêtes POST

// --- 1. CONFIGURATION DU RÉSEAU ET SERVEUR ---
const char *ssid = "electroProjectWifi";
const char *password = "B1MesureEnv";

// ⚠️ IP DU RASPBERRY PI (SERVEUR DE RÉCEPTION FLASK)
const char* SERVER_IP = "192.168.2.9"; 
const int SERVER_PORT = 80; // Port standard pour Flask (défini sur 80 dans votre configuration)
const char* UPLOAD_PATH = "/upload";

// Durée du délai entre chaque capture (5 secondes)
const unsigned long CAPTURE_INTERVAL_MS = 5000;
unsigned long lastCaptureTime = 0; // Minuteur

// --- 2. DÉFINITIONS DES BROCHES M5STACK (CORRIGÉES) ---
#define PWDN_GPIO_NUM -1 
#define RESET_GPIO_NUM 15 
#define XCLK_GPIO_NUM 27 
#define SIOD_GPIO_NUM 25 
#define SIOC_GPIO_NUM 23  
#define Y9_GPIO_NUM 35 
#define Y8_GPIO_NUM 34 
#define Y7_GPIO_NUM 39 
#define Y6_GPIO_NUM 36 
#define Y5_GPIO_NUM 21 
#define Y4_GPIO_NUM 19 
#define Y3_GPIO_NUM 18 
#define Y2_GPIO_NUM 5     
#define VSYNC_GPIO_NUM 22 
#define HREF_GPIO_NUM 26 
#define PCLK_GPIO_NUM 23 
#define LED_GPIO_NUM 33 

// --- 3. FONCTIONS DE GESTION DE L'ENVOI HTTP ---

void sendImageToServer() {
    // 1. Prendre l'image
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Echec de la capture pour l'envoi.");
        return;
    }

    // 2. Préparer les données
    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    
    // Nom de fichier unique basé sur le temps pour le serveur
    char timestamp[20];
    sprintf(timestamp, "%04d%02d%02d_%02d%02d%02d", 
            2025, 11, 21, (millis() / 3600000) % 24, (millis() / 60000) % 60, (millis() / 1000) % 60);

    String fileName = "capture_";
    fileName += String(timestamp);
    fileName += ".jpg";

    // 3. Préparer la requête HTTP POST
    HTTPClient http;
    String serverPath = "http://" + String(SERVER_IP) + ":" + String(SERVER_PORT) + String(UPLOAD_PATH); 
    
    http.begin(serverPath);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    // Corps de la requête (header du fichier)
    String header = "--" + boundary + "\r\n";
    header += "Content-Disposition: form-data; name=\"image\"; filename=\"" + fileName + "\"\r\n";
    header += "Content-Type: image/jpeg\r\n\r\n";
    
    // Fin de la requête (trailer)
    String trailer = "\r\n--" + boundary + "--\r\n";

    // 4. Calculer la taille totale
    int headerSize = header.length();
    int imageSize = fb->len;
    int trailerSize = trailer.length();
    int totalSize = headerSize + imageSize + trailerSize;

    // 5. Envoyer la requête complète
    // Attention : la librairie HTTPClient n'est pas ideale pour les gros fichiers, mais on tente l'envoi séquentiel
    
    // Début de la transaction
    http.begin(serverPath);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    
    // Créer un stream pour l'envoi
    http.sendRequest("POST", totalSize); 
    
    // Envoyer le header
    http.sendRequest("POST", (const uint8_t*)header.c_str(), headerSize);
    
    // Envoyer le corps de l'image (les donnees binaires)
    http.sendRequest("POST", (const uint8_t*)fb->buf, imageSize);
    
    // Envoyer le trailer
    http.sendRequest("POST", (const uint8_t*)trailer.c_str(), trailerSize);

    int httpResponseCode = http.end(); // Finalise la transaction et lit le code de retour

    // 6. Afficher le résultat
    if (httpResponseCode == 200) {
        Serial.printf("✅ HTTP OK. Image envoyee: %s\n", fileName.c_str());
    } else {
        Serial.printf("❌ ECHEC HTTP. Code: %d, Message: %s\n", httpResponseCode, http.getString().c_str());
    }

    // Nettoyage
    http.end();
    esp_camera_fb_return(fb);
}


// --- 4. CONFIGURATION DE LA CAMÉRA (Basée sur votre code stable) ---
void config_camera() {
    // ... (Code config_camera complet, identique à la version stable M5Stack) ...
}

// --- 5. SETUP et LOOP ---

void setup() {
    Serial.begin(115200);
    // ... (Initialisation des broches et de la caméra) ...

    WiFi.begin(ssid, password);
    // ... (Attente connexion Wi-Fi) ...

    Serial.print("Adresse IP Locale: ");
    Serial.println(WiFi.localIP());
    Serial.println("Pret a envoyer les images.");
}

void loop() {
    // Minuteur pour l'envoi périodique
    if (WiFi.status() == WL_CONNECTED && (millis() - lastCaptureTime >= CAPTURE_INTERVAL_MS)) {
        
        sendImageToServer();
        lastCaptureTime = millis();
    }

    // Le délai est géré par la logique if, pas par delay()
    // delay(1); 
}