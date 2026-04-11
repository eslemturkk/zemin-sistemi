# Backend Kurulum Rehberi

Bu klasor iki secenek sunar:
- FastAPI WebSocket sunucu
- Flask + flask-sock WebSocket sunucu

Her iki secenek de Arduino'dan pyserial ile veri okuyup dashboard'a gonderir.

## 1) Kurulum

```bash
cd backend
pip install -r requirements.txt
```

## 2) Ortam Degiskenleri

Windows PowerShell:

```powershell
$env:ARDUINO_PORT="COM3"
$env:ARDUINO_BAUD="9600"
$env:WS_PUSH_INTERVAL="0.2"
```

## 3) FastAPI Calistirma

```bash
cd backend
uvicorn fastapi_server:app --host 0.0.0.0 --port 8000 --reload
```

Dashboard URL kutusuna su endpoint yazilmalidir:
- ws://127.0.0.1:8000/ws

## 4) Flask Calistirma

```bash
cd backend
python flask_server.py
```

Yine ayni endpoint kullanilir:
- ws://127.0.0.1:8000/ws

## 5) Arduino Seri Satir Formati

Aşağıdaki formatlardan biri gonderilebilir:

JSON:
{"steps":123,"voltage":2.31,"energy_mj":4480,"alarm":false}

KV:
steps:123;voltage:2.31;energy_mj:4480

Alarm:
ALARM

## 6) Dashboard'dan Arduino'ya Komut

Security toggle degisince backend seri hatta su komutlardan birini yazar:
- GMOD:1
- GMOD:0
