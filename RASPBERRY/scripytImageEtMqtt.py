import os
import threading
import time
import json
from datetime import datetime
from flask import Flask, request, jsonify
import werkzeug
import mysql.connector # Utilisé ici pour la compatibilité, mais mariadb est aussi possible
import paho.mqtt.client as mqtt

# =======================================================
# CONFIGURATION GLOBALE
# =======================================================

DB_CONFIG = {
    'host': 'localhost',
    'user': 'eze1',
    'password': '1234',
    'database': 'projet_nichoir'
}

UPLOAD_FOLDER = os.path.expanduser('~/images')
MQTT_BROKER_HOST = "localhost"
MQTT_BROKER_PORT = 1883
TOPICS_TO_SUBSCRIBE = [
    ("nichoir/photo/metadata", 0),
    ("nichoir/status/battery", 0)
]
RASPBERRY_PI_IP = '192.168.2.46' # Adresse d'écoute (0.0.0.0)

# =======================================================
# LOGIQUE DE BASE DE DONNÉES ET D'INSERTION
# =======================================================

def get_db_connection():
    """Tente d'établir une connexion à la base de données."""
    try:
        # NOTE: Si vous utilisez 'mariadb', changez ici à mariadb.connect
        conn = mysql.connector.connect(**DB_CONFIG)
        return conn
    except mysql.connector.Error as err:
        print(f"❌ Erreur de connexion BDD: {err}")
        return None

def insert_event(data):
    """Insère un événement de photo ou de statut dans la BDD."""
    conn = get_db_connection()
    if not conn:
        return

    try:
        cursor = conn.cursor()

        # Logique pour insérer dans la table camera_events (pour les photos)
        sql = ("INSERT INTO camera_events "
               "(timestamp_capture, file_name, file_path, battery_level, detection_source) "
               "VALUES (%s, %s, %s, %s, %s)")

        cursor.execute(sql, (
            data.get('timestamp'),
            data.get('file_name'),
            data.get('file_path'),
            data.get('battery_level'),
            data.get('detection_source', 'BUTTON')
        ))
        conn.commit()
        print(f"✅ BDD OK: Enregistrement créé pour {data.get('file_name')}")

    except Exception as err:
        print(f"❌ Erreur BDD (Insertion): {err}")
        conn.rollback()
    finally:
        if conn and conn.is_connected():
            cursor.close()
            conn.close()

# =======================================================
# LOGIQUE MQTT (DANS UN THREAD SÉPARÉ)
# =======================================================

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"🟢 MQTT: Connecté au Broker ({MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}).")
        client.subscribe(TOPICS_TO_SUBSCRIBE)
        print(f"👂 Abonné aux topics: {[t[0] for t in TOPICS_TO_SUBSCRIBE]}")
    else:
        print(f"🔴 MQTT: Échec de la connexion. Code de retour: {rc}")

def on_message(client, userdata, msg):
    """Reçoit les métadonnées et déclenche l'enregistrement BDD."""
    topic = msg.topic
    try:
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)
        print(f"\n📥 MQTT Reçu sur [{topic}] : {data}")

        # Pour le TP: les métadonnées viennent après le HTTP POST
        if topic == "nichoir/photo/metadata":
            insert_event(data)

        # Si vous ajoutez la logique de statut (non photo)
        elif topic == "nichoir/status/battery":
            # NOTE: adapter insert_event ou créer une fonction insert_status si nécessaire
            print(f"🔋 MQTT Statut batterie reçu: {data.get('battery_level')}%")

    except Exception as e:
        print(f"❌ Erreur MQTT/JSON : {e}")

def mqtt_thread_function():
    """Fonction qui sera exécutée dans un thread pour écouter MQTT."""
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
        client.loop_forever()
    except Exception as e:
        print(f"❌ ERREUR THREAD MQTT: {e}")

# =======================================================
# LOGIQUE FLASK (SERVEUR HTTP POST)
# =======================================================

app = Flask(__name__)
app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER

@app.route('/upload', methods=['POST'])
def upload_file():
    """Reçoit l'image binaire via HTTP POST."""

    # 1. Récupération des Headers (Batterie/Source/Nom pour le BDD/MQTT)
    filename = request.headers.get('X-Filename', f"capture_{int(time.time())}.jpg")
    battery_level = request.headers.get('X-Battery-Level', '0')
    detection_source = request.headers.get('X-Detection-Source', 'BUTTON')

    image_data = request.data

    if not image_data:
        return jsonify({'status': 'error', 'message': 'No image data in request body'}), 400

    # 2. Sauvegarde de l'image
    secure_filename = werkzeug.utils.secure_filename(filename)
    file_path = os.path.join(app.config['UPLOAD_FOLDER'], secure_filename)

    try:
        with open(file_path, 'wb') as f:
            f.write(image_data)

        print(f"✅ UPLOAD OK: Image '{secure_filename}' enregistrée. IP: {request.remote_addr}")

        # ⚠️ NOTE IMPORTANTE: Nous faisons confiance à l'ESP32 pour envoyer le JSON MQTT APRÈS CE 200 OK.
        # Le script MQTT est déjà en cours d'exécution dans l'autre thread.

        # 3. Réponse 200 OK
        return jsonify({'status': 'success', 'filename': secure_filename}), 200

    except Exception as e:
        print(f"❌ UPLOAD CRITICAL ERROR: {e}")
        return jsonify({'status': 'error', 'message': f'File save failed: {e}'}), 500

# =======================================================
# DÉMARRAGE GLOBAL
# =======================================================

if __name__ == '__main__':
    # Préparation du dossier d'images
    os.makedirs(UPLOAD_FOLDER, exist_ok=True)

    print("--- DÉMARRAGE DU SERVEUR FLASK ET DU CLIENT MQTT ---")

    # 1. Lancement du client MQTT dans un thread d'arrière-plan
    mqtt_thread = threading.Thread(target=mqtt_thread_function)
    mqtt_thread.daemon = True # Le thread s'arrête si le programme principal s'arrête
    mqtt_thread.start()

    # 2. Lancement du serveur Flask (dans le thread principal)
    print(f"🚀 Serveur Flask écoutant sur http://{RASPBERRY_PI_IP}:5000/upload")
    app.run(host='0.0.0.0', port=5000, debug=False)