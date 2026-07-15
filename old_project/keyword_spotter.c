#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>

#include "c-api.h"
#include "audio_stream.h"

/*---------------------------------------------------------
 * Keyword Spotting Runtime State
 *
 * This structure is passed to the streaming callback
 * through user_data so the callback can access the
 * Sherpa-ONNX objects.
 *--------------------------------------------------------*/
typedef struct {
    const SherpaOnnxKeywordSpotter *kws;
    const SherpaOnnxOnlineStream *stream;
    float pcm_float[160];
} KwsState;

/*---------------------------------------------------------
 * Audio callback
 *--------------------------------------------------------*/
static void audio_chunk_cb(const int16_t *samples,
                           int num_samples,
                           void *user_data) {

    KwsState *state = (KwsState *)user_data;

    double sum_sq = 0.0;

    for (int i = 0; i < num_samples; i++) {
        double v = (double)samples[i];
        sum_sq += v * v;
    }

    long rms = (long)sqrt(sum_sq / (num_samples > 0 ? num_samples : 1));

     /* ---------------------------------------------------------
     * Debug:
     * Uncomment the line below to print the RMS value of each
     * incoming audio chunk. This is useful for checking
     * microphone input levels and troubleshooting audio capture.
     * --------------------------------------------------------- */
    //printf("RMS : %ld\n", rms);

    /* Convert int16 PCM to float */
    for (int i = 0; i < num_samples; i++) {
        state->pcm_float[i] = samples[i] / 32768.0f;
    }

    /* Feed audio chunk to Sherpa */
    SherpaOnnxOnlineStreamAcceptWaveform(
        state->stream,
        16000,
        state->pcm_float,
        num_samples);

/* Decode whenever enough audio is available */
    while (SherpaOnnxIsKeywordStreamReady(state->kws, state->stream)) {

    SherpaOnnxDecodeKeywordStream(state->kws, state->stream);

    const SherpaOnnxKeywordResult *r =
        SherpaOnnxGetKeywordResult(state->kws, state->stream);

    if (r && r->json && strlen(r->keyword)) {

    printf("\n=========================================\n");
    printf("Keyword Detected : %s\n", r->keyword);
    printf("Detection Details: %s\n", r->json);
    printf("=========================================\n");

    SherpaOnnxResetKeywordStream(state->kws, state->stream);
    }

    SherpaOnnxDestroyKeywordResult(r);
  }

}

/*---------------------------------------------------------
 * Ctrl+C handler for streaming mode
 *
 * When the user presses Ctrl+C, g_stop becomes 1.
 * The streaming loop will exit gracefully instead of
 * terminating abruptly.
 *--------------------------------------------------------*/
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

