import os
import json
import base64
from flask import Flask, render_template, request, jsonify, redirect, url_for
from sqlalchemy.orm import sessionmaker
from models import DataImage, DataBattery, create_engine
from datetime import datetime
import paho.mqtt.client as mqtt
import threading

# ===========================
# CONFIGURATION
# ===========================
UPLOAD_FOLDER = '/home/keren/webServer/static/photos'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

MQTT_BROKER = "192.168.0.3"
MQTT_PORT = 1883
MQTT_TOPIC_PHOTO = "nichoir/photo"

# ===========================
# CLASSE PRINCIPALE
# ===========================
class SiteWebLocal:
    def __init__(self):
        self.app = Flask(__name__)
        self.app.secret_key = os.urandom(24)

        # Base smartCities
        engine = create_engine("mariadb+mariadbconnector://kouatche:0123456789@192.168.0.3:3306/smartCities")
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
        self.app.add_url_rule('/mes_images', 'mes_images', self.a_propos)
        self.app.add_url_rule('/contact', 'contact', self.contact, methods=['GET', 'POST'])
        self.app.add_url_rule('/delete/<int:id>', 'delete_image', self.delete_image, methods=['POST'])

        # Routes API
        self.app.add_url_rule('/photos', 'list_photos', self.list_photos)
        self.app.add_url_rule('/battery', 'get_battery', self.get_battery)
        self.app.add_url_rule('/battery/history', 'battery_history', self.battery_history)
        self.app.add_url_rule('/test', 'test', self.test)
        self.app.add_url_rule('/stats', 'stats', self.get_stats)

    # ------------------------------
    # MQTT CALLBACKS
    # ------------------------------
    def on_mqtt_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print("✅ Connecté au broker MQTT")
            client.subscribe(MQTT_TOPIC_PHOTO)
            client.subscribe(f"{MQTT_TOPIC_PHOTO}/chunk/#")
            print(f"📡 Abonné à {MQTT_TOPIC_PHOTO}")
        else:
            print(f"❌ Échec connexion MQTT, code: {rc}")

    def on_mqtt_message(self, client, userdata, msg):
        try:
            topic = msg.topic
            print(f"\n📩 Message reçu sur {topic}")

            if "/chunk/" in topic:
                self.handle_chunked_message(topic, msg.payload)
            else:
                self.process_photo_message(msg.payload)

        except Exception as e:
            print(f"❌ Erreur traitement message MQTT: {e}")
            import traceback
            traceback.print_exc()

    # ------------------------------
    # TRAITEMENT PHOTOS AVEC BATTERIE
    # ------------------------------
    def handle_chunked_message(self, topic, payload):
        """Gère les messages fragmentés"""
        parts = topic.split('/')
        chunk_index = int(parts[-2])
        total_chunks = int(parts[-1])

        image_id = f"temp_{total_chunks}"

        if image_id not in self.image_chunks:
            self.image_chunks[image_id] = {
                'chunks': {},
                'total': total_chunks
            }

        self.image_chunks[image_id]['chunks'][chunk_index] = payload.decode('utf-8')
        print(f"📦 Morceau {chunk_index + 1}/{total_chunks} reçu")

        if len(self.image_chunks[image_id]['chunks']) == total_chunks:
            print("✅ Tous les morceaux reçus, reconstruction...")

            complete_message = ''
            for i in range(total_chunks):
                complete_message += self.image_chunks[image_id]['chunks'][i]

            self.process_photo_message(complete_message.encode('utf-8'))
            del self.image_chunks[image_id]

    def process_photo_message(self, payload):
        """Traite un message photo complet avec création de l'entrée batterie associée"""
        session = None
        try:
            data = json.loads(payload.decode('utf-8'))

            filename = data.get('filename', f'photo_{datetime.now().strftime("%Y%m%d_%H%M%S")}.jpg')
            image_base64 = data.get('image', '')
            timestamp_ms = data.get('timestamp', '')
            battery_voltage = data.get('battery_voltage', 0.0)
            battery_percentage = data.get('battery_percent', 0)  # Note: 'battery_percent' pas 'battery_percentage'
            trigger_type = data.get('trigger_type', 'UNKNOWN')  # ✅ trigger_type au lieu de trigger
            boot_count = data.get('boot_count', 0)
            pir_count = data.get('pir_count', 0)

            print(f"📸 Traitement de {filename}")
            print(f"🔋 Batterie: {battery_voltage}V ({battery_percentage}%)")
            print(f"🎯 Trigger: {trigger_type}")
            print(f"🔢 Boot: {boot_count} | PIR: {pir_count}")

            # Décoder l'image Base64
            image_data = base64.b64decode(image_base64)

            # Sauvegarder sur le disque
            filepath = os.path.join(UPLOAD_FOLDER, filename)
            with open(filepath, 'wb') as f:
                f.write(image_data)

            print(f"💾 Image sauvegardée: {filepath} ({len(image_data)} octets)")

            # ===== TRANSACTION BASE DE DONNÉES =====
            session = self.Session()

            # 1️⃣ CRÉER L'ENTRÉE BATTERIE
            nouvelle_batterie = DataBattery(
                batteryLevel=f"{battery_percentage}%",  # Format varchar
                voltage=battery_voltage,
                percentage=battery_percentage,
                device_id=f"ESP32CAM-{boot_count}",
                submissionDate=datetime.now()
            )
            session.add(nouvelle_batterie)
            session.flush()  # Pour obtenir l'ID sans commit
            battery_id = nouvelle_batterie.id
            print(f"✅ Batterie #{battery_id} créée ({battery_voltage}V, {battery_percentage}%)")

            # 2️⃣ CRÉER L'ENTRÉE IMAGE AVEC LA CLÉ ÉTRANGÈRE
            nouvelle_image = DataImage(
                imageName=filename,
                url=f"/static/photos/{filename}",
                submissionDate=datetime.now(),
                id_battery=battery_id,  # ⭐ RELATION AVEC BATTERIE
                trigger_type=trigger_type,  # ✅ trigger_type au lieu de trigger
                boot_count=boot_count,
                pir_count=pir_count
            )
            session.add(nouvelle_image)
            session.commit()
            image_id = nouvelle_image.id

            print(f"✅ Image #{image_id} enregistrée (liée à batterie #{battery_id})")
            print("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n")

        except json.JSONDecodeError as e:
            print(f"❌ Erreur JSON: {e}")
            if session:
                session.rollback()
        except base64.binascii.Error as e:
            print(f"❌ Erreur décodage Base64: {e}")
            if session:
                session.rollback()
        except Exception as e:
            print(f"❌ Erreur traitement photo: {e}")
            if session:
                session.rollback()
            import traceback
            traceback.print_exc()
        finally:
            if session:
                session.close()

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
        # Jointure pour récupérer l'image avec sa batterie
        image = session.query(DataImage).order_by(DataImage.submissionDate.desc()).first()

        # Récupérer les données de batterie associées si elles existent
        if image and image.id_battery:
            battery = session.query(DataBattery).filter_by(id=image.id_battery).first()
            if battery:
                # Créer un objet simple pour éviter DetachedInstanceError
                image.battery_data = {
                    'id': battery.id,
                    'voltage': battery.voltage,
                    'percentage': battery.percentage,
                    'level': battery.batteryLevel,
                    'timestamp': battery.submissionDate
                }
            else:
                image.battery_data = None
        elif image:
            image.battery_data = None

        session.close()
        return render_template('acceuil.html', titre="Accueil", image=image)

    def a_propos(self):
        session = self.Session()
        images = session.query(DataImage).order_by(DataImage.submissionDate.desc()).limit(20).all()

        # Enrichir chaque image avec ses données de batterie AVANT de fermer la session
        for img in images:
            if img.id_battery:
                battery = session.query(DataBattery).filter_by(id=img.id_battery).first()
                if battery:
                    # Créer un objet simple (pas SQLAlchemy) pour éviter DetachedInstanceError
                    img.battery_data = {
                        'id': battery.id,
                        'voltage': battery.voltage,
                        'percentage': battery.percentage,
                        'level': battery.batteryLevel,
                        'timestamp': battery.submissionDate
                    }
                else:
                    img.battery_data = None
            else:
                img.battery_data = None

        session.close()
        return render_template('mes_images.html', titre="Mes images", images=images)

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

            # Supprimer l'image (la batterie reste pour historique)
            session.delete(img)
            session.commit()
            print(f"✅ Image #{id} supprimée de la BD")

        session.close()
        return redirect(url_for('mes_images'))

    # ------------------------------
    # ROUTES API
    # ------------------------------
    def list_photos(self):
        """Liste des photos avec leurs données de batterie"""
        session = self.Session()
        images = session.query(DataImage).order_by(DataImage.submissionDate.desc()).limit(50).all()

        photos = []
        for img in images:
            photo_data = {
                "id": img.id,
                "imageName": img.imageName,
                "url": img.url,
                "submissionDate": str(img.submissionDate),
                "trigger_type": getattr(img, 'trigger_type', None),  # ✅ trigger_type
                "boot_count": getattr(img, 'boot_count', None),
                "pir_count": getattr(img, 'pir_count', None)
            }

            # Ajouter les données de batterie si disponibles
            if img.id_battery:
                battery = session.query(DataBattery).filter_by(id=img.id_battery).first()
                if battery:
                    photo_data["battery"] = {
                        "id": battery.id,
                        "voltage": battery.voltage,
                        "percentage": battery.percentage,
                        "level": battery.batteryLevel
                    }

            photos.append(photo_data)

        session.close()
        return jsonify({"photos": photos})

    def get_battery(self):
        """Récupère les dernières données de batterie"""
        try:
            session = self.Session()

            # Dernière lecture
            last_battery = session.query(DataBattery).order_by(DataBattery.submissionDate.desc()).first()

            result = {
                "current": {
                    "id": last_battery.id if last_battery else None,
                    "voltage": last_battery.voltage if last_battery else 0.0,
                    "percentage": last_battery.percentage if last_battery else 0,
                    "level": last_battery.batteryLevel if last_battery else None,
                    "timestamp": str(last_battery.submissionDate) if last_battery else None,
                    "device_id": last_battery.device_id if last_battery else None
                }
            }

            session.close()
            return jsonify(result)
        except Exception as e:
            print(f"❌ Erreur récupération batterie: {e}")
            return jsonify({"error": str(e)}), 500

    def battery_history(self):
        """Historique complet des mesures de batterie"""
        try:
            session = self.Session()
            history = session.query(DataBattery).order_by(DataBattery.submissionDate.desc()).limit(100).all()

            result = [{
                "id": b.id,
                "voltage": b.voltage,
                "percentage": b.percentage,
                "level": b.batteryLevel,
                "timestamp": str(b.submissionDate),
                "device_id": b.device_id
            } for b in history]

            session.close()
            return jsonify({"history": result, "count": len(result)})
        except Exception as e:
            print(f"❌ Erreur historique batterie: {e}")
            return jsonify({"error": str(e)}), 500

    def get_stats(self):
        """Statistiques du système"""
        session = self.Session()

        total_photos = session.query(DataImage).count()
        last_photo = session.query(DataImage).order_by(DataImage.submissionDate.desc()).first()
        total_battery_records = session.query(DataBattery).count()
        last_battery = session.query(DataBattery).order_by(DataBattery.submissionDate.desc()).first()

        session.close()

        return jsonify({
            "total_photos": total_photos,
            "last_photo": {
                "id": last_photo.id if last_photo else None,
                "name": last_photo.imageName if last_photo else None,
                "date": str(last_photo.submissionDate) if last_photo else None,
                "battery_id": last_photo.id_battery if last_photo else None
            },
            "total_battery_records": total_battery_records,
            "last_battery": {
                "id": last_battery.id if last_battery else None,
                "voltage": last_battery.voltage if last_battery else 0.0,
                "percentage": last_battery.percentage if last_battery else 0,
                "date": str(last_battery.submissionDate) if last_battery else None
            } if last_battery else None,
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
        print("║  Serveur Web + MQTT (Photo + Battery)   ║")
        print("╚══════════════════════════════════════════╝")
        print(f"🌐 Serveur web: http://0.0.0.0:5000")
        print(f"📡 Broker MQTT: {MQTT_BROKER}:{MQTT_PORT}")
        print(f"📂 Dossier photos: {UPLOAD_FOLDER}")
        print(f"📸 Topic photos: {MQTT_TOPIC_PHOTO}")
        print("══════════════════════════════════════════\n")

        self.app.run(host='0.0.0.0', port=5000, debug=True, use_reloader=False)


if __name__ == "__main__":
    site = SiteWebLocal()
    site.ajouter_imgChat()
    site.lancer()