#include <alsa/asoundlib.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WM8960_SAMPLE_RATE 48000
#define WM8960_CHANNELS 2
#define WM8960_RECORD_SECONDS 5

typedef enum {
    WM8960_PAN_LEFT = 0,
    WM8960_PAN_RIGHT,
    WM8960_PAN_BOTH
} wm8960_pan_t;

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static void sleep_for_ms(long milliseconds) {
    struct timespec duration;

    duration.tv_sec = milliseconds / 1000;
    duration.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&duration, NULL);
}

static const char *wm8960_pan_name(wm8960_pan_t pan) {
    switch (pan) {
        case WM8960_PAN_LEFT:
            return "LEFT";
        case WM8960_PAN_RIGHT:
            return "RIGHT";
        case WM8960_PAN_BOTH:
        default:
            return "BOTH";
    }
}

static int wm8960_open_pcm(const char *device, snd_pcm_stream_t stream, snd_pcm_t **pcm_handle) {
    int err;

    if ((err = snd_pcm_open(pcm_handle, device, stream, 0)) < 0) {
        fprintf(stderr, "Error opening PCM device '%s': %s\n", device, snd_strerror(err));
        return -1;
    }

    if ((err = snd_pcm_set_params(*pcm_handle,
                                  SND_PCM_FORMAT_S16_LE,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  WM8960_CHANNELS,
                                  WM8960_SAMPLE_RATE,
                                  1,
                                  500000)) < 0) {
        fprintf(stderr, "PCM parameter error on '%s': %s\n", device, snd_strerror(err));
        snd_pcm_close(*pcm_handle);
        *pcm_handle = NULL;
        return -1;
    }

    return 0;
}

