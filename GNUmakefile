# Nuke built-in rules.
.SUFFIXES:

# This is the name that our final executable will have.
# Change as needed.
override OUTPUT := wscope

# Keep all generated build artifacts under one directory.
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
ISO_ROOT := $(BUILD_DIR)/iso_root
ISO := $(BUILD_DIR)/image.iso
ISO_ROOT_STAMP := $(ISO_ROOT)/.stamp
LIMINE_DIR := $(BUILD_DIR)/limine-binary
LIMINE_TARBALL_URL := https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
GDB_SCRIPT := $(BUILD_DIR)/gdbinit
GENERATED_FONT := src/generated/font.h
LINKER_SCRIPT := config/linker.lds
LIMINE_CONFIG := config/limine.conf

# User controllable toolchain and toolchain prefix.
TOOLCHAIN :=
TOOLCHAIN_PREFIX :=
ifneq ($(TOOLCHAIN),)
    ifeq ($(TOOLCHAIN_PREFIX),)
        TOOLCHAIN_PREFIX := $(TOOLCHAIN)-
    endif
endif

# User controllable C compiler command.
ifneq ($(TOOLCHAIN_PREFIX),)
    CC := $(TOOLCHAIN_PREFIX)gcc
else
    CC := cc
endif

# User controllable linker command.
LD := $(TOOLCHAIN_PREFIX)ld

# User controllable ISO creation command.
XORRISO := xorriso

# User controllable emulator and debugger commands.
QEMU := qemu-system-x86_64
GDB := gdb
PYTHON := python3
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
GDB_HOST := localhost
GDB_PORT := 1234
QEMUFLAGS := -m 64M -cdrom "$(ISO)" -boot d -serial mon:stdio
QEMUDEBUGFLAGS := -s -S

USER_APP_NAME := init
USER_APP_DIR := src/userspace/app
USER_APP_BUILD_DIR := $(BUILD_DIR)/userspace
USER_APP_ELF := $(USER_APP_BUILD_DIR)/$(USER_APP_NAME).elf
USER_APP_OBJ := $(OBJ_DIR)/userspace/$(USER_APP_NAME).elf.o
USER_APP_LINKER := $(USER_APP_DIR)/linker.lds

# Defaults overrides for variables if using "llvm" as toolchain.
ifeq ($(TOOLCHAIN),llvm)
    CC := clang
    LD := ld.lld
    OBJCOPY := llvm-objcopy
endif

# User controllable C flags.
CFLAGS := -g -O2 -pipe

# User controllable C preprocessor flags. We set none by default.
CPPFLAGS :=

# User controllable nasm flags.
NASMFLAGS := -g

# User controllable linker flags. We set none by default.
LDFLAGS :=

# Check if CC is Clang.
override CC_IS_CLANG := $(shell ! $(CC) --version 2>/dev/null | grep -q '^Target: '; echo $$?)

# If the C compiler is Clang, set the target as needed.
ifeq ($(CC_IS_CLANG),1)
    override CC += \
        -target x86_64-unknown-none-elf
endif

# Internal C flags that should not be changed by the user.
override CFLAGS += \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-PIC \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -mabi=sysv \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -mcmodel=kernel

USER_CFLAGS := \
    -g \
    -O2 \
    -pipe \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-PIC \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -mabi=sysv \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -mcmodel=small

# Internal C preprocessor flags that should not be changed by the user.
override CPPFLAGS := \
    -I src \
    $(CPPFLAGS) \
    -MMD \
    -MP

USER_CPPFLAGS := \
    -I $(USER_APP_DIR) \
    -MMD \
    -MP

# Internal nasm flags that should not be changed by the user.
override NASMFLAGS := \
    -f elf64 \
    $(patsubst -g,-g -F dwarf,$(NASMFLAGS)) \
    -Wall

# Internal linker flags that should not be changed by the user.
override LDFLAGS += \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    --gc-sections \
    -T $(LINKER_SCRIPT)

