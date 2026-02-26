#ifndef VIBE_FB_H
#define VIBE_FB_H

#include "../../limine-10.7.0/limine-protocol/include/limine.h"
#include <stddef.h>
#include <stdint.h>

#define PSF2_MAGIC 0x864ab572

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t headersize;
  uint32_t flags;
  uint32_t length;
  uint32_t charsize;
  uint32_t height;
  uint32_t width;
} psf2_header_t;

typedef struct {
  uint32_t *fb_ptr;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t cursor_x;
  uint32_t cursor_y;
  uint32_t fg_color;
  uint32_t bg_color;
  psf2_header_t *font;
} VibeTerm;

extern VibeTerm term;

void fb_init(struct limine_framebuffer *fb, void *font_data);
void clear(uint32_t color);
void fb_putchar(char c);
void fb_draw_char(char c, int cx, int cy, uint32_t fg);
void fb_flush_rect(int x, int y, int w, int h); // Speed fix
void fb_clear_cell(int cx, int cy);             // Erase fix

#endif
