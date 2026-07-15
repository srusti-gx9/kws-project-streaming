# Keyword Spotting (KWS) with Sherpa-ONNX — Streaming C Engine + Browser Frontend

## Demo

The animation below shows the browser frontend streaming microphone audio to
the Sherpa-ONNX keyword spotter and displaying detected keywords in real time.

![Demo](docs/gif/demo.gif)

---

# Clone

```bash
git clone https://github.com/<your-github-username>/kws_project_streaming.git
cd kws_project_streaming
```

---

# Overview

This project implements a real-time Keyword Spotting (KWS) system using the
Sherpa-ONNX C API.

It detects predefined keywords from streaming audio and supports four input
sources.

| Mode | Description |
|------|-------------|
| `--wav` | Process one WAV file |
| `--mic` | Local microphone (ALSA) |
| `--mock` | Stream a WAV file in real time |
| `--stdin` | Receive raw PCM audio from another process |

The browser frontend streams microphone audio through WebSockets into the
Sherpa-ONNX keyword spotter running in C.

---

# Repository Contents

- Streaming keyword spotter (C)
- Browser frontend
- Sample WAV files
- Build system
- Demo GIF
- Screenshots
- Example keyword configuration

---

# Features

- Real-time keyword spotting
- Streaming Zipformer model
- Browser microphone frontend
- WAV testing
- Microphone testing
- Mock streaming mode
- stdin streaming mode
- WebSocket integration
- JSON output for detections

---

# Architecture

```
Browser Mic / WAV / Local Mic
            │
            ▼
      Audio Capture
            │
            ▼
    16 kHz PCM Audio
            │
            ▼
Sherpa-ONNX Keyword Spotter
            │
            ▼
     Keyword JSON Output
            │
            ▼
 Browser UI / Terminal
```

---

# Tested On

- Ubuntu 24.04 LTS
- GCC
- CMake
- Python 3.12
- Sherpa-ONNX C API
- ALSA

---

# System Requirements

- Ubuntu 24.04 LTS
- GCC
- CMake 3.13 or later
- Python 3.9+
- FFmpeg
- ALSA development libraries (`libasound2-dev`)
- Sherpa-ONNX Keyword Spotting model

---

# Install Dependencies

```bash
sudo apt update

sudo apt install \
    build-essential \
    cmake \
    python3 \
    python3-pip \
    python3-venv \
    ffmpeg \
    libasound2-dev
```
---

# Download the Model

Download the Sherpa-ONNX Keyword Spotting model from the official Sherpa-ONNX releases:

https://github.com/k2-fsa/sherpa-onnx/releases

For this project, use:

```text
sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20
```

After downloading, extract the model to a location of your choice, for example:

```text
~/models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/
```

The extracted directory should contain:

```text
encoder-epoch-13-avg-2-chunk-16-left-64.onnx
decoder-epoch-13-avg-2-chunk-16-left-64.onnx
joiner-epoch-13-avg-2-chunk-16-left-64.onnx
tokens.txt
en.phone
test_wavs/
├── keywords_raw.txt
└── keywords.txt
```

Update the model paths in `keyword_spotter.c` to point to your downloaded model directory before building the project.

---

# Processing Flow

```
Audio Source
      │
      ▼
16 kHz PCM
      │
      ▼
Keyword Spotter
      │
      ▼
Keyword Detection
      │
      ├── Terminal
      └── Browser
```

---

# Custom Enhancements

Compared to the original Sherpa-ONNX example, this project adds:

- Browser frontend
- Mock streaming mode
- stdin mode
- WebSocket bridge
- Flask backend
- JSON output
- Shared streaming pipeline

---

# Model

```
sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20
```

Required files

```
encoder-epoch-13-avg-2-chunk-16-left-64.onnx
decoder-epoch-13-avg-2-chunk-16-left-64.onnx
joiner-epoch-13-avg-2-chunk-16-left-64.onnx
tokens.txt
en.phone
keywords_raw.txt
keywords.txt
```

---

# Project Structure

```
kws_project_streaming/
│
├── keyword_spotter.c
├── CMakeLists.txt
├── README.md
├── c-api.h
├── build/
│   └── keyword_spotter
│
├── docs/
│   ├── gif/
│   └── images/
│
├── kws_frontend/
│   ├── server.py
│   ├── requirements.txt
│   └── static/
│       └── index.html
│
├── sample_heyalexa4.wav
├── lightup_sentence.wav
├── sample_input.wav
└── heyalexa_sentence.wav
```

---

# Prerequisites

Before building:

- Install the required packages.
- Download the Sherpa-ONNX model.
- Update model paths in `keyword_spotter.c`.
- Install Python dependencies for the frontend.
- Use Chrome (recommended) for browser testing.

---

# Build

```bash
mkdir build
cd build

cmake ..
make
```

Binary produced:

```
build/keyword_spotter
```

---

# Quick Start

