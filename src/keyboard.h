#include <stdint.h>

uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ volatile("inb %w1, %b0" : "=a"(ret) : "Nd"(port));
  return ret;
}

// Global state to track shift
static int shift_pressed = 0;

char get_char() {
  while (!(inb(0x64) & 1))
    ;
  uint8_t scancode = inb(0x60);

  // 1. Check for Shift "Make" (Press) and "Break" (Release)
  if (scancode == 0x2A || scancode == 0x36) { // Left or Right Shift
    shift_pressed = 1;
    return 0;
  }
  if (scancode == 0xAA || scancode == 0xB6) { // Shift Released (Make + 0x80)
    shift_pressed = 0;
    return 0;
  }

  // 2. Ignore other "Break" codes
  if (scancode & 0x80)
    return 0;

  switch (scancode) {
  case 0x1C:
    return '\n'; // Enter
  case 0x01:
    return 27; // Escape
  case 0x0E:
    return '\b'; // Backspace
  case 0x39:
    return ' '; // Space
  case 0x0F:
    return '\t'; // Tab
  default:
    break;
  }

  // 3. Define two maps: Normal and Shifted
  static const char kbd_map_normal[] = {
      0,   0,   '1', '2', '3',  '4', '5', '6',  '7', '8', '9', '0',
      '-', '=', 0,   0,   'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
      'o', 'p', '[', ']', 0,    0,   'a', 's',  'd', 'f', 'g', 'h',
      'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
      'b', 'n', 'm', ',', '.',  '/', 0,   '*',  0,   ' '};

  static const char kbd_map_shift[] = {
      0,   0,   '!', '@', '#',  '$', '%', '^', '&', '*', '(', ')',
      '_', '+', 0,   0,   'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
      'O', 'P', '{', '}', 0,    0,   'A', 'S', 'D', 'F', 'G', 'H',
      'J', 'K', 'L', ':', '\"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
      'B', 'N', 'M', '<', '>',  '?', 0,   '*', 0,   ' '};

  // 4. Return from the correct map
  if (scancode < sizeof(kbd_map_normal)) {
    return shift_pressed ? kbd_map_shift[scancode] : kbd_map_normal[scancode];
  }

  return 0;
}
