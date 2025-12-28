from flask import Flask, request
from datetime import datetime

app = Flask(__name__)

@app.route('/upload', methods=['POST'])
def upload_image():
    image_data = request.data
    filename = "photo_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".jpg"

    with open(filename, "wb") as f:
        f.write(image_data)

    print(f"Image sauvegardée : {filename}")
    return "OK", 200

app.run(host="0.0.0.0", port=5000)