## WAV Mode

```bash
cd build

./keyword_spotter --wav ../sample_heyalexa4.wav
```

---

## Mock Streaming

```bash
./keyword_spotter --mock ../sample_heyalexa4.wav
```

---

## Microphone

```bash
./keyword_spotter --mic
```

---

## stdin

```bash
ffmpeg -i ../sample_heyalexa4.wav \
-f s16le \
-ac 1 \
-ar 16000 - \
| ./keyword_spotter --stdin
```

---

## Browser

```bash
cd ../kws_frontend

python3 -m venv venv

source venv/bin/activate

pip install -r requirements.txt

export KEYWORD_SPOTTER_BIN=$(pwd)/../build/keyword_spotter

python3 server.py
```

Open

```
http://localhost:8000
```

Allow microphone permission.

---

# Sample Audio

| File | Purpose |
|------|---------|
| sample_heyalexa4.wav | HEY ALEXA |
| lightup_sentence.wav | LIGHT UP |
| sample_input.wav | General |
| heyalexa_sentence.wav | Browser |

---

# Configure Model Paths

Inside `keyword_spotter.c`

```c
config.model_config.transducer.encoder = ".../encoder.onnx";
config.model_config.transducer.decoder = ".../decoder.onnx";
config.model_config.transducer.joiner  = ".../joiner.onnx";
config.model_config.tokens = ".../tokens.txt";
config.keywords_file = ".../keywords.txt";
```
-- 
For Example:
config.model_config.transducer.encoder =
"/home/username/models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/encoder-epoch-13-avg-2-chunk-16-left-64.onnx";

config.model_config.transducer.decoder =
"/home/username/models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/decoder-epoch-13-avg-2-chunk-16-left-64.onnx";

config.model_config.transducer.joiner =
"/home/username/models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/joiner-epoch-13-avg-2-chunk-16-left-64.onnx";

config.model_config.tokens =
"/home/username/models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/tokens.txt";

config.keywords_file =
"/home/username/models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/test_wavs/keywords.txt";

--

Rebuild

```bash
cd build
make
```

---

# Run Modes

```bash
./keyword_spotter --wav sample.wav

./keyword_spotter --mock sample.wav

./keyword_spotter --mic

./keyword_spotter --stdin
```

---

# Adding Keywords

Edit

```
keywords_raw.txt
```

Example

```
HEY ALEXA @HEY_ALEXA
LIGHT UP @LIGHT_UP
HELLO WORLD @HELLO_WORLD
```

Generate

```bash
python3 text2token.py \
--text keywords_raw.txt \
--output keywords.txt \
--tokens tokens.txt \
--tokens-type phone+ppinyin \
--lexicon en.phone
```

Rebuild

```bash
cd build
make
```

---

# Browser Frontend

Each browser connection launches its own

```
keyword_spotter --stdin
```

process and streams microphone audio through WebSockets.

Flow

```
Browser
   ↓
WebSocket
   ↓
server.py
   ↓
stdin
   ↓
keyword_spotter
   ↓
JSON
   ↓
Browser
```

---

# Browser Screenshots

### Initial Screen

![Idle](docs/images/browser_idle.png)

### Permission

![Permission](docs/images/browser_mic_permission.png)

### Listening

![Listening](docs/images/browser_listening.png)

### Keyword Detected

![Detected](docs/images/browser_detected.png)

---

# Sample Output

```json
{
  "start_time": 0.00,
  "keyword": "LIGHT_UP",
  "timestamps": [2.92, 2.96, 3.04],
  "tokens": ["L", "AY1", "T", "AH1", "P"]
}
```

---

# Sherpa-ONNX C API Functions

- SherpaOnnxCreateKeywordSpotter()
- SherpaOnnxCreateKeywordStream()
- SherpaOnnxOnlineStreamAcceptWaveform()
- SherpaOnnxIsKeywordStreamReady()
- SherpaOnnxDecodeKeywordStream()
- SherpaOnnxGetKeywordResult()
- SherpaOnnxDestroyKeywordResult()
- SherpaOnnxDestroyOnlineStream()
- SherpaOnnxDestroyKeywordSpotter()

---

# Troubleshooting

### Binary not found

Check

```
KEYWORD_SPOTTER_BIN
```

points to

```
build/keyword_spotter
```

---

### Browser disconnects

- Verify microphone permission.
- Check browser console.
- Ensure `server.py` is running.

---

### Detection quality

Adjust

```c
config.keywords_score
config.keywords_threshold
```

Rebuild afterwards.


---

# References

Sherpa-ONNX

https://github.com/k2-fsa/sherpa-onnx

Documentation

https://k2-fsa.github.io/sherpa/onnx/

---

# Acknowledgements

This project is built on the Sherpa-ONNX C API and extends the original keyword spotting example by adding browser-based streaming, mock streaming mode, stdin support, JSON output, and a WebSocket frontend.
