#include <kernel/audio.h>
#include <kernel/cpu.h>
#include <kernel/irq.h>
#include <kernel/io.h>
#include <kernel/memory.h>
#include <kernel/pci.h>
#include <kernel/pic.h>
#include <kernel/timer.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define AC97_CLASS_ID 0x04
#define AC97_SUBCLASS_ID 0x01

#define AC97_NAM_RESET 0x00
#define AC97_NAM_MASTER_VOL 0x02
#define AC97_NAM_PCM_VOL 0x18
#define AC97_NAM_EXT_AUDIO_ID 0x28
#define AC97_NAM_EXT_AUDIO_CTRL 0x2A
#define AC97_NAM_PCM_DAC_RATE 0x2C

#define AC97_PO_BDBAR 0x00
#define AC97_PO_CIV 0x04
#define AC97_PO_LVI 0x05
#define AC97_PO_SR 0x06
#define AC97_PO_PICB 0x08
#define AC97_PO_CR 0x0B

#define AC97_GLOB_CNT 0x2C
#define AC97_GLOB_STA 0x30

#define AC97_PO_CR_RUN 0x01
#define AC97_PO_CR_RESET 0x02

#define AC97_SR_BCIS 0x08
#define AC97_SR_LVBCI 0x04
#define AC97_SR_FIFOE 0x10

#define AC97_GLOB_CNT_GIE 0x01
#define AC97_GLOB_STA_CODEC_READY 0x01

#define AC97_BDL_COUNT 32
#define AC97_DMA_BUFFER_BYTES 4096
#define AUDIO_RING_BYTES (AC97_BDL_COUNT * AC97_DMA_BUFFER_BYTES)

#define AC97_BDL_IOC 0x8000
#define AUDIO_TONE_MIN_HZ 20u
#define AUDIO_TONE_MAX_HZ 20000u
#define AUDIO_TONE_AMPLITUDE 8000

typedef struct {
	uint32_t addr;
	uint16_t length;
	uint16_t flags;
} __attribute__((packed)) ac97_bdl_entry_t;

static ac97_bdl_entry_t ac97_bdl[AC97_BDL_COUNT] __attribute__((aligned(16)));
static uint8_t ac97_dma_buffers[AC97_BDL_COUNT][AC97_DMA_BUFFER_BYTES] __attribute__((aligned(16)));
static uint8_t audio_ring[AUDIO_RING_BYTES] __attribute__((aligned(16)));

typedef struct {
	bool ready;
	uint16_t nam_base;
	uint16_t nabm_base;
	uint8_t irq_line;
	uint8_t last_civ;
	uint8_t master_volume;
	uint8_t pcm_volume;
	bool tone_enabled;
	uint32_t tone_phase;
	uint32_t tone_step;
	uint32_t ring_read;
	uint32_t ring_write;
	uint32_t ring_count;
} audio_state_t;

static audio_state_t audio_state;

static uint32_t audio_lock(void) {
	uint32_t flags = read_eflags();
	cpu_cli();
	return flags;
}

static void audio_unlock(uint32_t flags) {
	if (flags & (1u << 9)) {
		cpu_sti();
	}
}

static uint16_t ac97_read_nam(uint16_t reg) {
	return inw((uint16_t)(audio_state.nam_base + reg));
}

static void ac97_write_nam(uint16_t reg, uint16_t value) {
	outw((uint16_t)(audio_state.nam_base + reg), value);
}

static uint32_t ac97_read_nabm32(uint16_t reg) {
	return inl((uint16_t)(audio_state.nabm_base + reg));
}

static void ac97_write_nabm32(uint16_t reg, uint32_t value) {
	outl((uint16_t)(audio_state.nabm_base + reg), value);
}

static uint16_t ac97_read_nabm16(uint16_t reg) {
	return inw((uint16_t)(audio_state.nabm_base + reg));
}

static void ac97_write_nabm16(uint16_t reg, uint16_t value) {
	outw((uint16_t)(audio_state.nabm_base + reg), value);
}

static uint8_t ac97_read_nabm8(uint16_t reg) {
	return inb((uint16_t)(audio_state.nabm_base + reg));
}

static void ac97_write_nabm8(uint16_t reg, uint8_t value) {
	outb((uint16_t)(audio_state.nabm_base + reg), value);
}

static uint32_t audio_clamp_frequency(uint32_t frequency_hz) {
	if (frequency_hz < AUDIO_TONE_MIN_HZ) {
		return AUDIO_TONE_MIN_HZ;
	}
	if (frequency_hz > AUDIO_TONE_MAX_HZ) {
		return AUDIO_TONE_MAX_HZ;
	}
	return frequency_hz;
}

