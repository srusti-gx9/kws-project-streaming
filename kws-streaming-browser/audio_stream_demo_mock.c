/*
 * main.c — demo driver for the audio streaming layer.
 *
 * Stands in for the future Sherpa-ONNX keyword-spotting consumer: it starts the
 * stream, receives PCM chunks via the callback, computes a quick RMS level so
 * you can see the mic is live, and prints ring-buffer metrics once per second.
 *
 * Ctrl-C (SIGINT) stops cleanly.
 *
 * Usage:
 *   ./audio_stream_demo [device] [chunk_samples] [ring_chunks] [mmap|rw]
 *   e.g. ./audio_stream_demo default 160 256 rw
 *        ./audio_stream_demo plughw:0,0 160 256 mmap
 */
#define _POSIX_C_SOURCE 200809L

#include "audio_stream.h"
#include "c-api.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>

/*---------------------------------------------------------
 * Sherpa-ONNX Model Directory
 *
 * All model files are expected to be inside:
 *
 * models/
 *   sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/
 *
 * Using a relative path makes the application portable
 * across different Linux systems.
 *--------------------------------------------------------*/
#define MODEL_DIR "models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/"
static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

/* Demo consumer state passed through user_data. */
typedef struct {
    _Atomic unsigned long callback_count;
    _Atomic long          last_rms;
} DemoState;

/* Sherpa Keyword Spotter State */
typedef struct {
    DemoState demo;

    const SherpaOnnxKeywordSpotter *kws;
    const SherpaOnnxOnlineStream *stream;

    float pcm_float[160];
} KwsState;

/*
 * This is the integration seam. The future sherpa-onnx consumer replaces the
 * body with an int16->float conversion + SherpaOnnxOnlineStreamAcceptWaveform().
 * See README "Future integration with Sherpa-ONNX".
 */
static void audio_chunk_cb(const int16_t *samples, int num_samples, void *user_data) {
    KwsState  *st = (KwsState  *)user_data;

    double sum_sq = 0.0;
    for (int i = 0; i < num_samples; ++i) {
        double v = (double)samples[i];
        sum_sq += v * v;
    }
    long rms = (long)sqrt(sum_sq / (num_samples > 0 ? num_samples : 1));

    atomic_fetch_add_explicit(&st->demo.callback_count, 1, memory_order_relaxed);
    atomic_store_explicit(&st->demo.last_rms, rms, memory_order_relaxed);

/* Convert int16 PCM to float */
for (int i = 0; i < num_samples; i++) {
    st->pcm_float[i] = samples[i] / 32768.0f;
}

/* Feed audio to Sherpa */
SherpaOnnxOnlineStreamAcceptWaveform(
    st->stream,
    16000,
    st->pcm_float,
    num_samples);

/* Decode */
if (SherpaOnnxIsKeywordStreamReady(st->kws, st->stream)) {
    SherpaOnnxDecodeKeywordStream(
        st->kws,
        st->stream);
}

/* Check if a keyword was detected */
const SherpaOnnxKeywordResult *result =
    SherpaOnnxGetKeywordResult(
        st->kws,
        st->stream);

if (result && result->keyword[0] != '\0') {
    printf("\n=========================================\n");
    printf("Keyword Detected: %s\n", result->keyword);
    printf("=========================================\n\n");

    SherpaOnnxResetKeywordStream(
    st->kws,
    st->stream);
 }

if (result) {
    SherpaOnnxDestroyKeywordResult(result);
  }

}

