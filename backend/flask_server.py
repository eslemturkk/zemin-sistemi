import json
import os
import time

from flask import Flask, jsonify
from flask_sock import Sock

from common_serial import SerialBridge

SERIAL_PORT = os.getenv("ARDUINO_PORT", "COM3")
BAUDRATE = int(os.getenv("ARDUINO_BAUD", "9600"))
PUSH_INTERVAL = float(os.getenv("WS_PUSH_INTERVAL", "0.2"))

bridge = SerialBridge(SERIAL_PORT, BAUDRATE)
app = Flask(__name__)
sock = Sock(app)


@app.before_request
def _noop_before_request():
    # Bridge is started in main entrypoint to avoid duplicate starts in reload.
    return None


@app.get("/health")
def health():
    return jsonify(
        {
            "ok": True,
            "port": SERIAL_PORT,
            "baud": BAUDRATE,
            "serial_connected": bridge.serial_connected,
        }
    )


@app.get("/snapshot")
def snapshot():
    return jsonify(bridge.snapshot())


@sock.route("/ws")
def ws_route(ws):
    while True:
        ws.send(json.dumps(bridge.snapshot()))

        cmd = ws.receive(timeout=0.001)
        if cmd:
            normalized = cmd.strip().upper()
            if normalized in ("GMOD:1", "GMOD:0"):
                bridge.send_command(normalized)

        time.sleep(PUSH_INTERVAL)


if __name__ == "__main__":
    bridge.start()
    try:
        app.run(host="0.0.0.0", port=8000, debug=True)
    finally:
        bridge.stop()
