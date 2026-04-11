#ifndef DEMO_AUDIO_H
#define DEMO_AUDIO_H

#include <alsa/asoundlib.h>

#define DEMO_SAMPLE_RATE 44100
#define DEMO_CHANNELS 1

int demo_open_playback_device(const char *device, snd_pcm_t **pcm_handle);
void demo_play_tone(snd_pcm_t *pcm_handle, double freq, double duration);
void demo_play_single_tone(const char *device, double freq, double duration);
void demo_play_c_major_arpeggio(const char *device);
int demo_list_playback_devices(char device_names[][64], int max_devices);

#endif
