#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <kernel/interrupt.h>
#include <kernel/tty.h>
#include <kernel/pagings.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/shell.h>
#include <kernel/pic.h>
#include <kernel/graphics.h>
#include <kernel/timer.h>
#include <kernel/task.h>
#include <kernel/ata.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <kernel/cpu.h>
#include <kernel/gdt.h>
#include <kernel/kpti.h>
#include <kernel/user_programs.h>
#include <kernel/process.h>
#include <kernel/net.h>
#include <kernel/audio.h>

#include <stdint.h>
#include <stddef.h> 

#define SAMPLE_PPM_W 8
#define SAMPLE_PPM_H 8
#define SAMPLE_PGM_W 16
#define SAMPLE_PGM_H 8
#define SAMPLE_PNT_W 16
#define SAMPLE_PNT_H 16

#define PAINT_FILE_MAGIC 0x544E4950
#define PAINT_FILE_VERSION 1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t reserved;
} __attribute__((packed)) paint_file_header_t;

static bool sample_file_exists(const char *path) {
    fs_inode_t inode;
    return fs_stat(path, &inode) && inode.type == 1;
}

static void sample_write_file(const char *path, const uint8_t *data, uint32_t size) {
    if (!path || !data || size == 0) {
        return;
    }
    if (sample_file_exists(path)) {
        return;
    }
    int res = fs_create_file(path);
    if (res < 0 && res != -2) {
        return;
    }
    fs_write_file(path, data, size, 0);
}

static void create_sample_images(void) {
    fs_create_dir("/samples");

    uint8_t ppm_data[32 + SAMPLE_PPM_W * SAMPLE_PPM_H * 3];
    char ppm_header[32];
    int ppm_header_len = snprintf(ppm_header, sizeof(ppm_header), "P6\n%d %d\n255\n",
                                  SAMPLE_PPM_W, SAMPLE_PPM_H);
    if (ppm_header_len > 0) {
        memcpy(ppm_data, ppm_header, (size_t)ppm_header_len);
        uint8_t *pix = ppm_data + ppm_header_len;
        for (int y = 0; y < SAMPLE_PPM_H; y++) {
            for (int x = 0; x < SAMPLE_PPM_W; x++) {
                uint8_t r = (x < SAMPLE_PPM_W / 2) ? 255 : 0;
                uint8_t g = (y < SAMPLE_PPM_H / 2) ? 255 : 0;
                uint8_t b = ((x + y) & 1) ? 255 : 0;
                *pix++ = r;
                *pix++ = g;
                *pix++ = b;
            }
        }
        uint32_t ppm_size = (uint32_t)ppm_header_len + SAMPLE_PPM_W * SAMPLE_PPM_H * 3;
        sample_write_file("/samples/sample.ppm", ppm_data, ppm_size);
    }

    uint8_t pgm_data[32 + SAMPLE_PGM_W * SAMPLE_PGM_H];
    char pgm_header[32];
    int pgm_header_len = snprintf(pgm_header, sizeof(pgm_header), "P5\n%d %d\n255\n",
                                  SAMPLE_PGM_W, SAMPLE_PGM_H);
    if (pgm_header_len > 0) {
        memcpy(pgm_data, pgm_header, (size_t)pgm_header_len);
        uint8_t *pix = pgm_data + pgm_header_len;
        for (int y = 0; y < SAMPLE_PGM_H; y++) {
            for (int x = 0; x < SAMPLE_PGM_W; x++) {
                uint8_t v = (uint8_t)((x * 255) / (SAMPLE_PGM_W - 1));
                *pix++ = v;
            }
        }
        uint32_t pgm_size = (uint32_t)pgm_header_len + SAMPLE_PGM_W * SAMPLE_PGM_H;
        sample_write_file("/samples/sample.pgm", pgm_data, pgm_size);
    }

    uint8_t pnt_data[sizeof(paint_file_header_t) + SAMPLE_PNT_W * SAMPLE_PNT_H];
    paint_file_header_t header;
    header.magic = PAINT_FILE_MAGIC;
    header.version = PAINT_FILE_VERSION;
    header.width = SAMPLE_PNT_W;
    header.height = SAMPLE_PNT_H;
    header.reserved = 0;
    memcpy(pnt_data, &header, sizeof(header));
    uint8_t *pnt_pix = pnt_data + sizeof(header);
    for (int y = 0; y < SAMPLE_PNT_H; y++) {
        for (int x = 0; x < SAMPLE_PNT_W; x++) {
            uint8_t color = (uint8_t)((x / 2 + y / 2) % 8);
            *pnt_pix++ = color;
        }
    }
    sample_write_file("/samples/sample.pnt", pnt_data, sizeof(pnt_data));
}

