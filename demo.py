import os
from flask import Flask, render_template, request, jsonify
from sqlalchemy.orm import sessionmaker
from sqlalchemy import func
from models import DataImage, create_engine
from datetime import datetime
import paho.mqtt.publish as publish

# ===========================
# CONFIGURATION
# ===========================
UPLOAD_FOLDER = '/home/keren/webServer/static/photos'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

MQTT_BROKER = "localhost"
MQTT_PORT = 1883

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

        self.configurer_routes()

    def configurer_routes(self):
        # Routes site
        self.app.add_url_rule('/', 'acceuil', self.accueil)
        self.app.add_url_rule('/a-propos', 'a_propos', self.a_propos)
        self.app.add_url_rule('/contact', 'contact', self.contact, methods=['GET', 'POST'])
        self.app.add_url_rule('/delete/<int:id>', 'delete_image', self.delete_image, methods=['POST'])

        # Routes ESP32
        self.app.add_url_rule('/upload', 'upload_image', self.upload_image, methods=['POST'])
        self.app.add_url_rule('/photos', 'list_photos', self.list_photos)
        self.app.add_url_rule('/test', 'test', self.test)

    # ------------------------------
    # ROUTES ESP32
    # ------------------------------
    def upload_image(self):
        if not request.data:
            return jsonify({"error": "Pas de données"}), 400

        # Nom du fichier
        filename = request.headers.get(
            'X-Filename',
            f'photo_{datetime.now().strftime("%Y%m%d_%H%M%S")}.jpg'
        )

        filepath = os.path.join(UPLOAD_FOLDER, filename)

        # Sauvegarde sur le disque
        with open(filepath, 'wb') as f:
            f.write(request.data)

        # Ajout dans la table dataImage
        session = self.Session()
        nouvelle_image = DataImage(
            imageName=filename,
            url=f"/static/photos/{filename}",  # URL relative pour ton site
            submissionDate=datetime.now()
        )
        session.add(nouvelle_image)
        session.commit()
        session.close()

        # MQTT optionnel
        try:
            payload = {
                "filename": filename,
                "timestamp": datetime.now().isoformat()
            }
            publish.single("nichoir/photo/received", payload=str(payload), hostname=MQTT_BROKER, port=MQTT_PORT)
        except Exception as e:
            print(f"MQTT indisponible: {e}")

        return jsonify({"status": "success", "filename": filename})

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

    def test(self):
        return jsonify({
            "status": "online",
            "upload_folder": UPLOAD_FOLDER,
            "timestamp": datetime.now().isoformat()
        })

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
        images = session.query(DataImage).order_by(DataImage.submissionDate.desc()).all()
        session.close()
        return render_template('a_propos.html', titre="À Propos" , images=images)

    def contact(self):
        if request.method == 'POST':
            print(request.form)
        return render_template('contact.html', titre="Contact")

    def delete_image(self, id):
        session = self.Session()
        img = session.query(DataImage).filter_by(id=id).first()
        if img:
            session.delete(img)
            session.commit()
        session.close()
        return self.accueil()

    def ajouter_imgChat(self):
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
        session.close()

    def lancer(self):
        self.app.run(host='0.0.0.0', port=5000, debug=True)


if __name__ == "__main__":
    site = SiteWebLocal()
    site.ajouter_imgChat()
    site.lancer()