static uint8_t audio_volume_to_att(uint8_t volume) {
	if (volume >= 100) {
		return 0;
	}
	if (volume == 0) {
		return 31;
	}
	return (uint8_t)(((uint32_t)(100 - volume) * 31u) / 100u);
}

static uint8_t audio_clamp_volume(uint8_t volume) {
	return (volume > 100) ? 100 : volume;
}

static void audio_set_codec_volume(uint16_t reg, uint8_t volume) {
	uint8_t att = audio_volume_to_att(volume);
	uint16_t value = (uint16_t)(att | (att << 8));
	if (volume == 0) {
		value |= 0x8000;
	}
	ac97_write_nam(reg, value);
}

static bool ac97_wait_ready(void) {
	for (int i = 0; i < 100; i++) {
		uint32_t status = ac97_read_nabm32(AC97_GLOB_STA);
		if (status & AC97_GLOB_STA_CODEC_READY) {
			return true;
		}
		timer_sleep_ms(2);
	}
	return false;
}

static int16_t audio_next_tone_sample(void) {
	audio_state.tone_phase += audio_state.tone_step;
	return (audio_state.tone_phase & 0x80000000u) ? AUDIO_TONE_AMPLITUDE : -AUDIO_TONE_AMPLITUDE;
}

static void audio_fill_buffer(uint8_t index) {
	uint8_t *dst = ac97_dma_buffers[index];
	uint32_t remaining = AC97_DMA_BUFFER_BYTES;
	uint32_t flags = audio_lock();
	uint32_t to_copy = remaining;
	if (to_copy > audio_state.ring_count) {
		to_copy = audio_state.ring_count;
	}
	if (to_copy > 0) {
		uint32_t first = to_copy;
		uint32_t ring_space = AUDIO_RING_BYTES - audio_state.ring_read;
		if (first > ring_space) {
			first = ring_space;
		}
		memcpy(dst, audio_ring + audio_state.ring_read, first);
		if (to_copy > first) {
			memcpy(dst + first, audio_ring, to_copy - first);
		}
		audio_state.ring_read = (audio_state.ring_read + to_copy) % AUDIO_RING_BYTES;
		audio_state.ring_count -= to_copy;
	}
	audio_unlock(flags);

	if (to_copy < remaining) {
		memset(dst + to_copy, 0, remaining - to_copy);
	}

	if (audio_state.tone_enabled) {
		int16_t *samples = (int16_t *)dst;
		uint32_t count = AC97_DMA_BUFFER_BYTES / sizeof(int16_t);
		for (uint32_t i = 0; i + 1 < count; i += 2) {
			int16_t tone = audio_next_tone_sample();
			int32_t left = (int32_t)samples[i] + tone;
			int32_t right = (int32_t)samples[i + 1] + tone;
			if (left > INT16_MAX) left = INT16_MAX;
			if (left < INT16_MIN) left = INT16_MIN;
			if (right > INT16_MAX) right = INT16_MAX;
			if (right < INT16_MIN) right = INT16_MIN;
			samples[i] = (int16_t)left;
			samples[i + 1] = (int16_t)right;
		}
	}
}

static void ac97_irq(uint8_t irq) {
	(void)irq;
	if (!audio_state.ready) {
		return;
	}
	uint16_t status = ac97_read_nabm16(AC97_PO_SR);
	if (!status) {
		return;
	}

	if (status & (AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE)) {
		uint8_t civ = ac97_read_nabm8(AC97_PO_CIV);
		uint8_t idx = audio_state.last_civ;
		while (idx != civ) {
			audio_fill_buffer(idx);
			ac97_write_nabm8(AC97_PO_LVI, idx);
			idx = (uint8_t)((idx + 1) & 0x1F);
		}
		audio_state.last_civ = civ;
	}

	ac97_write_nabm16(AC97_PO_SR, status);
}