extern const uint8_t _binary_hello_elf_start[];
extern const uint8_t _binary_hello_elf_end[];
extern const uint8_t _binary_cat_elf_start[];
extern const uint8_t _binary_cat_elf_end[];
extern const uint8_t _binary_execdemo_elf_start[];
extern const uint8_t _binary_execdemo_elf_end[];
extern const uint8_t _binary_statdemo_elf_start[];
extern const uint8_t _binary_statdemo_elf_end[];
extern const uint8_t _binary_ls_elf_start[];
extern const uint8_t _binary_ls_elf_end[];
extern const uint8_t _binary_rm_elf_start[];
extern const uint8_t _binary_rm_elf_end[];
extern const uint8_t _binary_mkdir_elf_start[];
extern const uint8_t _binary_mkdir_elf_end[];
extern const uint8_t _binary_touch_elf_start[];
extern const uint8_t _binary_touch_elf_end[];
extern const uint8_t _binary_pwd_elf_start[];
extern const uint8_t _binary_pwd_elf_end[];

extern const uint8_t _binary_echo_elf_start[];
extern const uint8_t _binary_echo_elf_end[];
extern const uint8_t _binary_reverse_elf_start[];
extern const uint8_t _binary_reverse_elf_end[];
extern const uint8_t _binary_strlen_elf_start[];
extern const uint8_t _binary_strlen_elf_end[];
extern const uint8_t _binary_upper_elf_start[];
extern const uint8_t _binary_upper_elf_end[];
extern const uint8_t _binary_lower_elf_start[];
extern const uint8_t _binary_lower_elf_end[];
extern const uint8_t _binary_calc_elf_start[];
extern const uint8_t _binary_calc_elf_end[];
extern const uint8_t _binary_draw_elf_start[];
extern const uint8_t _binary_draw_elf_end[];
extern const uint8_t _binary_banner_elf_start[];
extern const uint8_t _binary_banner_elf_end[];
extern const uint8_t _binary_clear_elf_start[];
extern const uint8_t _binary_clear_elf_end[];
extern const uint8_t _binary_color_elf_start[];
extern const uint8_t _binary_color_elf_end[];
extern const uint8_t _binary_colors_elf_start[];
extern const uint8_t _binary_colors_elf_end[];
extern const uint8_t _binary_write_elf_start[];
extern const uint8_t _binary_write_elf_end[];
extern const uint8_t _binary_history_elf_start[];
extern const uint8_t _binary_history_elf_end[];
extern const uint8_t _binary_cd_elf_start[];
extern const uint8_t _binary_cd_elf_end[];
extern const uint8_t _binary_help_elf_start[];
extern const uint8_t _binary_help_elf_end[];
extern const uint8_t _binary_about_elf_start[];
extern const uint8_t _binary_about_elf_end[];
extern const uint8_t _binary_sysinfo_elf_start[];
extern const uint8_t _binary_sysinfo_elf_end[];
extern const uint8_t _binary_uptime_elf_start[];
extern const uint8_t _binary_uptime_elf_end[];
extern const uint8_t _binary_randcolor_elf_start[];
extern const uint8_t _binary_randcolor_elf_end[];
extern const uint8_t _binary_rainbow_elf_start[];
extern const uint8_t _binary_rainbow_elf_end[];
extern const uint8_t _binary_art_elf_start[];
extern const uint8_t _binary_art_elf_end[];
extern const uint8_t _binary_fortune_elf_start[];
extern const uint8_t _binary_fortune_elf_end[];
extern const uint8_t _binary_animate_elf_start[];
extern const uint8_t _binary_animate_elf_end[];
extern const uint8_t _binary_matrix_elf_start[];
extern const uint8_t _binary_matrix_elf_end[];
extern const uint8_t _binary_guess_elf_start[];
extern const uint8_t _binary_guess_elf_end[];
extern const uint8_t _binary_rps_elf_start[];
extern const uint8_t _binary_rps_elf_end[];
extern const uint8_t _binary_tictactoe_elf_start[];
extern const uint8_t _binary_tictactoe_elf_end[];
extern const uint8_t _binary_hangman_elf_start[];
extern const uint8_t _binary_hangman_elf_end[];
extern const uint8_t _binary_timer_elf_start[];
extern const uint8_t _binary_timer_elf_end[];
extern const uint8_t _binary_alias_elf_start[];
extern const uint8_t _binary_alias_elf_end[];
extern const uint8_t _binary_unalias_elf_start[];
extern const uint8_t _binary_unalias_elf_end[];
extern const uint8_t _binary_aliases_elf_start[];
extern const uint8_t _binary_aliases_elf_end[];
extern const uint8_t _binary_theme_elf_start[];
extern const uint8_t _binary_theme_elf_end[];
extern const uint8_t _binary_beep_elf_start[];
extern const uint8_t _binary_beep_elf_end[];
extern const uint8_t _binary_soundtest_elf_start[];
extern const uint8_t _binary_soundtest_elf_end[];
extern const uint8_t _binary_mixer_elf_start[];
extern const uint8_t _binary_mixer_elf_end[];
extern const uint8_t _binary_halt_elf_start[];
extern const uint8_t _binary_halt_elf_end[];
extern const uint8_t _binary_run_elf_start[];
extern const uint8_t _binary_run_elf_end[];
extern const uint8_t _binary_rmdir_elf_start[];
extern const uint8_t _binary_rmdir_elf_end[];
extern const uint8_t _binary_gfx_elf_start[];
extern const uint8_t _binary_gfx_elf_end[];
extern const uint8_t _binary_gfxanim_elf_start[];
extern const uint8_t _binary_gfxanim_elf_end[];
extern const uint8_t _binary_gfxpaint_elf_start[];
extern const uint8_t _binary_gfxpaint_elf_end[];
extern const uint8_t _binary_gui_elf_start[];
extern const uint8_t _binary_gui_elf_end[];
extern const uint8_t _binary_guipaint_elf_start[];
extern const uint8_t _binary_guipaint_elf_end[];
extern const uint8_t _binary_guicalc_elf_start[];
extern const uint8_t _binary_guicalc_elf_end[];
extern const uint8_t _binary_guifilemgr_elf_start[];
extern const uint8_t _binary_guifilemgr_elf_end[];
extern const uint8_t _binary_desktop_elf_start[];
extern const uint8_t _binary_desktop_elf_end[];
extern const uint8_t _binary_forktest_elf_start[];
extern const uint8_t _binary_forktest_elf_end[];
extern const uint8_t _binary_schedtest_elf_start[];
extern const uint8_t _binary_schedtest_elf_end[];
extern const uint8_t _binary_fault_elf_start[];
extern const uint8_t _binary_fault_elf_end[];
extern const uint8_t _binary_abi_test_elf_start[];
extern const uint8_t _binary_abi_test_elf_end[];

