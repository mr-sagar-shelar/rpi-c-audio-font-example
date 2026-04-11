#include "demo_audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int demo_open_playback_device(const char *device, snd_pcm_t **pcm_handle) {
    int err;

    if ((err = snd_pcm_open(pcm_handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "Error opening PCM device '%s': %s\n", device, snd_strerror(err));
        return -1;
    }

    if ((err = snd_pcm_set_params(*pcm_handle,
                                  SND_PCM_FORMAT_S16_LE,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  DEMO_CHANNELS,
                                  DEMO_SAMPLE_RATE,
                                  1,
                                  500000)) < 0) {
        fprintf(stderr, "Playback parameter error: %s\n", snd_strerror(err));
        snd_pcm_close(*pcm_handle);
        *pcm_handle = NULL;
        return -1;
    }

    return 0;
}

void demo_play_tone(snd_pcm_t *pcm_handle, double freq, double duration) {
    int frames = (int)(duration * DEMO_SAMPLE_RATE);
    short *buffer = malloc((size_t)frames * sizeof(short));

    if (buffer == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    for (int i = 0; i < frames; i++) {
        buffer[i] = (short)(32767.0 * sin(2.0 * M_PI * freq * i / DEMO_SAMPLE_RATE));
    }

    snd_pcm_writei(pcm_handle, buffer, frames);
    free(buffer);
}

void demo_play_single_tone(const char *device, double freq, double duration) {
    snd_pcm_t *pcm_handle = NULL;

    if (demo_open_playback_device(device, &pcm_handle) != 0) {
        return;
    }

    demo_play_tone(pcm_handle, freq, duration);
    snd_pcm_drain(pcm_handle);
    snd_pcm_close(pcm_handle);
}

void demo_play_c_major_arpeggio(const char *device) {
    snd_pcm_t *pcm_handle = NULL;

    if (demo_open_playback_device(device, &pcm_handle) != 0) {
        return;
    }

    printf("\nPlaying melody on '%s'...\n", device);
    demo_play_tone(pcm_handle, 261.63, 0.4);
    demo_play_tone(pcm_handle, 329.63, 0.4);
    demo_play_tone(pcm_handle, 392.00, 0.4);
    demo_play_tone(pcm_handle, 523.25, 0.8);

    snd_pcm_drain(pcm_handle);
    snd_pcm_close(pcm_handle);
    printf("Melody finished.\n");
}

int demo_list_playback_devices(char device_names[][64], int max_devices) {
    void **hints = NULL;
    void **node = NULL;
    int count = 0;

    if (snd_device_name_hint(-1, "pcm", &hints) < 0) {
        printf("Error getting device list.\n");
        return 0;
    }

    printf("\n=== Available Playback Devices ===\n");
    node = hints;
    while (*node != NULL && count < max_devices) {
        char *name = snd_device_name_get_hint(*node, "NAME");
        char *desc = snd_device_name_get_hint(*node, "DESC");

        if (name != NULL) {
            if (strncmp(name, "hw:", 3) == 0 ||
                strncmp(name, "plughw:", 7) == 0 ||
                strcmp(name, "default") == 0 ||
                strncmp(name, "sysdefault:", 11) == 0) {
                if (desc != NULL) {
                    for (int i = 0; desc[i] != '\0'; i++) {
                        if (desc[i] == '\n') {
                            desc[i] = ' ';
                        }
                    }
                }

                printf("%d. %s\n   (%s)\n", count + 1, name, desc != NULL ? desc : "No description");
                strncpy(device_names[count], name, 63);
                device_names[count][63] = '\0';
                count++;
            }
        }

        if (name != NULL) {
            free(name);
        }
        if (desc != NULL) {
            free(desc);
        }
        node++;
    }

    snd_device_name_free_hint(hints);
    return count;
}
