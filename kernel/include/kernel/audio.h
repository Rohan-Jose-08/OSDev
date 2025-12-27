#ifndef KERNEL_AUDIO_H
#define KERNEL_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_SAMPLE_RATE 48000u
#define AUDIO_CHANNELS 2u
#define AUDIO_BYTES_PER_SAMPLE 2u
#define AUDIO_BYTES_PER_FRAME (AUDIO_CHANNELS * AUDIO_BYTES_PER_SAMPLE)

void audio_init(void);
bool audio_is_ready(void);
int audio_write(const void *data, uint32_t bytes);
bool audio_set_volume(uint8_t master, uint8_t pcm);
bool audio_get_volume(uint8_t *master, uint8_t *pcm);
void audio_tone_start(uint32_t frequency_hz);
void audio_tone_stop(void);

#endif