typedef struct {
    const char *path;
    const uint8_t *start;
    const uint8_t *end;
} embedded_program_t;

static bool install_user_program(const char *path,
                                 const uint8_t *start,
                                 const uint8_t *end) {
    uint32_t size = (uint32_t)(end - start);
    fs_inode_t inode;
    uint32_t sample = 64;
    uint8_t buffer[64];

    if (size == 0) {
        printf("Embedded user program is empty: %s\n", path);
        return false;
    }

    if (fs_stat(path, &inode) && inode.type == 1 && inode.size == size) {
        if (size < sample) {
            sample = size;
        }
        if (sample > 0) {
            if (fs_read_file(path, buffer, sample, 0) == (int)sample &&
                memcmp(buffer, start, sample) == 0) {
                if (size == sample) {
                    return true;
                }
                if (fs_read_file(path, buffer, sample, size - sample) == (int)sample &&
                    memcmp(buffer, start + (size - sample), sample) == 0) {
                    return true;
                }
            }
        }
    }

    int file_res = fs_create_file(path);
    if (file_res < 0 && file_res != -2) {
        printf("Failed to create %s\n", path);
        return false;
    }

    int written = fs_write_file(path, start, size, 0);
    if (written != (int)size) {
        printf("Failed to write %s (%d/%u)\n", path, written, size);
        return false;
    }

    return true;
}

