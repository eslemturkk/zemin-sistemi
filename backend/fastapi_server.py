import asyncio
import json
import os
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

from common_serial import SerialBridge

SERIAL_PORT = os.getenv("ARDUINO_PORT", "COM3")
BAUDRATE = int(os.getenv("ARDUINO_BAUD", "9600"))
PUSH_INTERVAL = float(os.getenv("WS_PUSH_INTERVAL", "0.2"))

bridge = SerialBridge(SERIAL_PORT, BAUDRATE)


@asynccontextmanager
async def lifespan(_: FastAPI):
    bridge.start()
    try:
        yield
    finally:
        bridge.stop()


app = FastAPI(title="Smart Floor FastAPI Bridge", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health")
def health():
    return {
        "ok": True,
        "port": SERIAL_PORT,
        "baud": BAUDRATE,
        "serial_connected": bridge.serial_connected,
    }


@app.get("/snapshot")
def snapshot():
    return bridge.snapshot()


@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()

    async def producer():
        while True:
            await ws.send_text(json.dumps(bridge.snapshot()))
            await asyncio.sleep(PUSH_INTERVAL)

    async def consumer():
        while True:
            message = await ws.receive_text()
            cmd = message.strip().upper()
            if cmd in ("GMOD:1", "GMOD:0"):
                bridge.send_command(cmd)

    producer_task = asyncio.create_task(producer())
    consumer_task = asyncio.create_task(consumer())

    done, pending = await asyncio.wait(
        [producer_task, consumer_task], return_when=asyncio.FIRST_EXCEPTION
    )

    for task in pending:
        task.cancel()

    for task in done:
        try:
            task.result()
        except (WebSocketDisconnect, asyncio.CancelledError):
            return
        except Exception:
            return


if __name__ == "__main__":
    import uvicorn

    uvicorn.run("fastapi_server:app", host="0.0.0.0", port=8000, reload=True)
