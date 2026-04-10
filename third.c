#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <locale.h>
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

    for (int i = 0; i < frames; i++) {
        buffer[i] = (short)(32767.0 * sin(2.0 * M_PI * freq * i / SAMPLE_RATE));
    }
    
    snd_pcm_writei(pcm_handle, buffer, frames);
    free(buffer);
}

// Function to open the device and play the melody
void play_melody(const char *device) {
    snd_pcm_t *pcm_handle;
    int err;

    if ((err = snd_pcm_open(&pcm_handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "Error opening PCM device '%s': %s\n", device, snd_strerror(err));
        return;
    }

    if ((err = snd_pcm_set_params(pcm_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                  CHANNELS, SAMPLE_RATE, 1, 500000)) < 0) {   
        fprintf(stderr, "Playback parameter error: %s\n", snd_strerror(err));
        snd_pcm_close(pcm_handle);
        return;
    }

    printf("\nPlaying melody on '%s'...\n", device);
    
    play_tone(pcm_handle, 261.63, 0.4); // C4
    play_tone(pcm_handle, 329.63, 0.4); // E4
    play_tone(pcm_handle, 392.00, 0.4); // G4
    play_tone(pcm_handle, 523.25, 0.8); // C5

    snd_pcm_drain(pcm_handle);
    snd_pcm_close(pcm_handle);
    printf("Melody finished.\n");
}

// Function to query ALSA and let the user pick a device
void change_device(char *current_device) {
    void **hints, **n;
    char *name, *desc;
    int i = 1;
    char device_names[50][64];

    if (snd_device_name_hint(-1, "pcm", &hints) < 0) {
        printf("Error getting device list.\n");
        return;
    }

    printf("\n=== Available Playback Devices ===\n");
    n = hints;
    while (*n != NULL && i < 50) {
        name = snd_device_name_get_hint(*n, "NAME");
        desc = snd_device_name_get_hint(*n, "DESC");

        if (name != NULL) {
            if (strncmp(name, "hw:", 3) == 0 || strncmp(name, "plughw:", 7) == 0 || 
                strcmp(name, "default") == 0 || strncmp(name, "sysdefault:", 11) == 0) {
                
                if (desc) {
                    for(int j=0; desc[j]; j++) {
                        if(desc[j] == '\n') desc[j] = ' ';
                    }
                }
                
                printf("%d. %s\n   (%s)\n", i, name, desc ? desc : "No description");
                strncpy(device_names[i], name, 63);
                device_names[i][63] = '\0';
                i++;
            }
            free(name);
            if (desc) free(desc);
        }
        n++;
    }
    snd_device_name_free_hint(hints);

    if (i == 1) {
        printf("No ALSA devices found.\n");
        return;
    }

    int choice;
    printf("\nSelect device number (1-%d, or 0 to cancel): ", i - 1);
    if (scanf("%d", &choice) != 1) {
        while(getchar() != '\n'); 
        printf("Invalid input.\n");
        return;
    }

    if (choice > 0 && choice < i) {
        strncpy(current_device, device_names[choice], 63);
        current_device[63] = '\0'; 
        printf("Device successfully changed to: %s\n", current_device);
    }
}

int main() {
    // Crucial: Initialize native locale support to parse UTF-8 characters properly
    setlocale(LC_ALL, "");

    int choice;
    char device[64] = "default";

    while (1) {
        printf("\n=== Pi Zero ALSA Audio Player & Unicode Test ===\n");
        printf("Current Audio Device: %s\n\n", device);
        
        // Unicode Font Support Test Block
        printf("--- Unicode Font Display Test ---\n");
        printf("1. English:  Hello World\n");
        printf("2. Hindi:    नमस्ते दुनिया\n"); 
        printf("3. Japanese: こんにちは世界\n"); 
        printf("4. Russian:  Привет, мир\n"); 
        printf("5. Spanish:  ¡Hola Mundo!\n");
        printf("---------------------------------\n\n");

        printf("1. Select Audio Device from List\n");
        printf("2. Play Melody\n");
        printf("3. Exit\n");
        printf("Select an option: ");
        
        if (scanf("%d", &choice) != 1) {
            while(getchar() != '\n'); 
            printf("Invalid input. Please enter a number.\n");
            continue;
        }

        switch (choice) {
            case 1:
                change_device(device);
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