import os
import json
import base64
from flask import Flask, render_template, request, jsonify
from sqlalchemy.orm import sessionmaker
from models import DataImage, create_engine
from datetime import datetime
import paho.mqtt.client as mqtt
import threading

# ===========================
# CONFIGURATION
# ===========================
UPLOAD_FOLDER = '/home/keren/webServer/static/photos'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

MQTT_BROKER = "192.168.2.30"  # ou "192.168.2.30" si broker externe
MQTT_PORT = 1883
MQTT_TOPIC = "nichoir/photo"

# ===========================
# CLASSE PRINCIPALE
# ===========================
class SiteWebLocal:
    def __init__(self):
        self.app = Flask(__name__)
        self.app.secret_key = os.urandom(24)

        # Base smartCities
        engine = create_engine("mariadb+mariadbconnector://kouatche:0123456789@192.168.2.30:3306/smartCities")
        self.Session = sessionmaker(bind=engine)

        # Client MQTT
        self.mqtt_client = mqtt.Client()
        self.mqtt_client.on_connect = self.on_mqtt_connect
        self.mqtt_client.on_message = self.on_mqtt_message

        # Buffer pour images fragmentées
        self.image_chunks = {}

        self.configurer_routes()
        self.demarrer_mqtt()

    def configurer_routes(self):
        # Routes site
        self.app.add_url_rule('/', 'acceuil', self.accueil)
        self.app.add_url_rule('/a-propos', 'a_propos', self.a_propos)
        self.app.add_url_rule('/contact', 'contact', self.contact, methods=['GET', 'POST'])
        self.app.add_url_rule('/delete/<int:id>', 'delete_image', self.delete_image, methods=['POST'])

        # Routes API
        self.app.add_url_rule('/photos', 'list_photos', self.list_photos)
        self.app.add_url_rule('/test', 'test', self.test)
        self.app.add_url_rule('/stats', 'stats', self.get_stats)

    # ------------------------------
    # MQTT CALLBACKS
    # ------------------------------
    def on_mqtt_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print("✅ Connecté au broker MQTT")
            # S'abonner au topic principal et aux morceaux
            client.subscribe(MQTT_TOPIC)
            client.subscribe(f"{MQTT_TOPIC}/chunk/#")
            print(f"📡 Abonné à {MQTT_TOPIC}")
        else:
            print(f"❌ Échec connexion MQTT, code: {rc}")

    def on_mqtt_message(self, client, userdata, msg):
        try:
            topic = msg.topic
            print(f"\n📩 Message reçu sur {topic}")

            # Vérifier si c'est un message fragmenté
            if "/chunk/" in topic:
                self.handle_chunked_message(topic, msg.payload)
            else:
                # Message complet
                self.process_photo_message(msg.payload)

        except Exception as e:
            print(f"❌ Erreur traitement message MQTT: {e}")
            import traceback
            traceback.print_exc()

    def handle_chunked_message(self, topic, payload):
        """Gère les messages fragmentés"""
        parts = topic.split('/')
        chunk_index = int(parts[-2])
        total_chunks = int(parts[-1])

        # Identifier l'image par un ID temporaire
        image_id = f"temp_{total_chunks}"

        if image_id not in self.image_chunks:
            self.image_chunks[image_id] = {
                'chunks': {},
                'total': total_chunks
            }

        # Stocker le morceau
        self.image_chunks[image_id]['chunks'][chunk_index] = payload.decode('utf-8')

        print(f"📦 Morceau {chunk_index + 1}/{total_chunks} reçu")

        # Vérifier si tous les morceaux sont reçus
        if len(self.image_chunks[image_id]['chunks']) == total_chunks:
            print("✅ Tous les morceaux reçus, reconstruction...")

            # Reconstruire le message complet
            complete_message = ''
            for i in range(total_chunks):
                complete_message += self.image_chunks[image_id]['chunks'][i]

            # Traiter le message complet
            self.process_photo_message(complete_message.encode('utf-8'))

            # Nettoyer
            del self.image_chunks[image_id]

    def process_photo_message(self, payload):
        """Traite un message photo complet"""
        try:
            # Parser le JSON
            data = json.loads(payload.decode('utf-8'))

            filename = data.get('filename', f'photo_{datetime.now().strftime("%Y%m%d_%H%M%S")}.jpg')
            image_base64 = data.get('image', '')
            timestamp_ms = data.get('timestamp', '')
            battery = data.get('battery', 0)

            print(f"📸 Traitement de {filename}")
            print(f"🔋 Batterie: {battery}%")

            # Décoder l'image Base64
            image_data = base64.b64decode(image_base64)

            # Sauvegarder sur le disque
            filepath = os.path.join(UPLOAD_FOLDER, filename)
            with open(filepath, 'wb') as f:
                f.write(image_data)

            print(f"💾 Image sauvegardée: {filepath} ({len(image_data)} octets)")

            # Ajouter dans la base de données
            session = self.Session()
            nouvelle_image = DataImage(
                imageName=filename,
                url=f"/static/photos/{filename}",
                submissionDate=datetime.now()
            )
            session.add(nouvelle_image)
            session.commit()
            image_id = nouvelle_image.id
            session.close()

            print(f"✅ Image #{image_id} enregistrée dans la BD")

        except json.JSONDecodeError as e:
            print(f"❌ Erreur JSON: {e}")
        except base64.binascii.Error as e:
            print(f"❌ Erreur décodage Base64: {e}")
        except Exception as e:
            print(f"❌ Erreur traitement photo: {e}")
            import traceback
            traceback.print_exc()

    def demarrer_mqtt(self):
        """Démarre le client MQTT dans un thread séparé"""
        def mqtt_loop():
            try:
                self.mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
                self.mqtt_client.loop_forever()
            except Exception as e:
                print(f"❌ Erreur connexion MQTT: {e}")

        mqtt_thread = threading.Thread(target=mqtt_loop, daemon=True)
        mqtt_thread.start()
        print("🚀 Thread MQTT démarré")

    # ------------------------------
    # ROUTES SITE HTML
    # ------------------------------
    def accueil(self):
        session = self.Session()
        image = session.query(DataImage).order_by(DataImage.submissionDate.desc()).first()
        session.close()
        return render_template('acceuil.html', titre="Accueil", image=image)

    def a_propos(self):
        session = self.Session()
        images = session.query(DataImage).order_by(DataImage.submissionDate.desc()).limit(20).all()
        session.close()
        return render_template('a_propos.html', titre="À Propos", images=images)

    def contact(self):
        if request.method == 'POST':
            print(request.form)
        return render_template('contact.html', titre="Contact")

    def delete_image(self, id):
        session = self.Session()
        img = session.query(DataImage).filter_by(id=id).first()
        if img:
            # Supprimer le fichier physique
            filepath = os.path.join(UPLOAD_FOLDER, img.imageName)
            if os.path.exists(filepath):
                os.remove(filepath)
                print(f"🗑️ Fichier supprimé: {filepath}")

            # Supprimer de la BD
            session.delete(img)
            session.commit()
            print(f"✅ Image #{id} supprimée de la BD")
        session.close()
        return self.accueil()

    # ------------------------------
    # ROUTES API
    # ------------------------------
    def list_photos(self):
        session = self.Session()
        images = session.query(DataImage).order_by(DataImage.submissionDate.desc()).limit(50).all()
        photos = [{
            "id": img.id,
            "imageName": img.imageName,
            "url": img.url,
            "submissionDate": str(img.submissionDate)
        } for img in images]
        session.close()
        return jsonify({"photos": photos})

    def get_stats(self):
        """Statistiques du système"""
        session = self.Session()
        total_photos = session.query(DataImage).count()
        last_photo = session.query(DataImage).order_by(DataImage.submissionDate.desc()).first()
        session.close()

        return jsonify({
            "total_photos": total_photos,
            "last_photo": {
                "name": last_photo.imageName if last_photo else None,
                "date": str(last_photo.submissionDate) if last_photo else None
            },
            "mqtt_connected": self.mqtt_client.is_connected(),
            "broker": f"{MQTT_BROKER}:{MQTT_PORT}"
        })

    def test(self):
        return jsonify({
            "status": "online",
            "mode": "MQTT",
            "upload_folder": UPLOAD_FOLDER,
            "mqtt_broker": f"{MQTT_BROKER}:{MQTT_PORT}",
            "mqtt_connected": self.mqtt_client.is_connected(),
            "timestamp": datetime.now().isoformat()
        })

    def ajouter_imgChat(self):
        """Image de test"""
        session = self.Session()
        existe = session.query(DataImage).filter_by(imageName="imgChat").first()
        if not existe:
            new = DataImage(
                imageName="imgChat",
                url="https://s1.1zoom.me/b5050/338/Cats_Kittens_Two_Glance_486349_1366x768.jpg",
                submissionDate=datetime.now()
            )
            session.add(new)
            session.commit()
            print("✅ Image de test ajoutée")
        session.close()

    def lancer(self):
        print("\n╔══════════════════════════════════════════╗")
        print("║     Serveur Web + MQTT Subscriber       ║")
        print("╚══════════════════════════════════════════╝")
        print(f"🌐 Serveur web: http://0.0.0.0:5000")
        print(f"📡 Broker MQTT: {MQTT_BROKER}:{MQTT_PORT}")
        print(f"📂 Dossier photos: {UPLOAD_FOLDER}")
        print("══════════════════════════════════════════\n")

        self.app.run(host='0.0.0.0', port=5000, debug=True, use_reloader=False)


if __name__ == "__main__":
    site = SiteWebLocal()
    site.ajouter_imgChat()
    site.lancer()