#!/usr/bin/env python3
"""
server.py — browser-mic-to-KWS bridge.

Flow:
    Chrome (getUserMedia mic)
        -> resampled to 16 kHz mono PCM16 in JS
        -> binary WebSocket frames
        -> this server
        -> stdin of `./keyword_spotter --stdin` (per-connection subprocess)
        -> stdout lines "KEYWORD_JSON:{...}" parsed here
        -> JSON WebSocket messages back to the browser

Run:
    pip install -r requirements.txt
    python3 server.py

Then open http://localhost:8000 in Chrome (see README for SSH tunnel notes
if the keyword_spotter build lives on a remote box).
"""

import json
import os
import subprocess
import threading

from flask import Flask, send_from_directory
from flask_sock import Sock

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Path to the compiled keyword_spotter binary. Override with the
# KEYWORD_SPOTTER_BIN env var if it lives somewhere else.
KEYWORD_SPOTTER_BIN = os.environ.get(
    "KEYWORD_SPOTTER_BIN",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "kws_project", "build", "keyword_spotter"),
)

STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")

app = Flask(__name__, static_folder=None)
sock = Sock(app)


@app.route("/")
def index():
    return send_from_directory(STATIC_DIR, "index.html")


@app.route("/<path:path>")
def static_files(path):
    return send_from_directory(STATIC_DIR, path)


def _reader_thread(proc, ws, stop_event):
    """Reads stdout from the keyword_spotter subprocess line by line and
    forwards any KEYWORD_JSON: lines to the browser as JSON messages."""
    try:
        for raw_line in iter(proc.stdout.readline, b""):
            if stop_event.is_set():
                break
            line = raw_line.decode("utf-8", errors="ignore").strip()
            if not line:
                continue
            if line.startswith("KEYWORD_JSON:"):
                payload = line[len("KEYWORD_JSON:"):]
                try:
                    detected = json.loads(payload)
                except json.JSONDecodeError:
                    detected = {"raw": payload}
                try:
                    ws.send(json.dumps({"type": "detection", "data": detected}))
                except Exception:
                    break
            else:
                # Useful for debugging model/config load issues in the server console.
                print("[keyword_spotter]", line, flush=True)
    except Exception as exc:  # noqa: BLE001
        print("[reader_thread] stopped:", exc, flush=True)


@sock.route("/ws")
def ws_route(ws):
    if not os.path.isfile(KEYWORD_SPOTTER_BIN):
        ws.send(json.dumps({
            "type": "error",
            "message": f"keyword_spotter binary not found at {KEYWORD_SPOTTER_BIN}. "
                       f"Build it first, or set KEYWORD_SPOTTER_BIN env var.",
        }))
        return

    print("[ws] client connected, spawning:", KEYWORD_SPOTTER_BIN, flush=True)

    proc = subprocess.Popen(
        [KEYWORD_SPOTTER_BIN, "--stdin"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        bufsize=0,
    )

    stop_event = threading.Event()
    reader = threading.Thread(target=_reader_thread, args=(proc, ws, stop_event), daemon=True)
    reader.start()

    ws.send(json.dumps({"type": "ready"}))

    try:
        while True:
            message = ws.receive()
            if message is None:
                break  # client closed the socket
            if isinstance(message, (bytes, bytearray)):
                try:
                    proc.stdin.write(message)
                    proc.stdin.flush()
                except (BrokenPipeError, OSError):
                    break
            # ignore any stray text frames from the client
    finally:
        print("[ws] client disconnected, cleaning up subprocess", flush=True)
        stop_event.set()
        try:
            proc.stdin.close()
        except Exception:
            pass
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    print(f"keyword_spotter binary: {KEYWORD_SPOTTER_BIN}")
    print(f"exists: {os.path.isfile(KEYWORD_SPOTTER_BIN)}")
    app.run(host="0.0.0.0", port=8000, debug=False, threaded=True)
