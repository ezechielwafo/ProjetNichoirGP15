#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "base64.h"
#include "esp_sleep.h"

// ===================== CONFIGURATION WIFI =====================
const char* STORED_SSID = "electroProjectWifi";
const char* STORED_PASSWORD = "B1MesureEnv";

// ===================== CONFIGURATION MQTT =====================
const char* mqtt_server = "192.168.2.30";
const int mqtt_port = 1883;
const char* mqtt_topic = "nichoir/photo";

// ===================== CONFIGURATION PIR & LED =====================
#define PIR_GPIO GPIO_NUM_13
#define PIR_PIN 13
#define LED_PIN 2

// ===================== BATTERIE (TimerCAM) =====================
#define BATTERY_ADC_PIN 38
#define BAT_HOLD_PIN 33      // ⚠️ DOIT RESTER À HIGH
#define ADC_REFERENCE 3.3
#define ADC_RESOLUTION 4095
#define VOLTAGE_CALIBRATION 1.38

// ===================== DEEP SLEEP =====================
#define TIME_TO_SLEEP 60     // secondes

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int photoCounter = 0;
RTC_DATA_ATTR int pirDetectionCount = 0;

esp_sleep_wakeup_cause_t wakeup_reason;

// ===================== CAMERA (TimerCAM) =====================
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

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ===============================================================
// ===================== FONCTIONS BATTERIE ======================
// ===============================================================
float readBatteryVoltage() {
  pinMode(BAT_HOLD_PIN, OUTPUT);
  digitalWrite(BAT_HOLD_PIN, HIGH);   // 🔒 MAINTENU À HIGH

  analogSetAttenuation(ADC_11db);

  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(BATTERY_ADC_PIN);
    delay(5);
  }

  float adc = sum / 20.0;
  return (adc / ADC_RESOLUTION) * ADC_REFERENCE * VOLTAGE_CALIBRATION;
}

// ===============================================================
// ===================== CONFIG CAMERA ===========================
// ===============================================================
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

  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Erreur caméra");
    ESP.restart();
  }
}

// ===============================================================
// ===================== MQTT PHOTO ==============================
// ===============================================================
bool captureAndSendPhotoMQTT(bool pirTrigger) {

  if (!mqttClient.connected()) {
    mqttClient.connect("ESP32CAM");
  }

  digitalWrite(LED_PIN, HIGH);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;

  String encoded = base64::encode(fb->buf, fb->len);
  float voltage = readBatteryVoltage();

  String json =
    "{"
    "\"trigger\":\"" + String(pirTrigger ? "PIR" : "TIMER") + "\","
    "\"boot\":" + String(bootCount) + ","
    "\"pir_count\":" + String(pirDetectionCount) + ","
    "\"battery\":" + String(voltage, 2) + ","
    "\"image\":\"" + encoded + "\""
    "}";

  bool ok = mqttClient.publish(mqtt_topic, json.c_str());

  esp_camera_fb_return(fb);
  digitalWrite(LED_PIN, LOW);

  if (ok) photoCounter++;
  return ok;
}

// ===============================================================
// ===================== WAKEUP REASON ===========================
// ===============================================================
void printWakeupReason() {
  wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("🔴 Réveil PIR");
    pirDetectionCount++;
  } 
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("⏰ Réveil Timer");
  } 
  else {
    Serial.println("🔌 Power ON");
  }
}

// ===============================================================
// ===================== DEEP SLEEP ==============================
// ===============================================================
void goToSleep() {

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * 1000000ULL);

  // 🔥 EXT1 recommandé pour PIR
  esp_sleep_enable_ext1_wakeup(
    1ULL << PIR_GPIO,
    ESP_EXT1_WAKEUP_ANY_HIGH
  );

  Serial.println("💤 Deep sleep...");
  delay(100);
  esp_deep_sleep_start();
}

// ===============================================================
// ===================== SETUP ===================================
// ===============================================================
void setup() {

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(500);

  bootCount++;

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BAT_HOLD_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BAT_HOLD_PIN, HIGH);   // 🔒 OBLIGATOIRE

  printWakeupReason();

  config_camera();

  WiFi.begin(STORED_SSID, STORED_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  mqttClient.setServer(mqtt_server, mqtt_port);

  bool pirTrigger = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1);
  captureAndSendPhotoMQTT(pirTrigger);

  delay(2000);
  goToSleep();
}

// ===============================================================
// ===================== LOOP ====================================
// ===============================================================
void loop() {
  // Jamais atteint (deep sleep)
}