void audio_init(void) {
	memset(&audio_state, 0, sizeof(audio_state));
	pci_device_t dev;
	if (!pci_find_class(AC97_CLASS_ID, AC97_SUBCLASS_ID, 0xFF, &dev)) {
		return;
	}

	uint32_t bar0 = dev.bar[0];
	uint32_t bar1 = dev.bar[1];
	if ((bar0 & 0x1) == 0 || (bar1 & 0x1) == 0) {
		return;
	}

	audio_state.nam_base = (uint16_t)(bar0 & ~0x3u);
	audio_state.nabm_base = (uint16_t)(bar1 & ~0x3u);
	audio_state.irq_line = dev.irq_line;

	pci_enable_bus_master(&dev);

	ac97_write_nam(AC97_NAM_RESET, 0);
	timer_sleep_ms(10);
	if (!ac97_wait_ready()) {
		return;
	}

	uint16_t ext_id = ac97_read_nam(AC97_NAM_EXT_AUDIO_ID);
	if (ext_id & 0x1) {
		uint16_t ext_ctrl = ac97_read_nam(AC97_NAM_EXT_AUDIO_CTRL);
		ac97_write_nam(AC97_NAM_EXT_AUDIO_CTRL, (uint16_t)(ext_ctrl | 0x1));
		ac97_write_nam(AC97_NAM_PCM_DAC_RATE, AUDIO_SAMPLE_RATE);
	}

	for (uint32_t i = 0; i < AC97_BDL_COUNT; i++) {
		ac97_bdl[i].addr = virt_to_phys(ac97_dma_buffers[i]);
		ac97_bdl[i].length = AC97_DMA_BUFFER_BYTES;
		ac97_bdl[i].flags = AC97_BDL_IOC;
		memset(ac97_dma_buffers[i], 0, AC97_DMA_BUFFER_BYTES);
	}

	ac97_write_nabm8(AC97_PO_CR, AC97_PO_CR_RESET);
	timer_sleep_ms(1);
	ac97_write_nabm8(AC97_PO_CR, 0);
	ac97_write_nabm16(AC97_PO_SR, 0x1F);

	ac97_write_nabm32(AC97_PO_BDBAR, virt_to_phys(ac97_bdl));
	ac97_write_nabm8(AC97_PO_LVI, AC97_BDL_COUNT - 1);
	audio_state.last_civ = ac97_read_nabm8(AC97_PO_CIV);

	uint32_t glob_cnt = ac97_read_nabm32(AC97_GLOB_CNT);
	ac97_write_nabm32(AC97_GLOB_CNT, glob_cnt | AC97_GLOB_CNT_GIE);

	if (audio_state.irq_line >= 16) {
		return;
	}
	irq_register(audio_state.irq_line, ac97_irq);
	IRQ_clear_mask(audio_state.irq_line);

	audio_state.ready = true;
	audio_set_volume(80, 80);
	ac97_write_nabm8(AC97_PO_CR, AC97_PO_CR_RUN);
}

bool audio_is_ready(void) {
	return audio_state.ready;
}

int audio_write(const void *data, uint32_t bytes) {
	if (!audio_state.ready || !data || bytes == 0) {
		return -1;
	}
	uint32_t flags = audio_lock();
	uint32_t free_space = AUDIO_RING_BYTES - audio_state.ring_count;
	if (free_space == 0) {
		audio_unlock(flags);
		return 0;
	}
	uint32_t to_copy = bytes;
	if (to_copy > free_space) {
		to_copy = free_space;
	}
	uint32_t first = to_copy;
	uint32_t ring_space = AUDIO_RING_BYTES - audio_state.ring_write;
	if (first > ring_space) {
		first = ring_space;
	}
	memcpy(audio_ring + audio_state.ring_write, data, first);
	if (to_copy > first) {
		memcpy(audio_ring, (const uint8_t *)data + first, to_copy - first);
	}
	audio_state.ring_write = (audio_state.ring_write + to_copy) % AUDIO_RING_BYTES;
	audio_state.ring_count += to_copy;
	audio_unlock(flags);
	return (int)to_copy;
}

bool audio_set_volume(uint8_t master, uint8_t pcm) {
	if (!audio_state.ready) {
		return false;
	}
	master = audio_clamp_volume(master);
	pcm = audio_clamp_volume(pcm);
	audio_state.master_volume = master;
	audio_state.pcm_volume = pcm;
	audio_set_codec_volume(AC97_NAM_MASTER_VOL, master);
	audio_set_codec_volume(AC97_NAM_PCM_VOL, pcm);
	return true;
}

bool audio_get_volume(uint8_t *master, uint8_t *pcm) {
	if (!audio_state.ready) {
		return false;
	}
	if (master) {
		*master = audio_state.master_volume;
	}
	if (pcm) {
		*pcm = audio_state.pcm_volume;
	}
	return true;
}

void audio_tone_start(uint32_t frequency_hz) {
	if (!audio_state.ready) {
		return;
	}
	uint32_t flags = audio_lock();
	uint32_t freq = audio_clamp_frequency(frequency_hz);
	audio_state.tone_step = (uint32_t)((freq * 0x100000000ULL) / AUDIO_SAMPLE_RATE);
	audio_state.tone_phase = 0;
	audio_state.tone_enabled = true;
	audio_unlock(flags);
}

void audio_tone_stop(void) {
	if (!audio_state.ready) {
		return;
	}
	uint32_t flags = audio_lock();
	audio_state.tone_enabled = false;
	audio_state.tone_step = 0;
	audio_unlock(flags);
}
