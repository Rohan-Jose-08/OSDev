#include <kernel/io.h>
#include <kernel/speaker.h>
#include <kernel/timer.h>
#include <stdint.h>

#define PIT_CHANNEL2_PORT 0x42
#define PIT_COMMAND 0x43
#define PIT_CMD_CHANNEL2 0x80
#define PIT_MODE3 0x06
#define PIT_BOTH 0x30
#define PIT_BINARY 0x00
#define PIT_FREQUENCY 1193182

#define SPEAKER_PORT 0x61

#define SPEAKER_MIN_HZ 20u
#define SPEAKER_MAX_HZ 20000u
#define SPEAKER_DEFAULT_HZ 440u
#define SPEAKER_DEFAULT_MS 100u

static uint32_t clamp_frequency(uint32_t frequency_hz) {
	if (frequency_hz < SPEAKER_MIN_HZ) {
		return SPEAKER_MIN_HZ;
	}
	if (frequency_hz > SPEAKER_MAX_HZ) {
		return SPEAKER_MAX_HZ;
	}
	return frequency_hz;
}

void speaker_start(uint32_t frequency_hz) {
	uint32_t frequency = clamp_frequency(frequency_hz);
	uint32_t divisor = PIT_FREQUENCY / frequency;
	uint8_t speaker_state;

	if (divisor == 0) {
		divisor = 1;
	}

	outb(PIT_COMMAND, PIT_CMD_CHANNEL2 | PIT_BOTH | PIT_MODE3 | PIT_BINARY);
	outb(PIT_CHANNEL2_PORT, divisor & 0xFF);
	outb(PIT_CHANNEL2_PORT, (divisor >> 8) & 0xFF);

	speaker_state = inb(SPEAKER_PORT);
	outb(SPEAKER_PORT, speaker_state | 0x03);
}

void speaker_stop(void) {
	uint8_t speaker_state = inb(SPEAKER_PORT);
	outb(SPEAKER_PORT, speaker_state & 0xFC);
}

int speaker_beep(uint32_t frequency_hz, uint32_t duration_ms) {
	uint32_t frequency = frequency_hz ? frequency_hz : SPEAKER_DEFAULT_HZ;
	uint32_t duration = duration_ms ? duration_ms : SPEAKER_DEFAULT_MS;

	speaker_start(frequency);
	timer_sleep_ms(duration);
	speaker_stop();
	return 0;
}