int main(int argc, char **argv) {
    AudioStreamConfig cfg;
    audio_stream_default_config(&cfg);

    if (argc > 1) cfg.device        = argv[1];
    if (argc > 2) cfg.chunk_samples = (unsigned int)strtoul(argv[2], NULL, 10);
    if (argc > 3) cfg.ring_chunks   = (size_t)strtoul(argv[3], NULL, 10);
    if (argc > 4) cfg.use_mmap      = (strcmp(argv[4], "mmap") == 0);

    printf("Audio streaming layer demo\n");
    printf("  device=%s rate=%u Hz channels=%u chunk=%u samples (%.1f ms) ring=%zu chunks mode=%s\n",
           cfg.device, cfg.sample_rate, cfg.channels, cfg.chunk_samples,
           1000.0 * cfg.chunk_samples / cfg.sample_rate, cfg.ring_chunks,
           cfg.use_mmap ? "MMAP(requested)" : "RW");

/*---------------------------------------------------------
 * Configure Sherpa-ONNX Keyword Spotter
 *
 * Model files are loaded from MODEL_DIR.
 * Since MODEL_DIR is a relative path, anyone cloning the
 * repository can build and run the project without
 * modifying the source code.
 *--------------------------------------------------------*/
SherpaOnnxKeywordSpotterConfig config;

memset(&config, 0, sizeof(config));

config.model_config.transducer.encoder =
    MODEL_DIR "encoder-epoch-13-avg-2-chunk-16-left-64.onnx";

config.model_config.transducer.decoder =
    MODEL_DIR "decoder-epoch-13-avg-2-chunk-16-left-64.onnx";

config.model_config.transducer.joiner =
    MODEL_DIR "joiner-epoch-13-avg-2-chunk-16-left-64.onnx";

config.model_config.tokens =
    MODEL_DIR "tokens.txt";

config.model_config.provider = "cpu";
config.model_config.num_threads = 1;
config.model_config.debug = 1;

/* Optional tuning parameters */
// config.keywords_score = 1.0;
// config.keywords_threshold = 0.1;

config.keywords_file =
    MODEL_DIR "test_wavs/keywords.txt";

const SherpaOnnxKeywordSpotter *kws =
    SherpaOnnxCreateKeywordSpotter(&config);

if (!kws) {
    fprintf(stderr, "Please check your config\n");
    return 1;
}

const SherpaOnnxOnlineStream *kws_stream =
    SherpaOnnxCreateKeywordStream(kws);

if (!kws_stream) {
    fprintf(stderr, "Failed to create keyword stream\n");
    SherpaOnnxDestroyKeywordSpotter(kws);
    return 1;
}

    AudioStream *stream = audio_stream_create(&cfg);
    if (!stream) {
        fprintf(stderr, "Failed to create audio stream\n");
        return 1;
    }

    KwsState state;
    memset(&state, 0, sizeof(state));

    atomic_init(&state.demo.callback_count, 0);
    atomic_init(&state.demo.last_rms, 0);

    state.kws = kws;
    state.stream = kws_stream;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    if (audio_stream_start(stream, audio_chunk_cb, &state) != 0) {
        fprintf(stderr, "Failed to start audio stream\n");
        audio_stream_destroy(stream);
        return 1;
    }

    printf("Streaming... press Ctrl+C to stop.\n\n");

    while (!g_stop) {
        sleep(1);
        
        unsigned long cbs =
    atomic_load_explicit(&state.demo.callback_count, memory_order_relaxed);

        long rms =
    atomic_load_explicit(&state.demo.last_rms, memory_order_relaxed);

        printf("chunks: processed=%zu dropped=%zu fill=%zu/%zu | cb=%lu rms=%ld\n",
               audio_stream_total_processed(stream),
               audio_stream_dropped_chunks(stream),
               audio_stream_buffer_fill_level(stream),
               audio_stream_ring_capacity(stream),
               cbs, rms);
    }

    printf("\nStopping...\n");
    audio_stream_stop(stream);
    printf("Final: processed=%zu dropped=%zu\n",
       audio_stream_total_processed(stream),
       audio_stream_dropped_chunks(stream));

       audio_stream_destroy(stream);

       SherpaOnnxDestroyOnlineStream(kws_stream);
       SherpaOnnxDestroyKeywordSpotter(kws);

	return 0;
}
