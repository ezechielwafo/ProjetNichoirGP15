# 🐦 Nichoir Connecté – SmartCities (2025–2026)
Projet réalisé dans le cadre du cours **Initiation aux Smart Cities**.  
L’objectif est de concevoir un **nichoir intelligent**, autonome en énergie, capable de détecter la présence d’oiseaux, de prendre des photos, de les envoyer via MQTT vers un Raspberry Pi et de les afficher dans une interface Web.

---

## 📌 1. Objectifs du projet
Le système doit permettre :

- Détection de mouvement via un **capteur PIR**
- Réveil automatique de la **TimerCAM (ESP32)** en mode deep-sleep
- Activation d’une **LED IR** pour éclairage nocturne
- Capture d’image + envoi via **MQTT** au Raspberry Pi
- Stockage des images dans une **base MariaDB**
- Affichage à travers une interface **Flask WebApp**
- Envoi quotidien du **niveau de batterie**
- Fonctionnement autonome (> 6 mois) grâce au faible coût énergétique

---

## 📷 2. Architecture du système

### 🟦 Schéma fonctionnel
"" à ajouter"

---

## 🧩 3. Matériel utilisé

### 🔌 Électronique
- TimerCAM M5Stack (ESP32 + caméra)
- Capteur PIR BS612 
- LED IR 
- Carte Raspberry Pi 

### 🛠️ Fabrication
- Nichoir en bois (En fonction des  dimensions du cahier des charges)
- Breakout board pour : PIR, TimerCAM, LED IR

---

## 💾 4. Logiciels / Technologies

### 🟩 ESP32 (Arduino Framework)
- MQTT (PubSubClient)
- IDE Arduino
- Capture image +  via MQTT
- Lecture niveau batterie (ADC)

### 🟦 Raspberry Pi
- **Mosquitto** (broker MQTT)
- **Python 3**
- **MariaDB**


---

## 📡 5. Fonctionnement détaillé

### 🟧 Comportement de la TimerCAM
1. La TimerCAM est en **deep sleep** pour économiser la batterie  
2. Le PIR détecte un mouvement → réveil ESP32  
3. L’ESP32 allume la LED IR (si faible luminosité)  
4. Une image est capturée  
5. L’image est envoyée au Raspberry Pi via MQTT  
6. L’ESP32 retourne en deep sleep  
7. Une fois par jour, l’ESP32 se réveille automatiquement pour envoyer :  
   - niveau de batterie  

---


