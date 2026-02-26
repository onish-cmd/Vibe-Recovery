#include "../limine-10.7.0/limine-protocol/include/limine.h"
#include "framebuffer/fb.h"
#include "keyboard.h"
#include "printf.h"
#include "spleen.h"
#include <stddef.h>
#include <stdint.h>

void putchar_(char c) { _putchar(c); }

void scroll_screen(uint32_t amount);

__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_base_revision[] = {0xf95623d0d61a4821, 0x27a147040228308d, 0};

// 1. Request a Framebuffer (for your Mi TV)
static volatile struct limine_framebuffer_request fb_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

// 2. Request Memory Map (To find your 16GB DDR5)
static volatile struct limine_memmap_request mem_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0};

void _putchar(char c) { fb_putchar(c); }

uint32_t *fb_ptr;
struct limine_framebuffer *fb;

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
  uint32_t address;
  uint32_t lbus = (uint32_t)bus;
  uint32_t lslot = (uint32_t)slot;
  uint32_t lfunc = (uint32_t)func;

  /* Create configuration address:
   * Bit 31: Enable bit (1)
   * Bits 23-16: Bus
   * Bits 15-11: Slot
   * Bits 10-8: Function
   * Bits 7-2: Register Offset
   */
  address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                       (offset & 0xfc) | ((uint32_t)0x80000000));

  // Write address to 0xCF8
  __asm__ volatile("outl %0, %1" : : "a"(address), "Nd"((uint16_t)0xCF8));

  // Read data from 0xCFC
  uint32_t data;
  __asm__ volatile("inl %1, %0" : "=a"(data) : "Nd"((uint16_t)0xCFC));

  return data;
}

uint64_t pci_get_bar0(uint8_t bus, uint8_t slot, uint8_t func) {
  // BAR0 is at offset 0x10. It's 64-bit on modern systems (Q35/B650).
  uint32_t bar_low = pci_read(bus, slot, func, 0x10);
  uint32_t bar_high = pci_read(bus, slot, func, 0x14);

  // Mask out the bottom 4 bits (they contain metadata, not the address)
  uint64_t address = ((uint64_t)bar_high << 32) | (bar_low & ~0xF);
  return address;
}

void scan_pci_bus() {
  printf("Scanning PCI Bus for Storage Devices...\n");

  for (int bus = 0; bus < 256; bus++) {
    for (int slot = 0; slot < 32; slot++) {
      // Most devices are function 0, but some are multi-function
      for (int func = 0; func < 8; func++) {
        uint32_t vendor_device = pci_read(bus, slot, func, 0x00);
        uint16_t vendor = vendor_device & 0xFFFF;

        if (vendor == 0xFFFF)
          continue; // Empty slot

        // Read Class/Subclass (Offset 0x08 in PCI config space)
        uint32_t class_data = pci_read(bus, slot, func, 0x08);
        uint8_t class_code = (class_data >> 24) & 0xFF;
        uint8_t subclass = (class_data >> 16) & 0xFF;
        uint8_t prog_if = (class_data >> 8) & 0xFF;

        if (class_code == 0x01) {   // Mass Storage Controller
          term.fg_color = 0x9ece6a; // Tokyo Green
          if (subclass == 0x08 && prog_if == 0x02) {
            printf("[FOUND] NVMe Controller at %d:%d:%d\n", bus, slot, func);
          } else if (subclass == 0x06) {
            printf("[FOUND] SATA (AHCI) Controller at %d:%d:%d\n", bus, slot,
                   func);
          }
          term.fg_color = 0xc0caf5;
        }
      }
    }
  }
}

void print_memmap() {
  if (mem_req.response == NULL) {
    printf("[ERROR] Memory map request failed!\n");
    return;
  }

  uint64_t total_phys_ram = 0;
  uint64_t usable_ram = 0;

  for (uint64_t i = 0; i < mem_req.response->entry_count; i++) {
    struct limine_memmap_entry *entry = mem_req.response->entries[i];

    // Add EVERY entry to the total physical count
    total_phys_ram += entry->length;

    // Only add 'Usable' type to the usable count
    if (entry->type == LIMINE_MEMMAP_USABLE) {
      usable_ram += entry->length;
    }
  }

  // Now we print the discovered values
  printf("--- Memory Discovery ---\n");
  printf("Total Physical RAM: %u MB\n",
         (uint32_t)(total_phys_ram / 1024 / 1024));
  printf("Usable by Kernel:   %u MB\n", (uint32_t)(usable_ram / 1024 / 1024));

  uint64_t reserved = total_phys_ram - usable_ram;
  printf("Reserved/iGPU:      %u MB\n", (uint32_t)(reserved / 1024 / 1024));
}