# Use "find" to glob all *.c, *.S, and *.asm files in the tree and obtain the
# object and header dependency file names.
override SRCFILES := $(shell find -L src -type f 2>/dev/null | LC_ALL=C sort)
override KERNEL_SRCFILES := $(filter-out $(USER_APP_DIR)/%,$(SRCFILES))
override CFILES := $(filter %.c,$(KERNEL_SRCFILES))
override ASFILES := $(filter %.S,$(KERNEL_SRCFILES))
override NASMFILES := $(filter %.asm,$(KERNEL_SRCFILES))
override USER_APP_SRCFILES := $(filter $(USER_APP_DIR)/%,$(SRCFILES))
override USER_APP_CFILES := $(filter %.c,$(USER_APP_SRCFILES))
override USER_APP_ASFILES := $(filter %.S,$(USER_APP_SRCFILES))
override USER_APP_OBJECTS := $(addprefix $(OBJ_DIR)/,$(USER_APP_CFILES:.c=.user.c.o) $(USER_APP_ASFILES:.S=.user.S.o))
override OBJ := $(addprefix $(OBJ_DIR)/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o)) $(USER_APP_OBJ)
override HEADER_DEPS := $(addprefix $(OBJ_DIR)/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))
override USER_APP_HEADER_DEPS := $(USER_APP_OBJECTS:.o=.d)
override KERNEL := $(BIN_DIR)/$(OUTPUT)
override LIMINE := $(LIMINE_DIR)/limine
override LIMINE_FILES := \
    $(LIMINE_DIR)/limine-bios.sys \
    $(LIMINE_DIR)/limine-bios-cd.bin \
    $(LIMINE_DIR)/limine-uefi-cd.bin \
    $(LIMINE_DIR)/BOOTX64.EFI \
    $(LIMINE_DIR)/BOOTIA32.EFI
override ISO_INSTALLED_STAMP := $(ISO).limine-installed

# Default target. This must come first, before header dependencies.
.PHONY: all
all: $(KERNEL)

.PHONY: iso
iso: $(ISO_INSTALLED_STAMP)

.PHONY: limine
limine: $(LIMINE)

.PHONY: qemu
qemu: $(ISO_INSTALLED_STAMP)
	command -v "$(QEMU)" >/dev/null || { echo "error: qemu-system-x86_64 is required for 'make qemu'." >&2; exit 127; }
	$(QEMU) $(QEMUFLAGS)

.PHONY: debug
debug: $(ISO_INSTALLED_STAMP)
	command -v "$(QEMU)" >/dev/null || { echo "error: qemu-system-x86_64 is required for 'make debug'." >&2; exit 127; }
	$(QEMU) $(QEMUFLAGS) $(QEMUDEBUGFLAGS)

.PHONY: gdb
gdb: $(GDB_SCRIPT)
	command -v "$(GDB)" >/dev/null || { echo "error: gdb is required for 'make gdb'." >&2; exit 127; }
	$(GDB) -x "$(GDB_SCRIPT)"

# Include header dependencies.
-include $(HEADER_DEPS) $(USER_APP_HEADER_DEPS)

# Link rules for the final executable.
$(KERNEL): GNUmakefile $(LINKER_SCRIPT) $(OBJ)
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(OBJ) -o $@

# Compilation rules for *.c files.
$(OBJ_DIR)/%.c.o: %.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/src/framebuffer/framebuffer.c.o: $(GENERATED_FONT)

$(GENERATED_FONT): src/font/characters.json scripts/generate_font.py scripts/fontgen.py GNUmakefile
	$(PYTHON) scripts/generate_font.py src/font/characters.json "$@"

