#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

#define WIFI_SSID "electroProjectWifi" // <== VOTRE WIFI
#define WIFI_PASSWORD "B1MesureEnv"      // <== VOTRE MOT DE PASSE

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM  26 // Broche SDA (Data) pour I2C caméra
#define SIOC_GPIO_NUM  27 // Broche SCL (Clock) pour I2C caméra

#define Y9_GPIO_NUM  35
#define Y8_GPIO_NUM  34
#define Y7_GPIO_NUM  39
#define Y6_GPIO_NUM  36
#define Y5_GPIO_NUM  21
#define Y4_GPIO_NUM  19
#define Y3_GPIO_NUM  18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM  22

#define LED_GPIO_NUM 33 // LED Flash/Torche

esp_err_t capture_handler(httpd_req_t *req){
 camera_fb_t * fb = NULL;
 esp_err_t res = ESP_OK;

 fb = esp_camera_fb_get();
 if (!fb) {
 Serial.println("Échec de la capture de l'image");
 httpd_resp_send_500(req);
 return ESP_FAIL;
 }
 
 httpd_resp_set_type(req, "image/jpeg");
 httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

 size_t fb_len = fb->len;
 res = httpd_resp_send(req, (const char *)fb->buf, fb_len);

 esp_camera_fb_return(fb);

 return res;
}

httpd_uri_t uri_capture = {
 .uri  = "/capture", 
 .method = HTTP_GET,
 .handler = capture_handler,
 .user_ctx = NULL
};

httpd_handle_t start_webserver(void)
{
 httpd_handle_t server = NULL;
 httpd_config_t config = HTTPD_DEFAULT_CONFIG();

 if (httpd_start(&server, &config) == ESP_OK) {
 httpd_register_uri_handler(server, &uri_capture);
 }
 return server;
}

// --- 5. Configuration de la caméra ---
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
 config.pin_pwdn = PWDN_GPIO_NUM;
 config.pin_reset = RESET_GPIO_NUM;
 
 config.xclk_freq_hz = 20000000;
 config.pixel_format = PIXFORMAT_JPEG; 
 config.frame_size = FRAMESIZE_SVGA; // 800x600 pixels (bon compromis pour le test)
 config.jpeg_quality = 10; // 0-63, plus petit = meilleure qualite, plus gros fichier
 config.fb_count = 1;

 esp_err_t err = esp_camera_init(&config);
 if (err != ESP_OK) {
 Serial.printf("Échec de l'initialisation de la caméra: 0x%x\n", err);
 }
}

void setup() {
 Serial.begin(115200);
 
 config_camera();

 WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
 Serial.print("Connexion au WiFi...");
 while (WiFi.status() != WL_CONNECTED) {
 delay(500);
Serial.print(".");
}
 Serial.println("");
 Serial.print("Connecté. Adresse IP: ");
 Serial.println(WiFi.localIP());

 start_webserver();
 Serial.println("Serveur web démarré. Accédez à l'URL suivante pour la capture:");
 Serial.print("http://");
 Serial.print(WiFi.localIP());
 Serial.println("/capture");
}

void loop() {
 delay(10);
}