static const embedded_program_t embedded_programs[] = {
    {"/bin/hello.elf", _binary_hello_elf_start, _binary_hello_elf_end},
    {"/bin/cat.elf", _binary_cat_elf_start, _binary_cat_elf_end},
    {"/bin/execdemo.elf", _binary_execdemo_elf_start, _binary_execdemo_elf_end},
    {"/bin/statdemo.elf", _binary_statdemo_elf_start, _binary_statdemo_elf_end},
    {"/bin/ls.elf", _binary_ls_elf_start, _binary_ls_elf_end},
    {"/bin/rm.elf", _binary_rm_elf_start, _binary_rm_elf_end},
    {"/bin/mkdir.elf", _binary_mkdir_elf_start, _binary_mkdir_elf_end},
    {"/bin/touch.elf", _binary_touch_elf_start, _binary_touch_elf_end},
    {"/bin/pwd.elf", _binary_pwd_elf_start, _binary_pwd_elf_end},
    {"/bin/echo.elf", _binary_echo_elf_start, _binary_echo_elf_end},
    {"/bin/reverse.elf", _binary_reverse_elf_start, _binary_reverse_elf_end},
    {"/bin/strlen.elf", _binary_strlen_elf_start, _binary_strlen_elf_end},
    {"/bin/upper.elf", _binary_upper_elf_start, _binary_upper_elf_end},
    {"/bin/lower.elf", _binary_lower_elf_start, _binary_lower_elf_end},
    {"/bin/calc.elf", _binary_calc_elf_start, _binary_calc_elf_end},
    {"/bin/draw.elf", _binary_draw_elf_start, _binary_draw_elf_end},
    {"/bin/banner.elf", _binary_banner_elf_start, _binary_banner_elf_end},
    {"/bin/clear.elf", _binary_clear_elf_start, _binary_clear_elf_end},
    {"/bin/color.elf", _binary_color_elf_start, _binary_color_elf_end},
    {"/bin/colors.elf", _binary_colors_elf_start, _binary_colors_elf_end},
    {"/bin/write.elf", _binary_write_elf_start, _binary_write_elf_end},
    {"/bin/history.elf", _binary_history_elf_start, _binary_history_elf_end},
    {"/bin/cd.elf", _binary_cd_elf_start, _binary_cd_elf_end},
    {"/bin/help.elf", _binary_help_elf_start, _binary_help_elf_end},
    {"/bin/about.elf", _binary_about_elf_start, _binary_about_elf_end},
    {"/bin/sysinfo.elf", _binary_sysinfo_elf_start, _binary_sysinfo_elf_end},
    {"/bin/uptime.elf", _binary_uptime_elf_start, _binary_uptime_elf_end},
    {"/bin/randcolor.elf", _binary_randcolor_elf_start, _binary_randcolor_elf_end},
    {"/bin/rainbow.elf", _binary_rainbow_elf_start, _binary_rainbow_elf_end},
    {"/bin/art.elf", _binary_art_elf_start, _binary_art_elf_end},
    {"/bin/fortune.elf", _binary_fortune_elf_start, _binary_fortune_elf_end},
    {"/bin/animate.elf", _binary_animate_elf_start, _binary_animate_elf_end},
    {"/bin/matrix.elf", _binary_matrix_elf_start, _binary_matrix_elf_end},
    {"/bin/guess.elf", _binary_guess_elf_start, _binary_guess_elf_end},
    {"/bin/rps.elf", _binary_rps_elf_start, _binary_rps_elf_end},
    {"/bin/tictactoe.elf", _binary_tictactoe_elf_start, _binary_tictactoe_elf_end},
    {"/bin/hangman.elf", _binary_hangman_elf_start, _binary_hangman_elf_end},
    {"/bin/timer.elf", _binary_timer_elf_start, _binary_timer_elf_end},
    {"/bin/alias.elf", _binary_alias_elf_start, _binary_alias_elf_end},
    {"/bin/unalias.elf", _binary_unalias_elf_start, _binary_unalias_elf_end},
    {"/bin/aliases.elf", _binary_aliases_elf_start, _binary_aliases_elf_end},
    {"/bin/theme.elf", _binary_theme_elf_start, _binary_theme_elf_end},
    {"/bin/beep.elf", _binary_beep_elf_start, _binary_beep_elf_end},
    {"/bin/soundtest.elf", _binary_soundtest_elf_start, _binary_soundtest_elf_end},
    {"/bin/mixer.elf", _binary_mixer_elf_start, _binary_mixer_elf_end},
    {"/bin/halt.elf", _binary_halt_elf_start, _binary_halt_elf_end},
    {"/bin/run.elf", _binary_run_elf_start, _binary_run_elf_end},
    {"/bin/rmdir.elf", _binary_rmdir_elf_start, _binary_rmdir_elf_end},
    {"/bin/gfx.elf", _binary_gfx_elf_start, _binary_gfx_elf_end},
    {"/bin/gfxanim.elf", _binary_gfxanim_elf_start, _binary_gfxanim_elf_end},
    {"/bin/gfxpaint.elf", _binary_gfxpaint_elf_start, _binary_gfxpaint_elf_end},
    {"/bin/gui.elf", _binary_gui_elf_start, _binary_gui_elf_end},
    {"/bin/guipaint.elf", _binary_guipaint_elf_start, _binary_guipaint_elf_end},
    {"/bin/guicalc.elf", _binary_guicalc_elf_start, _binary_guicalc_elf_end},
    {"/bin/guifilemgr.elf", _binary_guifilemgr_elf_start, _binary_guifilemgr_elf_end},
    {"/bin/desktop.elf", _binary_desktop_elf_start, _binary_desktop_elf_end},
    {"/bin/forktest.elf", _binary_forktest_elf_start, _binary_forktest_elf_end},
    {"/bin/schedtest.elf", _binary_schedtest_elf_start, _binary_schedtest_elf_end},
    {"/bin/fault.elf", _binary_fault_elf_start, _binary_fault_elf_end},
    {"/bin/abi_test.elf", _binary_abi_test_elf_start, _binary_abi_test_elf_end},
};

