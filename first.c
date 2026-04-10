#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE 44100
#define CHANNELS 1

// Function to generate and play a single sine wave tone
void play_tone(snd_pcm_t *pcm_handle, double freq, double duration) {
    int frames = (int)(duration * SAMPLE_RATE);
    short *buffer = malloc(frames * sizeof(short));
    
    if (buffer == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    // Generate the sine wave
    for (int i = 0; i < frames; i++) {
        buffer[i] = (short)(32767.0 * sin(2.0 * M_PI * freq * i / SAMPLE_RATE));
    }
    
    // Write the buffer to the ALSA PCM device
    snd_pcm_writei(pcm_handle, buffer, frames);
    free(buffer);
}

// Function to open the device and play a sequence of notes
void play_melody(const char *device) {
    snd_pcm_t *pcm_handle;
    int err;

    // Open the PCM device in playback mode
    if ((err = snd_pcm_open(&pcm_handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "Error opening PCM device '%s': %s\n", device, snd_strerror(err));
        return;
    }

    // Set hardware parameters: 16-bit little-endian, interleaved, 1 channel, 44100Hz
    if ((err = snd_pcm_set_params(pcm_handle,
                                  SND_PCM_FORMAT_S16_LE,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  CHANNELS,
                                  SAMPLE_RATE,
                                  1,
                                  500000)) < 0) {   /* 0.5 second latency */
        fprintf(stderr, "Playback parameter error: %s\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        return;
    }

    printf("\nPlaying melody on '%s'...\n", device);
    
    // Play a simple C Major arpeggio
    play_tone(pcm_handle, 261.63, 0.4); // C4
    play_tone(pcm_handle, 329.63, 0.4); // E4
    play_tone(pcm_handle, 392.00, 0.4); // G4
    play_tone(pcm_handle, 523.25, 0.8); // C5

    // Ensure all audio is played before closing
    snd_pcm_drain(pcm_handle);
    snd_pcm_close(pcm_handle);
    printf("Melody finished.\n");
}

int main() {
    int choice;
    char device[64] = "default"; // "default" targets the standard ALSA output

    while (1) {
        printf("\n=== Pi Zero ALSA Audio Player ===\n");
        printf("Current Audio Device: %s\n", device);
        printf("1. Change Audio Device (e.g., default, hw:0,0, hw:1,0)\n");
        printf("2. Play Melody\n");
        printf("3. Exit\n");
        printf("Select an option: ");
        
        // Handle invalid character inputs gracefully
        if (scanf("%d", &choice) != 1) {
            while(getchar() != '\n'); // clear the input buffer
            printf("Invalid input. Please enter a number.\n");
            continue;
        }

        switch (choice) {
            case 1:
                printf("Enter device name: ");
                scanf("%63s", device);
                break;
            case 2:
                play_melody(device);
                break;
            case 3:
                printf("Exiting...\n");
                return 0;
            default:
                printf("Invalid choice. Please select 1, 2, or 3.\n");
        }
    }
    return 0;
}