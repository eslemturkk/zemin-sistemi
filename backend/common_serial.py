import json
import math
import random
import threading
import time
from dataclasses import dataclass, asdict
from datetime import datetime
from queue import Empty, Queue

import serial


@dataclass
class Telemetry:
    ts: str
    steps: int
    voltage: float
    energy_mj: float
    alarm: bool = False
    security_mode: bool = False


class SerialBridge:
    """Reads Arduino serial lines and exposes latest telemetry + command writer."""

    def __init__(self, port: str, baudrate: int = 9600):
        self.port = port
        self.baudrate = baudrate
        self._ser = None
        self._thread = None
        self._running = False
        self._lock = threading.Lock()
        self._commands = Queue()
        self.serial_connected = False
        self._sim_t = 0

        self.latest = Telemetry(
            ts=datetime.now().isoformat(),
            steps=0,
            voltage=0.0,
            energy_mj=0.0,
            alarm=False,
            security_mode=False,
        )

    def start(self):
        if self._running:
            return
        try:
            self._ser = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self.serial_connected = True
        except serial.SerialException:
            self._ser = None
            self.serial_connected = False
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1)
        if self._ser and self._ser.is_open:
            self._ser.close()

    def send_command(self, command: str):
        self._commands.put(command)

    def snapshot(self) -> dict:
        with self._lock:
            return asdict(self.latest)

    def _loop(self):
        while self._running:
            self._flush_commands()

            if not self._ser:
                self._simulate_tick()
                time.sleep(0.2)
                continue

            raw = self._ser.readline().decode("utf-8", errors="ignore").strip()
            if not raw:
                continue

            payload = self._parse_line(raw)
            if payload is None:
                continue

            with self._lock:
                self.latest = Telemetry(**payload)

    def _simulate_tick(self):
        self._sim_t += 1
        base_v = 2.3 + math.sin(self._sim_t / 6.0) * 1.5 + random.random() * 0.25
        voltage = max(0.0, min(5.0, base_v))

        snap = self.snapshot()
        steps = snap["steps"] + (1 if random.random() > 0.42 else 0)
        energy_mj = snap["energy_mj"] + (voltage * 4.8)

        snap_sec = snap.get("security_mode", False)
        with self._lock:
            self.latest = Telemetry(
                ts=datetime.now().isoformat(),
                steps=steps,
                voltage=voltage,
                energy_mj=energy_mj,
                alarm=False,
                security_mode=snap_sec,
            )

    def _flush_commands(self):
        if not self._ser:
            return
        try:
            while True:
                cmd = self._commands.get_nowait().strip()
                if cmd:
                    self._ser.write(f"{cmd}\n".encode("utf-8"))
        except Empty:
            return

    def _parse_line(self, line: str):
        if line.upper() == "ALARM":
            snap = self.snapshot()
            snap["alarm"] = True
            snap["ts"] = datetime.now().isoformat()
            return snap

        # JSON: {"steps":123,"voltage":2.3,"energy_mj":456,"alarm":false}
        if line.startswith("{") and line.endswith("}"):
            try:
                data = json.loads(line)
                return self._normalize_data(data)
            except json.JSONDecodeError:
                return None

        # KV: steps:123;voltage:2.3;energy_mj:456;alarm:0
        kv_data = {}
        for part in line.split(";"):
            if ":" not in part:
                continue
            k, v = part.split(":", 1)
            kv_data[k.strip().lower()] = v.strip()

        if not kv_data:
            return None
        return self._normalize_data(kv_data)

    def _normalize_data(self, data: dict):
        def as_float(value, default=0.0):
            try:
                return float(value)
            except (TypeError, ValueError):
                return default

        def as_int(value, default=0):
            try:
                return int(float(value))
            except (TypeError, ValueError):
                return default

        alarm_val = data.get("alarm", False)
        alarm = str(alarm_val).lower() in ("1", "true", "yes", "on")

        security_val = data.get("security_mode", False)
        security = bool(security_val) if isinstance(security_val, (int, bool)) else str(security_val).lower() in ("1", "true", "yes", "on")

        return {
            "ts": datetime.now().isoformat(),
            "steps": max(0, as_int(data.get("steps", 0))),
            "voltage": max(0.0, min(5.0, as_float(data.get("voltage", 0.0)))),
            "energy_mj": max(0.0, as_float(data.get("energy_mj", data.get("energy", 0.0)))),
            "alarm": alarm,
            "security_mode": security,
        }
