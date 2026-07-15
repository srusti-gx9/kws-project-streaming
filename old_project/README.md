# Standalone Keyword Spotting using Sherpa-ONNX C API

## Overview

This project implements a standalone Keyword Spotting (KWS) application using the Sherpa-ONNX C API.

The application loads a keyword spotting model, reads a WAV audio file, and detects predefined keywords along with their timestamps.

The project is built independently by using only the required Sherpa-ONNX C API files and libraries.

---

# Repository Used

https://github.com/k2-fsa/sherpa-onnx

---

# Model Used

Model:

```
sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20
```

Model Files:

```
encoder-epoch-13-avg-2-chunk-16-left-64.onnx
decoder-epoch-13-avg-2-chunk-16-left-64.onnx
joiner-epoch-13-avg-2-chunk-16-left-64.onnx
tokens.txt
en.phone
test_wavs/keywords_raw.txt
test_wavs/keywords.txt
```

This model supports both English and Chinese keyword spotting.

---

# Project Structure

```
kws_project/
│
├── keyword_spotter.c
├── c-api.h
├── CMakeLists.txt
├── libsherpa-onnx-c-api.so
├── libonnxruntime.so
├── README.md
└── build/
```

---

# Build Instructions

Create the build directory:

```bash
mkdir build
cd build
```

Configure the project:

```bash
cmake ..
```

Compile the application:

```bash
make
```

---

# Run the Application

```bash
cd build
./keyword_spotter
```

---

# Using a Different Audio File

Open the source file:

```bash
vim ~/kws_project/keyword_spotter.c
```

Locate:

```c
const char *wav_filename =
"/home/ubuntu/kws_project/sample.wav";
```

Replace it with your desired WAV file.

Example:

```c
const char *wav_filename =
"/home/ubuntu/kws_project/sample_heyalexa.wav";
```

Save the file and rebuild:

```bash
cd build
make
```

Run again:

```bash
./keyword_spotter
```

---

# Audio Requirements

The input audio must be:

- WAV format
- PCM 16-bit
- Mono channel
- 16 kHz sample rate

To verify the audio format:

```bash
file sample.wav
```

Expected output:

```
RIFF WAVE
16-bit
mono
16000 Hz
```

---

# Adding Custom Keywords

Sherpa-ONNX uses two keyword files:

### 1. keywords_raw.txt

Contains the readable keywords.

Example:

```
LIGHT UP @LIGHT_UP
HEY ALEXA @HEY_ALEXA
HELLO WORLD @HELLO_WORLD
```

### 2. keywords.txt

Contains the phoneme representation generated from `keywords_raw.txt`.

Example:

```
HH EY1 AH0 L EH1 K S AH0 @HEY_ALEXA
HH AH0 L OW1 W ER1 L D @HELLO_WORLD
```

**Do not edit `keywords.txt` manually. It should always be generated using `text2token.py`.**

---

# Edit keywords_raw.txt

Open the file:

```bash
vim ~/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/test_wavs/keywords_raw.txt
```

Go to the end of the file and add your custom keywords.

Example:

```
LIGHT UP @LIGHT_UP
HEY ALEXA @HEY_ALEXA
HELLO WORLD @HELLO_WORLD
```

Save and exit:

```
:wq
```

---

# Generate keywords.txt

Go to the Sherpa-ONNX repository:

```bash
cd ~/sherpa-onnx
```

Set the Python path:

```bash
export PYTHONPATH=$HOME/sherpa-onnx/build/lib.linux-x86_64-cpython-313:$HOME/sherpa-onnx/sherpa-onnx/python
```

Generate `keywords.txt`:

```bash
python3 ~/sherpa-onnx/scripts/text2token.py \
  --text ~/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/test_wavs/keywords_raw.txt \
  --output ~/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/test_wavs/keywords.txt \
  --tokens ~/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/tokens.txt \
  --tokens-type phone+ppinyin \
  --lexicon ~/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/en.phone
```

---

# Verify Generated Keywords

Verify a generated keyword:

```bash
grep "HEY_ALEXA" ~/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/test_wavs/keywords.txt
```

Example output:

```
HH EY1 AH0 L EH1 K S AH0 @HEY_ALEXA
```

Similarly:

```bash
grep "HELLO_WORLD" ~/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/test_wavs/keywords.txt
```

---

# Rebuild After Changes

Whenever you modify:

- `keyword_spotter.c`
- `keywords.txt`
- Model configuration

Rebuild the application:

```bash
cd ~/kws_project/build
make
```

Run the application:

```bash
./keyword_spotter
```

---

# Sample Output

```
Detected keyword:
{
  "start_time": 0.00,
  "keyword": "LIGHT_UP",
  "timestamps": [2.92, 2.96, 3.04, 3.20, 3.28],
  "tokens": ["L", "AY1", "T", "AH1", "P"]
}
```

---

# Key C API Functions Used

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

# Components Used

- keyword_spotter.c
- c-api.h
- CMakeLists.txt
- libsherpa-onnx-c-api.so
- libonnxruntime.so

---

# Work Completed

- Connected to the AWS Ubuntu instance.
- Explored the Sherpa-ONNX repository structure.
- Studied the Sherpa-ONNX Keyword Spotting C API.
- Built a standalone keyword spotting application using the Sherpa-ONNX C API.
- Configured the Zipformer keyword spotting model.
- Successfully compiled the project using CMake.
- Verified keyword detection using the provided sample audio.
- Added support for custom keywords using `keywords_raw.txt`.
- Generated phoneme-based `keywords.txt` using `text2token.py`.
- Tested detection with custom WAV audio files.
- Verified generated phoneme sequences for custom keywords.

---

# Current Status

The standalone keyword spotting application has been successfully built and tested.

The application can:

- Detect predefined keywords.
- Detect user-defined custom keywords.
- Generate `keywords.txt` from `keywords_raw.txt`.
- Process custom WAV audio files.
- Return detected keywords with timestamps.

---

# Troubleshooting

## Verify the Current Audio File

```bash
grep -A2 "wav_filename" ~/kws_project/keyword_spotter.c
```

This displays the WAV file currently configured for keyword detection.

---

## Verify the Current Keywords File

```bash
grep -A2 "keywords_file" ~/kws_project/keyword_spotter.c
```

This displays the `keywords.txt` file currently used by the application.

---

## Rebuild the Project

If any changes are made to:

- `keyword_spotter.c`
- `keywords.txt`
- Model configuration

Rebuild:

```bash
cd ~/kws_project/build
make
```

Run:

```bash
./keyword_spotter
```

---

# References

Sherpa-ONNX Repository

https://github.com/k2-fsa/sherpa-onnx

Sherpa-ONNX Documentation

https://k2-fsa.github.io/sherpa/onnx/
