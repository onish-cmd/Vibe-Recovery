#include "fb.h"

VibeTerm term;

// 1080p Backbuffer (BSS)
#define MAX_FB_PIXELS (1920 * 1080)
static uint32_t vibe_buffer[MAX_FB_PIXELS];
static uint32_t *backbuffer = vibe_buffer;

// Only copy the modified character box to the real screen
void fb_flush_rect(int x, int y, int w, int h) {
  if (backbuffer == term.fb_ptr)
    return;

  for (int i = y; i < y + h; i++) {
    for (int j = x; j < x + w; j++) {
      term.fb_ptr[i * term.stride + j] = backbuffer[i * term.stride + j];
    }
  }
}

void fb_init(struct limine_framebuffer *fb, void *font_ptr) {
  term.fb_ptr = (uint32_t *)fb->address;
  term.width = fb->width;
  term.height = fb->height;
  term.stride = fb->pitch / 4;
  term.font = (psf2_header_t *)font_ptr;

  if (term.width * term.height > MAX_FB_PIXELS) {
    backbuffer = term.fb_ptr;
  }

  term.cursor_x = 20;
  term.cursor_y = 20;
  term.fg_color = 0xc0caf5; // Tokyo Night
  term.bg_color = 0x1a1b26;

  clear(term.bg_color);
}

void fb_clear_cell(int cx, int cy) {
  uint32_t fw = term.font->width;
  uint32_t fh = term.font->height;

  for (uint32_t y = 0; y < fh; y++) {
    for (uint32_t x = 0; x < fw; x++) {
      backbuffer[(cy + y) * term.stride + (cx + x)] = term.bg_color;
    }
  }
  fb_flush_rect(cx, cy, fw, fh);
}

void fb_draw_char(char c, int cx, int cy, uint32_t fg) {
  // Use a base pointer so we don't lose the start of the glyph
  uint8_t *glyph_base =
      (uint8_t *)term.font + term.font->headersize + (c * term.font->charsize);
  uint32_t bytes_per_row = (term.font->width + 7) / 8;

  for (uint32_t y = 0; y < term.font->height; y++) {
    // Locate the start of the current row's data
    uint8_t *row = glyph_base + (y * bytes_per_row);

    for (uint32_t x = 0; x < term.font->width; x++) {
      // Pick the correct byte in the row (x / 8) and the correct bit (7 - (x %
      // 8))
      if ((row[x >> 3] >> (7 - (x & 7))) & 1) {
        backbuffer[(cy + y) * term.stride + (cx + x)] = fg;
      } else {
        backbuffer[(cy + y) * term.stride + (cx + x)] = term.bg_color;
      }
    }
  }
  fb_flush_rect(cx, cy, term.font->width, term.font->height);
}

void fb_scroll(uint32_t amount) {
  uint64_t pixels_to_copy = (uint64_t)(term.height - amount) * term.stride;
  for (uint64_t i = 0; i < pixels_to_copy; i++) {
    backbuffer[i] = backbuffer[i + (amount * term.stride)];
  }
  for (uint64_t i = pixels_to_copy; i < (uint64_t)term.height * term.stride;
       i++) {
    backbuffer[i] = term.bg_color;
  }

  // Scrolling moves everything, so we must do a full flush
  if (backbuffer != term.fb_ptr) {
    for (uint64_t i = 0; i < (uint64_t)term.height * term.stride; i++) {
      term.fb_ptr[i] = backbuffer[i];
    }
  }
}

void fb_putchar(char c) {
  uint32_t fw = term.font->width;
  uint32_t fh = term.font->height;

  if (c == '\n') {
    term.cursor_x = 20;
    term.cursor_y += fh;
  } else if (c == '\b') {
    if (term.cursor_x > 20) {
      term.cursor_x -= fw;
      fb_clear_cell(term.cursor_x, term.cursor_y);
    }
  } else {
    fb_draw_char(c, term.cursor_x, term.cursor_y, term.fg_color);
    term.cursor_x += fw;

    if (term.cursor_x + fw > term.width - 20) {
      term.cursor_x = 20;
      term.cursor_y += fh;
    }
  }

  if (term.cursor_y + fh > term.height - 20) {
    fb_scroll(fh);
    term.cursor_y -= fh;
  }
}

void clear(uint32_t color) {
  term.bg_color = color;
  term.cursor_x = 20;
  term.cursor_y = 20;
  uint64_t total = (uint64_t)term.height * term.stride;
  for (uint64_t i = 0; i < total; i++) {
    backbuffer[i] = color;
    if (backbuffer != term.fb_ptr)
      term.fb_ptr[i] = color;
  }
}