int32_t main(int argc, char *argv[]) {
  bool microphone_mode = false;
  bool mock_mode = false;
  bool stdin_mode = false;

    const char *wav_filename = NULL;
    const char *mock_filename = NULL;

    if (argc != 2 && argc != 3) {

        printf("\n");
        printf("=========================================\n");
        printf("Streaming Keyword Spotter (Sherpa-ONNX)\n");
        printf("=========================================\n\n");

        printf("Usage:\n");
        printf("  ./keyword_spotter --wav <audio.wav>\n");
        printf("  ./keyword_spotter --mic\n");
	printf("  ./keyword_spotter --mock <audio.wav>\n");
	printf("  ./keyword_spotter --stdin\n\n");

        printf("Examples:\n");
        printf("  ./keyword_spotter --wav heyalexa.wav\n");
        printf("  ./keyword_spotter --wav anil.wav\n");
        printf("  ./keyword_spotter --mic\n");
	printf("  ./keyword_spotter --mock sample_heyalexa2.wav\n");
	printf("  ./keyword_spotter --stdin   # reads raw PCM16LE 16kHz mono from stdin\n\n");

        return 0;
    }

    if (strcmp(argv[1], "--mic") == 0) {
        microphone_mode = true;
    }
    else if (strcmp(argv[1], "--wav") == 0 && argc == 3) {
        wav_filename = argv[2];
    }
    else if (strcmp(argv[1], "--mock") == 0 && argc == 3) {
    mock_mode = true;
    mock_filename = argv[2];
}
    else if (strcmp(argv[1], "--stdin") == 0) {
    stdin_mode = true;
}
    else {
        printf("Invalid arguments.\n");
        return 1;
    }
  SherpaOnnxKeywordSpotterConfig config;

  memset(&config, 0, sizeof(config));

  config.model_config.transducer.encoder =
      "/home/ubuntu/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/"
      "encoder-epoch-13-avg-2-chunk-16-left-64.onnx";

  config.model_config.transducer.decoder =
      "/home/ubuntu/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/"
      "decoder-epoch-13-avg-2-chunk-16-left-64.onnx";

  config.model_config.transducer.joiner =
      "/home/ubuntu/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/"
      "joiner-epoch-13-avg-2-chunk-16-left-64.onnx";

  config.model_config.tokens =
      "/home/ubuntu/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/"
      "tokens.txt";

  config.model_config.provider = "cpu";
  config.model_config.num_threads = 1;
  config.model_config.debug = 1;
  // Lower the detection threshold for testing custom recordings
  //config.keywords_score = 1.0;
  //config.keywords_threshold = 0.1;

  config.keywords_file =
      "/home/ubuntu/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/"
      "test_wavs/keywords.txt";

  const SherpaOnnxKeywordSpotter *kws =
      SherpaOnnxCreateKeywordSpotter(&config);
  if (!kws) {
    fprintf(stderr, "Please check your config");
    exit(-1);
  }

  // Repository sample audio
  //const char *wav_filename =
    //"/home/ubuntu/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/"
    //"test_wavs/en_0.wav";


  // My recorded audio
  //const char *wav_filename =
    //"/home/ubuntu/kws_project/cloud_computing1.wav";

/*
 // commenting Because it is used inside the microphonr_mode block
  float tail_paddings[8000] = {0};
  const SherpaOnnxWave *wave = SherpaOnnxReadWave(wav_filename);
  if (wave == NULL) {
    fprintf(stderr, "Failed to read %s\n", wav_filename);
    exit(-1);
  }
*/
  const SherpaOnnxOnlineStream *stream = SherpaOnnxCreateKeywordStream(kws);
  if (!stream) {
   fprintf(stderr, "Failed to create stream\n");
    exit(-1);
  }
 /*---------------------------------------------------------
 * Runtime state for streaming callback
 *--------------------------------------------------------*/
  KwsState state;

  memset(&state, 0, sizeof(state));

  state.kws = kws;
  state.stream = stream;

  if (wav_filename) {

    printf("\n");
    printf("=========================================\n");
    printf("Streaming Keyword Spotter (Sherpa-ONNX)\n");
    printf("=========================================\n\n");

    printf("Mode        : WAV File\n");
    printf("Input File  : %s\n\n", wav_filename);

    float tail_paddings[8000] = {0};

    const SherpaOnnxWave *wave = SherpaOnnxReadWave(wav_filename);

    if (wave == NULL) {
        fprintf(stderr, "Failed to read %s\n", wav_filename);
        exit(-1);
    }

    SherpaOnnxOnlineStreamAcceptWaveform(
        stream,
        wave->sample_rate,
        wave->samples,
        wave->num_samples);

    SherpaOnnxOnlineStreamAcceptWaveform(
        stream,
        wave->sample_rate,
        tail_paddings,
        sizeof(tail_paddings) / sizeof(float));

    SherpaOnnxOnlineStreamInputFinished(stream);

    while (SherpaOnnxIsKeywordStreamReady(kws, stream)) {

        SherpaOnnxDecodeKeywordStream(kws, stream);

        const SherpaOnnxKeywordResult *r =
            SherpaOnnxGetKeywordResult(kws, stream);

        if (r && r->json && strlen(r->keyword)) {

        printf("\n=========================================\n");
        printf("Keyword Detected : %s\n", r->keyword);
        printf("Detection Details: %s\n", r->json);
        printf("=========================================\n");

            SherpaOnnxResetKeywordStream(kws, stream);
        }

        SherpaOnnxDestroyKeywordResult(r);
    }

    SherpaOnnxFreeWave(wave);

} else if (microphone_mode) {

    printf("\n");
    printf("=========================================\n");
    printf("Streaming Keyword Spotter (Sherpa-ONNX)\n");
    printf("=========================================\n\n");

    printf("Mode        : Microphone\n");
    printf("Device      : default\n");
    printf("Sample Rate : 16000 Hz\n");
    printf("Chunk Size  : 160 samples (10 ms)\n\n");

    AudioStreamConfig cfg;

    audio_stream_default_config(&cfg);

    AudioStream *audio = audio_stream_create(&cfg);

    if (!audio) {
        fprintf(stderr, "Failed to create audio stream\n");
        exit(-1);
    }

    signal(SIGINT, on_sigint);

    if (audio_stream_start(audio, audio_chunk_cb, &state) != 0) {
        fprintf(stderr, "Failed to start audio stream\n");
        audio_stream_destroy(audio);
        exit(-1);
    }

    printf("Listening...\n");
    printf("Say the wake word.\n");
    printf("Press Ctrl+C to stop.\n\n");

    while (!g_stop) {
        usleep(100000);
    }

    printf("\nStopping...\n");

    audio_stream_stop(audio);
    audio_stream_destroy(audio);
}

else if (mock_mode) {

    printf("\n");
    printf("=========================================\n");
    printf("Streaming Keyword Spotter (Sherpa-ONNX)\n");
    printf("=========================================\n\n");

    printf("Mode        : Mock Streaming\n");
    printf("Input File  : %s\n\n", mock_filename);
    
    AudioStreamConfig cfg;
    audio_stream_default_config(&cfg);

    cfg.device = mock_filename;

    AudioStream *audio = audio_stream_create(&cfg);

    if (!audio) {
        fprintf(stderr, "Failed to create audio stream\n");
        exit(-1);
    }

    signal(SIGINT, on_sigint);

    if (audio_stream_start(audio, audio_chunk_cb, &state) != 0) {
        fprintf(stderr, "Failed to start audio stream\n");
        audio_stream_destroy(audio);
        exit(-1);
    }

    printf("Streaming from mock WAV...\n");
    printf("Press Ctrl+C to stop.\n\n");

    while (!g_stop) {
        usleep(100000);
    }

    printf("\nStopping...\n");

    audio_stream_stop(audio);
    audio_stream_destroy(audio);

}

else if (stdin_mode) {

    /* Line-buffer stdout so every printed line reaches the reader
     * (e.g. a Python subprocess pipe) immediately instead of waiting
     * for a full 4KB/64KB stdio buffer to fill. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    fprintf(stderr, "\n");
    fprintf(stderr, "=========================================\n");
    fprintf(stderr, "Streaming Keyword Spotter (Sherpa-ONNX)\n");
    fprintf(stderr, "=========================================\n\n");
    fprintf(stderr, "Mode        : STDIN\n");
    fprintf(stderr, "Format      : PCM16LE, 16000 Hz, mono\n");
    fprintf(stderr, "Chunk Size  : 160 samples (10 ms)\n\n");
    fprintf(stderr, "Reading audio from stdin. Send raw PCM16LE bytes.\n");
    fflush(stderr);

    signal(SIGINT, on_sigint);

    const int chunk_samples = 160; /* 10 ms @ 16 kHz, matches --mic path */
    int16_t pcm_i16[160];
    float   pcm_f32[160];

    while (!g_stop) {
        size_t got = fread(pcm_i16, sizeof(int16_t), chunk_samples, stdin);

        if (got == 0) {
            if (feof(stdin)) break;   /* upstream closed the pipe: stop cleanly   */
            if (ferror(stdin)) break; /* read error: stop cleanly                 */
            continue;
        }

        for (size_t i = 0; i < got; i++) {
            pcm_f32[i] = pcm_i16[i] / 32768.0f;
        }

        SherpaOnnxOnlineStreamAcceptWaveform(stream, 16000, pcm_f32, (int)got);

        while (SherpaOnnxIsKeywordStreamReady(kws, stream)) {

            SherpaOnnxDecodeKeywordStream(kws, stream);

            const SherpaOnnxKeywordResult *r =
                SherpaOnnxGetKeywordResult(kws, stream);

            if (r && r->json && strlen(r->keyword)) {
                /* Machine-readable line for the Python/Node wrapper to grep for.
                 * Everything else on stdout/stderr can be ignored by the wrapper. */
                printf("KEYWORD_JSON:%s\n", r->json);
                fflush(stdout);

                SherpaOnnxResetKeywordStream(kws, stream);
            }

            SherpaOnnxDestroyKeywordResult(r);
        }
    }

    fprintf(stderr, "\nStdin closed. Stopping...\n");
}

/*
// Old WAV-PROCESSING CODE (NEED TO REMOVE)
  SherpaOnnxOnlineStreamAcceptWaveform(stream, wave->sample_rate,
                                       wave->samples, wave->num_samples);

  SherpaOnnxOnlineStreamAcceptWaveform(stream, wave->sample_rate,
                                       tail_paddings,
                                       sizeof(tail_paddings) / sizeof(float));

  SherpaOnnxOnlineStreamInputFinished(stream);

  while (SherpaOnnxIsKeywordStreamReady(kws, stream)) {
    SherpaOnnxDecodeKeywordStream(kws, stream);
    const SherpaOnnxKeywordResult *r =
        SherpaOnnxGetKeywordResult(kws, stream);
    if (r && r->json && strlen(r->keyword)) {
      fprintf(stderr, "Detected keyword: %s\n", r->json);
      SherpaOnnxResetKeywordStream(kws, stream);
    }
    SherpaOnnxDestroyKeywordResult(r);
  }

 */
  SherpaOnnxDestroyOnlineStream(stream);
  //SherpaOnnxFreeWave(wave); //commenting this because wave functionis not using 
  SherpaOnnxDestroyKeywordSpotter(kws);

  return 0;
}