static int embedded_program_count(void) {
    return (int)(sizeof(embedded_programs) / sizeof(embedded_programs[0]));
}

bool user_program_install_if_embedded(const char *path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    if (path[0] == '/' && path[1] == 'b' && path[2] == 'i' &&
        path[3] == 'n' && path[4] == '/') {
        fs_create_dir("/bin");
    }
    int count = embedded_program_count();
    for (int i = 0; i < count; i++) {
        if (strcmp(path, embedded_programs[i].path) == 0) {
            return install_user_program(embedded_programs[i].path,
                                        embedded_programs[i].start,
                                        embedded_programs[i].end);
        }
    }
    return false;
}

static const char user_bin_stamp[] = "userbin:v1:RohanOS-0.3";

static bool __attribute__((unused)) user_bins_up_to_date(void) {
    fs_inode_t inode;
    uint32_t stamp_len = (uint32_t)strlen(user_bin_stamp);
    char buf[64];

    if (!fs_stat("/bin/.installed", &inode) || inode.type != 1) {
        return false;
    }
    if (inode.size != stamp_len || stamp_len >= sizeof(buf)) {
        return false;
    }
    if (fs_read_file("/bin/.installed", (uint8_t *)buf, stamp_len, 0) != (int)stamp_len) {
        return false;
    }
    buf[stamp_len] = '\0';
    return strcmp(buf, user_bin_stamp) == 0;
}

