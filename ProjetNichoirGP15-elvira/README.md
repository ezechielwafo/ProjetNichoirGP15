# ProjetNichoirGP15
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
