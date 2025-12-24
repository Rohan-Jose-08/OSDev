DEFAULT_HOST!=../default-host.sh
HOST?=DEFAULT_HOST
HOSTARCH!=../target-triplet-to-arch.sh $(HOST)

CFLAGS?=-O2 -g
CPPFLAGS?=
LDFLAGS?=
LIBS?=

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
BOOTDIR?=$(EXEC_PREFIX)/boot
INCLUDEDIR?=$(PREFIX)/include

CFLAGS:=$(CFLAGS) -ffreestanding -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -D__is_kernel -Iinclude 
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) -nostdlib -lk -lgcc

ARCHDIR=arch/$(HOSTARCH)

USERDIR=../user
USER_BLOB=$(USERDIR)/hello_blob.o $(USERDIR)/cat_blob.o $(USERDIR)/execdemo_blob.o $(USERDIR)/statdemo_blob.o $(USERDIR)/ls_blob.o $(USERDIR)/rm_blob.o $(USERDIR)/mkdir_blob.o $(USERDIR)/touch_blob.o $(USERDIR)/pwd_blob.o $(USERDIR)/echo_blob.o $(USERDIR)/reverse_blob.o $(USERDIR)/strlen_blob.o $(USERDIR)/upper_blob.o $(USERDIR)/lower_blob.o $(USERDIR)/calc_blob.o $(USERDIR)/draw_blob.o $(USERDIR)/banner_blob.o $(USERDIR)/clear_blob.o $(USERDIR)/color_blob.o $(USERDIR)/colors_blob.o $(USERDIR)/write_blob.o $(USERDIR)/history_blob.o $(USERDIR)/cd_blob.o $(USERDIR)/help_blob.o $(USERDIR)/about_blob.o $(USERDIR)/sysinfo_blob.o $(USERDIR)/uptime_blob.o $(USERDIR)/randcolor_blob.o $(USERDIR)/rainbow_blob.o $(USERDIR)/art_blob.o $(USERDIR)/fortune_blob.o $(USERDIR)/animate_blob.o $(USERDIR)/matrix_blob.o $(USERDIR)/guess_blob.o $(USERDIR)/rps_blob.o $(USERDIR)/tictactoe_blob.o $(USERDIR)/hangman_blob.o $(USERDIR)/timer_blob.o $(USERDIR)/alias_blob.o $(USERDIR)/unalias_blob.o $(USERDIR)/aliases_blob.o $(USERDIR)/theme_blob.o $(USERDIR)/beep_blob.o $(USERDIR)/halt_blob.o $(USERDIR)/run_blob.o $(USERDIR)/rmdir_blob.o $(USERDIR)/gfx_blob.o $(USERDIR)/gfxanim_blob.o $(USERDIR)/gfxpaint_blob.o $(USERDIR)/gui_blob.o $(USERDIR)/guipaint_blob.o $(USERDIR)/guicalc_blob.o $(USERDIR)/guifilemgr_blob.o $(USERDIR)/desktop_blob.o

ASFLAGS?=
ASFLAGS:=$(ASFLAGS) -f elf

# Add rules for assembling .S files with nasm
$(ARCHDIR)/exception.o: $(ARCHDIR)/exception.S
	nasm -o $@ $(ASFLAGS) $<

$(ARCHDIR)/irq.o: $(ARCHDIR)/irq.S
	nasm -o $@ $(ASFLAGS) $<

$(ARCHDIR)/context_switch.o: $(ARCHDIR)/context_switch.S
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

$(ARCHDIR)/cpu.o: $(ARCHDIR)/cpu.S
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

# Update the KERNEL_OBJS list to include the new files
KERNEL_OBJS=\
$(KERNEL_ARCH_OBJS) \
kernel/kernel.o \
kernel/pic.o \
kernel/shell.o \
kernel/editor.o \
kernel/snake.o \
kernel/graphics_demo.o \
kernel/paint.o \
kernel/task.o \
kernel/fs.o \
kernel/syscall.o \
kernel/elf.o \
kernel/usermode.o \
kernel/kmalloc.o \
kernel/window.o \
kernel/desktop.o \
kernel/menu_bar.o \
kernel/paint_windowed.o \
kernel/calculator.o \
kernel/file_manager.o \
kernel/file_dialog.o \
$(USER_BLOB) \




include $(ARCHDIR)/make.config

CFLAGS:=$(CFLAGS) $(KERNEL_ARCH_CFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(KERNEL_ARCH_CPPFLAGS)
LDFLAGS:=$(LDFLAGS) $(KERNEL_ARCH_LDFLAGS)
LIBS:=$(LIBS) $(KERNEL_ARCH_LIBS)

OBJS=\
$(ARCHDIR)/crti.o \
$(ARCHDIR)/crtbegin.o \
$(KERNEL_OBJS) \
$(ARCHDIR)/crtend.o \
$(ARCHDIR)/crtn.o \




LINK_LIST=\
$(LDFLAGS) \
$(ARCHDIR)/crti.o \
$(ARCHDIR)/crtbegin.o \
$(KERNEL_OBJS) \
$(LIBS) \
$(ARCHDIR)/crtend.o \
$(ARCHDIR)/crtn.o 

CPPFLAGS:=$(CPPFLAGS) -Ikernel/include/kernel 


.PHONY: all clean install install-headers install-kernel
.SUFFIXES: .o .c .S

all: myos.kernel

$(USER_BLOB):
	$(MAKE) -C $(USERDIR) hello_blob.o cat_blob.o execdemo_blob.o statdemo_blob.o ls_blob.o rm_blob.o mkdir_blob.o touch_blob.o pwd_blob.o echo_blob.o reverse_blob.o strlen_blob.o upper_blob.o lower_blob.o calc_blob.o draw_blob.o banner_blob.o clear_blob.o color_blob.o colors_blob.o write_blob.o history_blob.o cd_blob.o help_blob.o about_blob.o sysinfo_blob.o uptime_blob.o randcolor_blob.o rainbow_blob.o art_blob.o fortune_blob.o animate_blob.o matrix_blob.o guess_blob.o rps_blob.o tictactoe_blob.o hangman_blob.o timer_blob.o alias_blob.o unalias_blob.o aliases_blob.o theme_blob.o beep_blob.o halt_blob.o run_blob.o rmdir_blob.o gfx_blob.o gfxanim_blob.o gfxpaint_blob.o gui_blob.o guipaint_blob.o guicalc_blob.o guifilemgr_blob.o desktop_blob.o HOST=$(HOST)

myos.kernel: $(OBJS) $(ARCHDIR)/linker.ld
	$(CC) -T $(ARCHDIR)/linker.ld -o $@ $(CFLAGS) $(LINK_LIST)
	grub-file --is-x86-multiboot myos.kernel

$(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o:
	OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@

.c.o:
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS) $(CPPFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

clean:
	rm -f myos.kernel
	rm -f $(OBJS) *.o */*.o */*/*.o
	rm -f $(OBJS:.o=.d) *.d */*.d */*/*.d
	$(MAKE) -C $(USERDIR) clean

install: install-headers install-kernel

install-headers:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp -R --preserve=timestamps include/. $(DESTDIR)$(INCLUDEDIR)/.

install-kernel: myos.kernel
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp myos.kernel $(DESTDIR)$(BOOTDIR)

-include $(OBJS:.o=.d)
