# Akilli Enerji Ureten Zemin Dashboard

Bu arayuz Arduino Uno tabanli sistemden gelen verileri WebSocket veya WebSerial uzerinden gostermek icin hazirlandi.

## Dosyalar
- index.html: Dashboard yapisi
- styles.css: Koyu ve endustriyel tema
- app.js: Veri akisi, grafik, alarm, GMOD toggle, CSV indirme

## Beklenen Veri Formati
Dashboard asagidaki formatlarin tumunu kabul eder:

1) JSON metni
{"steps": 1250, "voltage": 2.31, "energy_mj": 4480, "alarm": false}

2) Anahtar-deger metni (seri satiri)
steps:1250;voltage:2.31;energy_mj:4480

3) Sadece alarm mesaji
ALARM

## Guvenlik Komutlari
Toggle degisince su komutlardan biri gonderilir:
- GMOD:1
- GMOD:0

Komutlar aktif baglantiya (WebSocket veya WebSerial) yollanir.

## FastAPI WebSocket Ornegi
```python
from fastapi import FastAPI, WebSocket
import asyncio, json

app = FastAPI()

@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()
    steps = 0
    energy = 0.0
    while True:
        # Arduino tarafindan gelen degerleri burada guncelleyin
        steps += 1
        voltage = 2.4
        energy += voltage * 4.8
        payload = {"steps": steps, "voltage": voltage, "energy_mj": energy, "alarm": False}
        await ws.send_text(json.dumps(payload))

        # Dashboard'dan komut geldiyse okuyun
        try:
            cmd = await asyncio.wait_for(ws.receive_text(), timeout=0.01)
            if cmd in ("GMOD:1", "GMOD:0"):
                print("Komut:", cmd)
        except Exception:
            pass

        await asyncio.sleep(0.2)
```

## Flask (flask-sock) WebSocket Ornegi
```python
from flask import Flask
from flask_sock import Sock
import json, time

app = Flask(__name__)
sock = Sock(app)

@sock.route('/ws')
def ws(ws):
    steps = 0
    energy = 0.0
    while True:
        steps += 1
        voltage = 2.2
        energy += voltage * 4.8
        ws.send(json.dumps({"steps": steps, "voltage": voltage, "energy_mj": energy, "alarm": False}))

        cmd = ws.receive(timeout=0.01)
        if cmd in ("GMOD:1", "GMOD:0"):
            print("Komut:", cmd)

        time.sleep(0.2)
```

## Calistirma
1. index.html dosyasini tarayicida acin.
2. WebSocket endpoint'inizi URL kutusuna yazin.
3. WebSocket Baglan butonuna basin.
4. Isteyen tarayicilar icin WebSerial Baglan butonuyla direkt seri baglantiya gecin.

Not: Gercek veri baglantisi yoksa demo veri akisi otomatik devreye girer.

## Backend Dosyalari
- backend/common_serial.py: Arduino seri okuma/yazma katmani
- backend/fastapi_server.py: FastAPI + WebSocket sunucu
- backend/flask_server.py: Flask + flask-sock WebSocket sunucu
- backend/requirements.txt: Python bagimliliklari
- backend/README_BACKEND.md: Kurulum ve calistirma adimlari

Hizli baslangic icin backend klasorundeki README_BACKEND.md dosyasini izleyin.
