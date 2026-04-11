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
#define WM8960_MAX_FRAMES 48000

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

static int wm8960_open_pcm(const char *device, snd_pcm_t **pcm_handle) {
    int err;

    if ((err = snd_pcm_open(pcm_handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
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
        fprintf(stderr, "Playback parameter error: %s\n", snd_strerror(err));
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
    if (frames > WM8960_MAX_FRAMES) {
        frames = WM8960_MAX_FRAMES;
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

int main(int argc, char **argv) {
    const char *device = argc > 1 ? argv[1] : "default";
    long interval_ms = argc > 2 ? strtol(argv[2], NULL, 10) : 900;
    snd_pcm_t *pcm_handle = NULL;
    const double notes[] = {220.00, 261.63, 293.66, 329.63, 392.00, 440.00, 523.25, 659.25};
    const size_t note_count = sizeof(notes) / sizeof(notes[0]);
    unsigned int seed = (unsigned int)time(NULL);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (interval_ms < 100) {
        interval_ms = 100;
    }

    srand(seed);

    if (wm8960_open_pcm(device, &pcm_handle) != 0) {
        fprintf(stderr, "WM8960 tone demo could not open ALSA device '%s'.\n", device);
        return 1;
    }

    printf("WM8960 stereo tone demo running on '%s'\n", device);
    printf("Press Ctrl+C to stop.\n");

    while (keep_running) {
        double frequency = notes[rand() % note_count];
        wm8960_pan_t pan = (wm8960_pan_t)(rand() % 3);

        printf("Playing %.2f Hz on %s\n", frequency, wm8960_pan_name(pan));
        wm8960_play_stereo_tone(pcm_handle, frequency, 0.28, pan);
        snd_pcm_drain(pcm_handle);
        sleep_for_ms(interval_ms);
    }

    snd_pcm_close(pcm_handle);
    return 0;
}
