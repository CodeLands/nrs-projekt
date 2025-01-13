from flask import Flask, request

app = Flask(__name__)

# Dodaj GET endpoint za testiranje
@app.route('/', methods=['GET'])
def test_connection():
    return "Povezava deluje!", 200

# Dodaj POST endpoint za sprejemanje podatkov
@app.route('/data', methods=['POST'])
def receive_data():
    data = request.json  # Prejmi podatke v JSON obliki
    print("Prejeti podatki:", data)
    return "Podatki prejeti!", 200


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)  # strežnik posluša na vseh omrežnih vmesnikih