$(OBJ_DIR)/%.user.c.o: %.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(USER_CFLAGS) $(USER_CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/%.user.S.o: %.S GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(USER_CFLAGS) $(USER_CPPFLAGS) -c $< -o $@

$(USER_APP_ELF): $(USER_APP_LINKER) $(USER_APP_OBJECTS) GNUmakefile
	mkdir -p "$(dir $@)"
	$(LD) -m elf_x86_64 -nostdlib -static -T "$(USER_APP_LINKER)" $(USER_APP_OBJECTS) -o "$@"

$(USER_APP_OBJ): $(USER_APP_ELF)
	mkdir -p "$(dir $@)"
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --rename-section .data=.rodata.user.init,alloc,load,readonly,data,contents \
	    --redefine-sym _binary_build_userspace_init_elf_start=user_init_start \
	    --redefine-sym _binary_build_userspace_init_elf_end=user_init_end \
	    "$<" "$@"

# Compilation rules for *.S files.
$(OBJ_DIR)/%.S.o: %.S GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Compilation rules for *.asm (nasm) files.
$(OBJ_DIR)/%.asm.o: %.asm GNUmakefile
	mkdir -p "$(dir $@)"
	nasm $(NASMFLAGS) $< -o $@

.PHONY: iso-root
iso-root: $(ISO_ROOT_STAMP)

$(ISO_ROOT_STAMP): $(KERNEL) $(LIMINE_CONFIG) $(LIMINE_FILES) GNUmakefile
	rm -rf "$(ISO_ROOT)"
	mkdir -p "$(ISO_ROOT)/boot/limine" "$(ISO_ROOT)/EFI/BOOT"
	cp -v "$(KERNEL)" "$(ISO_ROOT)/boot/"
	cp -v "$(LIMINE_CONFIG)" "$(ISO_ROOT)/boot/limine/limine.conf"
	cp -v "$(LIMINE_DIR)/limine-bios.sys" "$(ISO_ROOT)/boot/limine/"
	cp -v "$(LIMINE_DIR)/limine-bios-cd.bin" "$(ISO_ROOT)/boot/limine/"
	cp -v "$(LIMINE_DIR)/limine-uefi-cd.bin" "$(ISO_ROOT)/boot/limine/"
	cp -v "$(LIMINE_DIR)/BOOTX64.EFI" "$(ISO_ROOT)/EFI/BOOT/"
	cp -v "$(LIMINE_DIR)/BOOTIA32.EFI" "$(ISO_ROOT)/EFI/BOOT/"
	touch "$@"

$(ISO): GNUmakefile $(ISO_ROOT_STAMP)
	command -v "$(XORRISO)" >/dev/null || { echo "error: xorriso is required for 'make iso'." >&2; exit 127; }
	$(XORRISO) -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
	    -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    "$(ISO_ROOT)" -o "$@"

$(ISO_INSTALLED_STAMP): $(ISO) $(LIMINE)
	"$(LIMINE)" bios-install "$(ISO)"
	touch "$@"

$(LIMINE_FILES): $(LIMINE)

$(LIMINE): $(LIMINE_DIR)/Makefile
	$(MAKE) -C "$(LIMINE_DIR)"

$(LIMINE_DIR)/Makefile:
	mkdir -p "$(BUILD_DIR)"
	if [ -f limine-binary/Makefile ]; then \
	    cp -a limine-binary "$(LIMINE_DIR)"; \
	else \
	    curl -L "$(LIMINE_TARBALL_URL)" | tar -xzf - -C "$(BUILD_DIR)"; \
	fi

$(GDB_SCRIPT): GNUmakefile $(KERNEL)
	mkdir -p "$(dir $@)"
	printf '%s\n' \
	    'file $(KERNEL)' \
	    'set architecture i386:x86-64' \
	    'break breakpoint' \
	    'target remote $(GDB_HOST):$(GDB_PORT)' \
	    > "$@"

# Remove object files and the final executable.
.PHONY: clean
clean:
	rm -rf "$(OBJ_DIR)" "$(BIN_DIR)" "$(USER_APP_BUILD_DIR)" "$(ISO_ROOT)" "$(ISO)" "$(ISO_INSTALLED_STAMP)" "$(GDB_SCRIPT)"

.PHONY: distclean
distclean: clean
	rm -rf "$(LIMINE_DIR)"
