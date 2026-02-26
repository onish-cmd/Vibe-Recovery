# --- Project Configuration ---
KERNEL     := vibe_kernel.elf
ISO        := vibe_recovery.iso
LIMINE_DIR := ./limine-10.7.0
BIN_DIR    := ./limine
SRC_DIR    := src

# --- Tools ---
CC      := clang
LD      := ld.lld

# --- Flags ---
# Added -I./src/framebuffer so the compiler can find fb.h easily
CFLAGS := --target=x86_64-elf -std=c11 -ffreestanding \
          -Wall -Wextra -O2 -pipe -fno-stack-protector \
          -fno-stack-check -fno-lto -fno-pie -fno-pic \
          -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
          -mno-red-zone -mcmodel=kernel -I. -I$(SRC_DIR) -I$(SRC_DIR)/framebuffer

LDFLAGS := -nostdlib -static -m elf_x86_64 -z max-page-size=0x1000 \
           -T linker.lds

# --- Objects ---
# Included fb.o to fix the "Undefined Symbol" errors
OBJS := kernel.o printf.o fb.o

# --- Rules ---

all: $(ISO)

# Link the kernel
$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $(KERNEL)

# Compile Kernel 
kernel.o: $(SRC_DIR)/kernel.c src/spleen.h $(SRC_DIR)/framebuffer/fb.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/kernel.c -o kernel.o

# Compile Framebuffer / PSF2 Renderer
fb.o: $(SRC_DIR)/framebuffer/fb.c $(SRC_DIR)/framebuffer/fb.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/framebuffer/fb.c -o fb.o

# Special rule for printf
printf.o: $(SRC_DIR)/printf.c
	$(CC) $(CFLAGS) \
	-DPRINTF_DISABLE_SUPPORT_FLOAT \
	-DPRINTF_DISABLE_SUPPORT_EXPONENTIAL \
	-DPRINTF_DISABLE_SUPPORT_LONG_LONG \
	-c $(SRC_DIR)/printf.c -o printf.o

# Create the bootable ISO
$(ISO): $(KERNEL)
	mkdir -p iso_root
	cp $(KERNEL) limine.conf iso_root/
	cp $(BIN_DIR)/limine-bios.sys \
	   $(BIN_DIR)/limine-bios-cd.bin \
	   $(BIN_DIR)/limine-uefi-cd.bin iso_root/
	mkdir -p iso_root/EFI/BOOT
	cp $(BIN_DIR)/BOOTX64.EFI iso_root/EFI/BOOT/
	# Create the ISO image
	xorriso -as mkisofs -b limine-bios-cd.bin \
                -no-emul-boot -boot-load-size 4 -boot-info-table \
                --efi-boot limine-uefi-cd.bin \
                -efi-boot-part --efi-boot-image \
                --protective-msdos-label iso_root -o $(ISO)
	# Deploy Limine MBR
	$(BIN_DIR)/limine-binary bios-install $(ISO)

clean:
	rm -rf $(OBJS) $(KERNEL) $(ISO) iso_root

run: $(ISO)
	qemu-system-x86_64 -M q35 -m 1G \
	-drive file=$(ISO),format=raw \
	-drive file=nvme_disk.img,if=none,id=nvm1,format=raw \
	-device nvme,drive=nvm1,serial=VIBE001 \
	-vnc :1