static inline void cpuid(uint32_t code, uint32_t *a, uint32_t *b, uint32_t *c,
                         uint32_t *d) {
  __asm__ volatile("cpuid"
                   : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                   : "a"(code));
}

void check_cpu_features() {
  uint32_t eax, ebx, ecx, edx;

  // Get Vendor ID (Should be "AuthenticAMD")
  cpuid(0, &eax, &ebx, &ecx, &edx);
  char vendor[13];
  *(uint32_t *)(vendor) = ebx;
  *(uint32_t *)(vendor + 4) = edx;
  *(uint32_t *)(vendor + 8) = ecx;
  vendor[12] = '\0';

  printf("CPU Vendor: %s\n", vendor);
  uint32_t a = 7, c = 0;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(a), "c"(c));
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static inline uint64_t rdtsc();

void mandelbrot_test(int max_iter) {
  uint64_t start = rdtsc();
  int width = term.width;
  int height = term.height;

  // Total stress settings
  int64_t scale = 65536; // 16-bit fixed point
  int64_t high_threshold = 4 * scale;

  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      int64_t cx = ((int64_t)x - (width / 2) - 100) * 3 * scale / width;
      int64_t cy = ((int64_t)y - (height / 2)) * 3 * scale / height;

      int64_t zx = 0, zy = 0;
      int i;

      for (i = 0; i < max_iter; i++) {
        int64_t zx2 = (zx * zx) >> 16;
        int64_t zy2 = (zy * zy) >> 16;
        int64_t next_zx = zx2 - zy2 + cx;
        zy = ((zx * zy) >> 15) + cy;
        zx = next_zx;
      }
      uint32_t color;
      if ((zx * zx + zy * zy) >> 16 < high_threshold) {
        color = 0x1a1b26; // Original Blue (Inside)
      } else {
        color = 0xbb9af7; // Tokyo Night Purple (Outside/Escaped)
      }

      term.fb_ptr[y * term.stride + x] = color;
    }

    // Real-time Progress Bar (Updates every row)
    int bar_width = (y * width) / height;
    for (int i = 0; i < bar_width; i++) {
      // Drawing a 5-pixel tall bar at the very bottom
      for (int bh = 0; bh < 5; bh++) {
        term.fb_ptr[(height - 5 + bh) * term.stride + i] =
            0x7aa2f7; // Tokyo Night Blue
      }
    }
  }

  // reset to old color
  clear(term.bg_color);
  uint64_t end = rdtsc();
  uint64_t diff = end - start;

  // Split 64-bit for your printf
  uint32_t high = (uint32_t)(diff >> 32);
  uint32_t low = (uint32_t)(diff & 0xFFFFFFFF);
  uint64_t total_cycles = end - start;

  // 1 Billion = 1,000,000,000
  uint32_t billions = (uint32_t)(total_cycles / 1000000000);
  uint32_t remainder = (uint32_t)(total_cycles % 1000000000);

  if (billions > 0) {
    // print the billions, then the rest.
    printf("Total Cycles: %u Billion %u\n", billions, remainder);
  } else {
    printf("Total Cycles: %u\n", (uint32_t)total_cycles);
  }
}

