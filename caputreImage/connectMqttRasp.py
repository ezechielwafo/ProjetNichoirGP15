import mariadb
import paho.mqtt.client as mqtt
import json
from datetime import datetime

# --- CONFIGURATION DU SYSTÈME (À PERSONNALISER) ---
# ⚠️ Assurez-vous que cet utilisateur (eze1) a les droits GRANT ALL sur 'nichoir_db2'
DB_CONFIG = {
    'host': "localhost",
    'user': "eze1",
    'password': "1234", 
    'database': "nichoir_db2"
}

# Configuration MQTT (Mosquitto est sur le Raspberry Pi)
MQTT_BROKER_HOST = "localhost"
MQTT_BROKER_PORT = 1883
TOPICS_TO_SUBSCRIBE = [
    ("nichoir/status/battery", 0),  
    ("nichoir/photo/metadata", 0)    
]

# --- 1. FONCTIONS D'INSERTION DE DONNÉES ---

def insert_photo_event(data: dict) -> bool:
    """Insère un événement de photo capturée dans la table camera_events."""
    cnx = None
    try:
        cnx = mariadb.connect(**DB_CONFIG) 
        cursor = cnx.cursor()

        sql = """
        INSERT INTO camera_events
        (timestamp_capture, file_name, file_path, battery_level, detection_source)
        VALUES (?, ?, ?, ?, ?)
        """
        val = (
            data.get('timestamp'),
            data.get('file_name'),
            data.get('file_path'),
            data.get('battery_level'),
            data.get('detection_source', 'PIR')
        )

        cursor.execute(sql, val)
        cnx.commit()
        print(f"✅ BDD: Événement photo enregistré: {data.get('file_name')}")
        return True

    except mariadb.Error as err:
        print(f"❌ BDD Erreur d'insertion photo: {err}")
        return False
    finally:
        # Nettoyage robuste pour éviter l'erreur de déconnexion
        if cnx and cnx.connected:
            cursor.close()
            cnx.close()

def insert_status_check(data: dict) -> bool:
    """Insère un relevé quotidien de batterie dans la table system_status."""
    cnx = None
    try:
        cnx = mariadb.connect(**DB_CONFIG) 
        cursor = cnx.cursor()
        
        sql = "INSERT INTO system_status (timestamp_check, battery_level, mode_used) VALUES (?, ?, ?)"
        
        # Logique d'insertion statut (complétée)
        val = (
            data.get('timestamp'),
            data.get('battery_level'),
            data.get('mode_used', 'STANDBY')
        )

        cursor.execute(sql, val)
        cnx.commit()
        print(f"✅ BDD: Statut batterie enregistré: {data.get('battery_level')}%")
        return True

    except mariadb.Error as err:
        print(f"❌ BDD Erreur d'insertion statut: {err}")
        return False
    finally:
        # Nettoyage robuste pour éviter l'erreur de déconnexion
        if cnx and cnx.connected:
            cursor.close()
            cnx.close()


# --- 2. FONCTIONS DE GESTION MQTT ---

def on_connect(client, userdata, flags, rc):
    """Callback appelé lorsque le client reçoit une réponse CONNACK du broker."""
    if rc == 0:
        print(f"🟢 MQTT: Connecté au Broker ({MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}).")
        client.subscribe(TOPICS_TO_SUBSCRIBE)
        print(f"👂 Abonné aux topics: {[t[0] for t in TOPICS_TO_SUBSCRIBE]}")
    else:
        print(f"🔴 MQTT: Échec de la connexion. Code de retour: {rc}")

def on_message(client, userdata, msg):
    """Callback appelé lorsqu'un message PUBLISH est reçu du broker."""

    topic = msg.topic
    try:
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)

        print(f"\n📥 MQTT Reçu sur [{topic}] : {data}")

        if topic == "nichoir/photo/metadata":
            insert_photo_event(data)

        elif topic == "nichoir/status/battery":
            insert_status_check(data)

        else:
            print(f"⚠️ Topic non géré: {topic}")

    except json.JSONDecodeError:
        print(f"❌ Erreur: Payload non JSON reçu sur {topic}: {payload}")
    except Exception as e:
        print(f"❌ Erreur de traitement du message: {e}")

# --- 3. BLOC PRINCIPAL ---

if __name__ == '__main__':

    print("--- Démarrage du Client MQTTSaver (Abonné) ---")

    # Configuration du client MQTT
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        # Tenter la connexion au broker
        client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)

        # Boucle pour maintenir la connexion et l'écoute
        client.loop_forever()

    except ConnectionRefusedError:
        print("\n🔴 ERREUR: Connexion au Broker refusée. Assurez-vous que Mosquitto est démarré (sudo systemctl status mosquitto).")
    except mariadb.Error as e:
        print(f"\n🔴 ERREUR BDD: Impossible de se connecter à la base de données. Vérifiez DB_CONFIG et l'état de MariaDB: {e}")
    except Exception as e:
        print(f"\n❌ Une erreur inattendue s'est produite: {e}")