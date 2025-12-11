// ===== CONFIGURATION BATTERIE M5Stack TimerCAM =====
const int BATTERY_ADC_PIN = 38;
const int BAT_HOLD_PIN = 33;
const int LED_PIN = 2;

const float ADC_REFERENCE = 3.3;      // Tension de référence ADC
const int ADC_RESOLUTION = 4095;      // Résolution ADC 12 bits
const float VOLTAGE_CALIBRATION = 1.38;  // Correctif pour M5TimerCAM 1S

// ===== FONCTIONS =====
float readBatteryVoltage() {
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

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(BAT_HOLD_PIN, OUTPUT);
    digitalWrite(BAT_HOLD_PIN, HIGH);  // Permet lecture batterie

    pinMode(LED_PIN, OUTPUT);
    analogSetAttenuation(ADC_11db);    // Atténuation 0-3.6V
}

void loop() {
    float voltage = readBatteryVoltage();
    int percent = getBatteryPercentage(voltage);
    int adcRaw = analogRead(BATTERY_ADC_PIN);

    Serial.printf("ADC Raw: %d  |  Voltage: %.2f V  |  %d%%\n", adcRaw, voltage, percent);

    // Clignotement LED selon batterie
    if(percent >= 60) digitalWrite(LED_PIN, HIGH);
    else if(percent >= 30) digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    else if(percent >= 10) digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    else digitalWrite(LED_PIN, HIGH); // Critique

    delay(1000);
}