static inline uint64_t rdtsc() {
  uint32_t lo, hi;
  // 'rdtsc' puts the low 32 bits in EAX and high 32 bits in EDX
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

void cmd_test_speed() {
  uint64_t start = rdtsc();

  // Do 10 million NOPs (No-Operations)
  for (volatile int i = 0; i < 10000000; i++) {
    __asm__ volatile("nop");
  }

  uint64_t end = rdtsc();
  printf("10M NOPs took: %u cycles\n", (uint32_t)(end - start));
}

void get_brand_string() {
  // 48 bytes + 1 for the null terminator
  char brand[49];
  uint32_t *ptr = (uint32_t *)brand;

  for (uint32_t i = 0; i < 3; i++) {
    uint32_t leaf = 0x80000002 + i;

    __asm__ volatile("cpuid"
                     : "=a"(ptr[i * 4 + 0]), "=b"(ptr[i * 4 + 1]),
                       "=c"(ptr[i * 4 + 2]), "=d"(ptr[i * 4 + 3])
                     : "a"(leaf));
  }

  brand[48] = '\0';
  char *final_string = brand;
  while (*final_string == ' ')
    final_string++;

  printf("Processor: %s\n", final_string);
}

uint64_t hex_to_int(const char *s) {
  uint64_t res = 0;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    s += 2;

  while (*s) {
    uint8_t val = 0;
    if (*s >= '0' && *s <= '9')
      val = *s - '0';
    else if (*s >= 'a' && *s <= 'f')
      val = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'F')
      val = *s - 'A' + 10;
    else
      break;

    res = (res << 4) | val;
    s++;
  }
  return res;
}

int atoi(const char *s) {
  int res = 0;
  int sign = 1;

  // Handle negative numbers if needed
  if (*s == '-') {
    sign = -1;
    s++;
  }

  while (*s >= '0' && *s <= '9') {
    res = res * 10 + (*s - '0');
    s++;
  }

  return res * sign;
}

void vibe_shell() {
  char buffer[64];
  int idx = 0;

  printf("\nvibe> ");

  while (1) {
    char c = get_char();
    if (c == 0)
      continue; // No key pressed

    if (c == '\n' || idx == 63) {
      buffer[idx] = '\0';
      // COMMAND LOGIC
      char *cmd = buffer;
      char *arg1 = 0;
      char *arg2 = 0;

      // Find first space to split command from arg1
      for (int i = 0; buffer[i]; i++) {
        if (buffer[i] == ' ') {
          buffer[i] = '\0';
          arg1 = &buffer[i + 1];
          break;
        }
      }

      // Find second space to split arg1 from arg2
      if (arg1) {
        for (int i = 0; arg1[i]; i++) {
          if (arg1[i] == ' ') {
            arg1[i] = '\0';
            arg2 = &arg1[i + 1];
            break;
          }
        }
      }
      printf("\n");

      // COMMAND LOGIC
      if (strcmp(buffer, "find") == 0) {
        scan_pci_bus();
      } else if (strcmp(buffer, "info") == 0) {
        check_cpu_features();
        get_brand_string();
        print_memmap();
      } else if (strcmp(buffer, "test-cpu") == 0) {
        mandelbrot_test(atoi(arg1));
        printf("Speed Test (10M NOPs)\n\n");
        cmd_test_speed();
      } else if (strcmp(buffer, "clear") == 0) {
        if (arg1) {
          // Parse the color from the first argument
          uint32_t custom_color = (uint32_t)hex_to_int(arg1);

          clear(custom_color);
        } else {
          clear(term.bg_color);
        }
      } else if (strcmp(buffer, "shutdown") == 0) {
        printf("It is now safe to power off your computer.");
        while (1) {
          __asm__("hlt");
        }
      } else {
        printf("Unknown command: %s", buffer);
      }

      // Reset for next command
      idx = 0;
      printf("\nvibe> ");
    } else if (c == '\b') {
      if (idx > 0) {
        idx--;
        printf("%c", c);
      }
    } else {
      // Echo the character to your Mi TV
      buffer[idx++] = c;
      printf("%c", c);
    }
  }
}

void _start(void) {
  // Check if Limine gave us a screen
  if (fb_req.response == NULL || fb_req.response->framebuffer_count < 1) {
    for (;;)
      __asm__("hlt"); // Halt if no screen
  }

  fb = fb_req.response->framebuffers[0];
  fb_ptr = fb->address;

  fb_init(fb, (void *)font_psf);

  // --- RECOVERY SHELL UI (Tokyo Night Style) ---
  // Fill screen with #1a1b26 (Dark Blue/Grey)
  clear(0x1a1b26);

  printf("Welcome to Vibe Recovery.\n\n");
  vibe_shell();

  for (;;) {
    __asm__("hlt");
  }
}
