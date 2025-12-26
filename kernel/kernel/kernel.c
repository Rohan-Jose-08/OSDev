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

#include <stdint.h>
#include <stddef.h> 

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
    scheduler_init(); // Initialize task scheduler
    keyboard_init();
    mouse_init();
    graphics_init();
    ata_init();
    fs_init();
	idt_init();
	pic_disable();
	__asm__ volatile ("sti");
	IRQ_clear_mask(0);  // Timer
	IRQ_clear_mask(1);  // Keyboard
	IRQ_clear_mask(2);  // Cascade (needed for IRQ12)
	IRQ_clear_mask(12); // Mouse

    

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
                    
                    // Create a welcome file
                    fs_create_file("welcome.txt");
                    const char *welcome = "Welcome to RohanOS!\n\nYour files are now stored on disk and will persist between reboots.\n\nTry these commands:\n  ls - list files\n  cat welcome.txt - read this file\n  write <file> <text> - create a file\n  rm <file> - delete a file\n  run /bin/hello.elf - run a user program\n  run /bin/execdemo.elf /bin/hello.elf hi\n";
                    fs_write_file("welcome.txt", (const uint8_t*)welcome, strlen(welcome), 0);

                    fs_create_dir("/bin");
                } else {
                    printf("Failed to mount after format\n");
                }
            } else {
                printf("Failed to format disk\n");
            }
        } else {
            printf("Disk mounted successfully!\n");
            fs_create_dir("/bin");
        }
    } else {
        printf("Warning: No disk drive detected. File operations will be limited.\n");
    }
    

    shell_init();
    

    while(1) {
        __asm__ volatile ("hlt");
    }
}
    
