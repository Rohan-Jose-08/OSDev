#ifndef KERNEL_SPEAKER_H
#define KERNEL_SPEAKER_H

#include <stdint.h>

int speaker_beep(uint32_t frequency_hz, uint32_t duration_ms);
void speaker_start(uint32_t frequency_hz);
void speaker_stop(void);

#endif