static void __attribute__((unused)) write_user_bin_stamp(void) {
    fs_create_file("/bin/.installed");
    fs_write_file("/bin/.installed", (const uint8_t *)user_bin_stamp,
                  (uint32_t)strlen(user_bin_stamp), 0);
}

static void __attribute__((unused)) install_user_programs(void) {
    int dir_res = fs_create_dir("/bin");
    if (dir_res < 0 && dir_res != -2) {
        printf("Failed to create /bin\n");
        return;
    }

    if (user_bins_up_to_date()) {
        return;
    }

    printf("Syncing /bin user programs...\n");
    int count = embedded_program_count();
    for (int i = 0; i < count; i++) {
        install_user_program(embedded_programs[i].path,
                             embedded_programs[i].start,
                             embedded_programs[i].end);
    }

    write_user_bin_stamp();
}

static inline bool are_interrupts_enabled()
{
    unsigned long flags;
    asm volatile ( "pushf\n\t"
                   "pop %0"
                   : "=g"(flags) );
    return flags & (1 << 9);
}

static bool dma_boot_override = false;

static char tolower_ascii(char c) {
	if (c >= 'A' && c <= 'Z') {
		return (char)(c + ('a' - 'A'));
	}
	return c;
}

static bool has_prefix(const char *str, const char *prefix) {
	size_t i = 0;
	for (; prefix[i] != '\0'; i++) {
		if (str[i] == '\0' || str[i] != prefix[i]) {
			return false;
		}
	}
	return true;
}

static bool parse_dma_setting(const char *buf, bool *enabled_out) {
	const char *p = buf;
	while (p && *p) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
			p++;
		}
		if (has_prefix(p, "dma=")) {
			p += 4;
			char value[8];
			size_t idx = 0;
			while (*p && *p != '\n' && *p != '\r' && *p != ' ' && idx + 1 < sizeof(value)) {
				value[idx++] = tolower_ascii(*p++);
			}
			value[idx] = '\0';
			if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0 || strcmp(value, "true") == 0) {
				*enabled_out = true;
				return true;
			}
			if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0 || strcmp(value, "false") == 0) {
				*enabled_out = false;
				return true;
			}
		}
		while (*p && *p != '\n') {
			p++;
		}
		if (*p == '\n') {
			p++;
		}
	}
	return false;
}

static void boot_apply_dma_config(void) {
	if (dma_boot_override) {
		return;
	}
	fs_context_t *fs = fs_get_context();
	if (!fs || !fs->mounted) {
		return;
	}
	char buffer[128];
	int read = fs_read_file("/etc/boot.cfg", (uint8_t *)buffer, sizeof(buffer) - 1, 0);
	if (read <= 0) {
		return;
	}
	buffer[read] = '\0';
	bool enabled = false;
	if (parse_dma_setting(buffer, &enabled)) {
		ata_set_dma_enabled(enabled);
		printf("ATA DMA %s (from /etc/boot.cfg)\n", enabled ? "enabled" : "disabled");
	}
}