static void wm8960_play_stereo_tone(snd_pcm_t *pcm_handle, double frequency, double duration_seconds, wm8960_pan_t pan) {
    int frames = (int)(duration_seconds * WM8960_SAMPLE_RATE);
    int16_t *buffer;

    if (frames <= 0) {
        return;
    }

    buffer = malloc((size_t)frames * WM8960_CHANNELS * sizeof(int16_t));
    if (buffer == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    for (int i = 0; i < frames; i++) {
        double sample = sin(2.0 * M_PI * frequency * (double)i / WM8960_SAMPLE_RATE);
        int16_t value = (int16_t)(sample * 18000.0);
        int16_t left = 0;
        int16_t right = 0;

        if (pan == WM8960_PAN_LEFT || pan == WM8960_PAN_BOTH) {
            left = value;
        }
        if (pan == WM8960_PAN_RIGHT || pan == WM8960_PAN_BOTH) {
            right = value;
        }

        buffer[(i * 2)] = left;
        buffer[(i * 2) + 1] = right;
    }

    if (snd_pcm_writei(pcm_handle, buffer, frames) < 0) {
        snd_pcm_prepare(pcm_handle);
        snd_pcm_writei(pcm_handle, buffer, frames);
    }

    free(buffer);
}

static void wm8960_run_tone_demo(const char *playback_device) {
    snd_pcm_t *playback_handle = NULL;
    const double notes[] = {220.00, 261.63, 293.66, 329.63, 392.00, 440.00, 523.25, 659.25};
    const size_t note_count = sizeof(notes) / sizeof(notes[0]);

    if (wm8960_open_pcm(playback_device, SND_PCM_STREAM_PLAYBACK, &playback_handle) != 0) {
        fprintf(stderr, "Unable to start stereo tone demo on '%s'.\n", playback_device);
        return;
    }

    printf("Stereo tone demo on '%s'\n", playback_device);
    for (int i = 0; i < 6 && keep_running; i++) {
        double frequency = notes[rand() % note_count];
        wm8960_pan_t pan = (wm8960_pan_t)(rand() % 3);

        printf("Playing %.2f Hz on %s\n", frequency, wm8960_pan_name(pan));
        wm8960_play_stereo_tone(playback_handle, frequency, 0.28, pan);
        snd_pcm_drain(playback_handle);
        sleep_for_ms(500);
    }

    snd_pcm_close(playback_handle);
}

static int wm8960_record_audio(const char *capture_device, int16_t **recorded_buffer, size_t *recorded_frames) {
    snd_pcm_t *capture_handle = NULL;
    size_t total_frames = WM8960_SAMPLE_RATE * WM8960_RECORD_SECONDS;
    size_t captured_frames = 0;
    int16_t *buffer;

    buffer = malloc(total_frames * WM8960_CHANNELS * sizeof(int16_t));
    if (buffer == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return -1;
    }

    if (wm8960_open_pcm(capture_device, SND_PCM_STREAM_CAPTURE, &capture_handle) != 0) {
        free(buffer);
        fprintf(stderr, "Unable to start recording from '%s'.\n", capture_device);
        return -1;
    }

    printf("Recording microphone for %d seconds from '%s'...\n", WM8960_RECORD_SECONDS, capture_device);
    while (captured_frames < total_frames && keep_running) {
        size_t frames_left = total_frames - captured_frames;
        snd_pcm_sframes_t chunk = frames_left > 1024 ? 1024 : (snd_pcm_sframes_t)frames_left;
        snd_pcm_sframes_t read_frames = snd_pcm_readi(capture_handle,
                                                      buffer + (captured_frames * WM8960_CHANNELS),
                                                      chunk);

        if (read_frames < 0) {
            snd_pcm_prepare(capture_handle);
            continue;
        }

        captured_frames += (size_t)read_frames;
    }

    snd_pcm_close(capture_handle);

    *recorded_buffer = buffer;
    *recorded_frames = captured_frames;
    printf("Recorded %zu frames.\n", captured_frames);
    return 0;
}

static int wm8960_play_recording(const char *playback_device, const int16_t *recorded_buffer, size_t recorded_frames) {
    snd_pcm_t *playback_handle = NULL;
    size_t played_frames = 0;

    if (recorded_buffer == NULL || recorded_frames == 0) {
        printf("No recording available. Record first.\n");
        return -1;
    }

    if (wm8960_open_pcm(playback_device, SND_PCM_STREAM_PLAYBACK, &playback_handle) != 0) {
        fprintf(stderr, "Unable to play recording on '%s'.\n", playback_device);
        return -1;
    }

    printf("Playing back recorded audio on '%s'...\n", playback_device);
    while (played_frames < recorded_frames && keep_running) {
        size_t frames_left = recorded_frames - played_frames;
        snd_pcm_sframes_t chunk = frames_left > 1024 ? 1024 : (snd_pcm_sframes_t)frames_left;
        snd_pcm_sframes_t written_frames = snd_pcm_writei(playback_handle,
                                                          recorded_buffer + (played_frames * WM8960_CHANNELS),
                                                          chunk);

        if (written_frames < 0) {
            snd_pcm_prepare(playback_handle);
            continue;
        }

        played_frames += (size_t)written_frames;
    }

    snd_pcm_drain(playback_handle);
    snd_pcm_close(playback_handle);
    return 0;
}

int main(int argc, char **argv) {
    const char *playback_device = argc > 1 ? argv[1] : "default";
    const char *capture_device = argc > 2 ? argv[2] : "default";
    int16_t *recorded_buffer = NULL;
    size_t recorded_frames = 0;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    srand((unsigned int)time(NULL));

    printf("WM8960 audio validation tool\n");
    printf("Playback device: %s\n", playback_device);
    printf("Capture device: %s\n", capture_device);

    while (keep_running) {
        int choice;

        printf("\n=== WM8960 Menu ===\n");
        printf("1. Record microphone for %d seconds\n", WM8960_RECORD_SECONDS);
        printf("2. Playback recorded audio\n");
        printf("3. Run stereo tone demo\n");
        printf("4. Exit\n");
        printf("Select an option: ");

        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n') {
            }
            printf("Invalid input.\n");
            continue;
        }

        switch (choice) {
            case 1:
                free(recorded_buffer);
                recorded_buffer = NULL;
                recorded_frames = 0;
                wm8960_record_audio(capture_device, &recorded_buffer, &recorded_frames);
                break;
            case 2:
                wm8960_play_recording(playback_device, recorded_buffer, recorded_frames);
                break;
            case 3:
                wm8960_run_tone_demo(playback_device);
                break;
            case 4:
                keep_running = 0;
                break;
            default:
                printf("Invalid choice.\n");
                break;
        }
    }

    free(recorded_buffer);
    return 0;
}
