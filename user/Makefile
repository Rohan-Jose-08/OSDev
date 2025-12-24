DEFAULT_HOST!=../default-host.sh
HOST?=$(DEFAULT_HOST)

CC=$(HOST)-gcc
LD=$(HOST)-ld
AR=$(HOST)-ar

INCLUDE_DIR=include
LIBC_DIR=libc
LIBGUI_DIR=libgui
APP_DIR=apps
BUILD_DIR=build

USER_CFLAGS?=-ffreestanding -fno-pic -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra
USER_CFLAGS+=-I$(INCLUDE_DIR)
USER_LDFLAGS?=-nostdlib -Wl,-T,linker.ld -Wl,-N

APPS=hello cat execdemo statdemo ls rm mkdir touch pwd echo reverse strlen upper lower calc draw banner clear color colors write history cd help about sysinfo uptime randcolor rainbow art fortune animate matrix guess rps tictactoe hangman timer alias unalias aliases theme beep halt run rmdir gfx gfxanim gfxpaint gui guipaint guicalc guifilemgr desktop
APP_ELF=$(addsuffix .elf,$(APPS))
APP_BLOB=$(addsuffix _blob.o,$(APPS))
APP_OBJS=$(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(APPS)))

LIBC_OBJS=\
$(BUILD_DIR)/crt0.o \
$(BUILD_DIR)/unistd.o \
$(BUILD_DIR)/stat.o \
$(BUILD_DIR)/string.o \
$(BUILD_DIR)/stdio.o \
$(BUILD_DIR)/stdlib.o \
$(BUILD_DIR)/graphics.o \
$(BUILD_DIR)/mouse.o \

LIBGUI_OBJS=\
$(BUILD_DIR)/uwm.o \
$(BUILD_DIR)/gui_window.o \
$(BUILD_DIR)/font.o \
$(BUILD_DIR)/calculator.o \
$(BUILD_DIR)/paint.o \
$(BUILD_DIR)/file_manager.o \
$(BUILD_DIR)/editor.o \
$(BUILD_DIR)/file_dialog.o \
$(BUILD_DIR)/gui_apps.o \

LIBU_OBJS=$(LIBC_OBJS) $(LIBGUI_OBJS)

LIBC_A=$(BUILD_DIR)/libu.a

.PHONY: all clean

all: $(APP_BLOB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/crt0.o: $(LIBC_DIR)/crt0.S | $(BUILD_DIR)
	$(CC) -c $< -o $@ $(USER_CFLAGS)

$(BUILD_DIR)/%.o: $(LIBC_DIR)/%.c | $(BUILD_DIR)
	$(CC) -c $< -o $@ $(USER_CFLAGS)

$(BUILD_DIR)/%.o: $(LIBGUI_DIR)/%.c | $(BUILD_DIR)
	$(CC) -c $< -o $@ $(USER_CFLAGS)

$(LIBC_A): $(LIBU_OBJS)
	$(AR) rcs $@ $(LIBU_OBJS)

$(BUILD_DIR)/%.o: $(APP_DIR)/%.c | $(BUILD_DIR)
	$(CC) -c $< -o $@ $(USER_CFLAGS)

%.elf: $(BUILD_DIR)/%.o $(LIBC_A) linker.ld
	$(CC) -o $@ $(BUILD_DIR)/crt0.o $(BUILD_DIR)/$*.o $(LIBC_A) $(USER_LDFLAGS)

%_blob.o: %.elf
	$(LD) -r -b binary -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(APP_ELF) $(APP_BLOB)