static void boot_dma_toggle_prompt(void) {
    const int timeout_ms = 1500;
    printf("Boot option: press 'D' to toggle ATA DMA (currently %s)...\n",
           ata_dma_is_enabled() ? "on" : "off");
    keyboard_clear_buffer();
    int remaining = timeout_ms;
    while (remaining > 0) {
        if (keyboard_has_input()) {
            unsigned char c = keyboard_getchar();
            if (c == 'd' || c == 'D') {
                bool enabled = !ata_dma_is_enabled();
                ata_set_dma_enabled(enabled);
                dma_boot_override = true;
                printf("ATA DMA %s (will validate on init)\n",
                       enabled ? "enabled" : "disabled");
            }
            break;
        }
        timer_sleep_ms(10);
        remaining -= 10;
    }
}

void kernel_main(void) {   
	__asm__ volatile ("cli"); // Keep interrupts off until IDT is installed.

	terminal_initialize();
    
	gdt_init();
    page_init();
	kpti_init();
	write_cr0(read_cr0() | CR0_WP);

	// Initialize kernel heap after paging is ready
	kmalloc_init();
	process_init();
    
	idt_init();
    timer_init(100); // Initialize timer at 100 Hz (10ms per tick)
    task_scheduler_init(); // Initialize kernel task scheduler
    keyboard_init();
    mouse_init();
    graphics_init();
	idt_init();
	pic_disable();
	__asm__ volatile ("sti");
	IRQ_clear_mask(0);  // Timer
	IRQ_clear_mask(1);  // Keyboard
	IRQ_clear_mask(2);  // Cascade (needed for IRQ12)
	IRQ_clear_mask(12); // Mouse
    boot_dma_toggle_prompt();
    ata_init();
    fs_init();
	net_init();
	audio_init();

    

    printf("RohanOS Version 0.3\n");
    printf("Interrupts are: ");
    printf(are_interrupts_enabled()?"enabled\n":"disabled\n");
    printf("Keyboard initialized.\n");
    printf("Mouse initialized. (Scroll to navigate history)\n");
    
    // Auto-mount primary disk (drive 0)
    printf("Mounting disk filesystem...\n");
    ata_device_t *drive = ata_get_device(0);
    if (drive) {
        // Try to mount existing filesystem
        if (!fs_mount(0)) {
            // If mount fails, format and mount
            printf("No filesystem found. Formatting disk...\n");
            if (fs_format(0)) {
                if (fs_mount(0)) {
                    printf("Disk formatted and mounted successfully!\n");
                    boot_apply_dma_config();
                    
                    // Create a welcome file
                    fs_create_file("welcome.txt");
                    const char *welcome = "Welcome to RohanOS!\n\nYour files are now stored on disk and will persist between reboots.\n\nTry these commands:\n  ls - list files\n  cat welcome.txt - read this file\n  write <file> <text> - create a file\n  rm <file> - delete a file\n  run /bin/hello.elf - run a user program\n  run /bin/execdemo.elf /bin/hello.elf hi\n";
                    fs_write_file("welcome.txt", (const uint8_t*)welcome, strlen(welcome), 0);

                    fs_create_dir("/bin");
                    create_sample_images();
                } else {
                    printf("Failed to mount after format\n");
                }
            } else {
                printf("Failed to format disk\n");
            }
        } else {
            printf("Disk mounted successfully!\n");
            boot_apply_dma_config();
            fs_create_dir("/bin");
            create_sample_images();
        }
    } else {
        printf("Warning: No disk drive detected. File operations will be limited.\n");
    }
    

    shell_init();
    

    while(1) {
        __asm__ volatile ("hlt");
    }
}
    
