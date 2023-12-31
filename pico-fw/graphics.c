#include "graphics.h"

#include "cga_8x8_font.h"
#include "mda_8x14_font.h"
#include "mda_9x14_font.h"

struct gfx_font {
  uint8_t *data;
  uint8_t cell_width, cell_height;
};

gfx_font_t gfx_mda_9x14_font = {
    .data = mda_9x14_font,
    .cell_width = 9,
    .cell_height = 14,
};

gfx_font_t gfx_mda_8x14_font = {
    .data = mda_8x14_font,
    .cell_width = 8,
    .cell_height = 14,
};

gfx_font_t gfx_cga_8x8_font = {
    .data = cga_8x8_font,
    .cell_width = 8,
    .cell_height = 8,
};

static uint8_t *gfx_frame_buffer;
static uint32_t gfx_frame_buffer_stride;

void gfx_set_frame_buffer(uint8_t *frame_buffer, uint32_t stride) {
  gfx_frame_buffer = frame_buffer;
  gfx_frame_buffer_stride = stride;
}

uint8_t *gfx_get_frame_buffer(void) { return gfx_frame_buffer; }
uint32_t gfx_get_frame_buffer_stride(void) { return gfx_frame_buffer_stride; }

inline void gfx_update_pixel(uint32_t x, uint32_t y, uint8_t v, gfx_operation_t op) {
  uint8_t *row = gfx_frame_buffer + (gfx_frame_buffer_stride * y);
  uint32_t byte_idx = x >> 2;
  uint32_t px_idx = x & 0x3;

  uint8_t p = row[byte_idx];

  uint8_t mask = 0x3 << (6 - (px_idx << 1));
  uint8_t shift_v = (v & 0x3) << (6 - (px_idx << 1));

  switch (op) {
  case GFX_OP_SET:
    p &= ~mask;
    p |= shift_v;
    break;
  case GFX_OP_XOR:
    p ^= shift_v;
    break;
  case GFX_OP_AND:
    p &= shift_v | (~mask);
    break;
  }
  row[byte_idx] = p;
}

uint8_t gfx_font_get_cell_width(gfx_font_t *font) { return font->cell_width; }
uint8_t gfx_font_get_cell_height(gfx_font_t *font) { return font->cell_height; }

void gfx_font_draw_char(gfx_font_t *font, uint32_t x, uint32_t y, uint8_t c, uint8_t active_v,
                        uint8_t inactive_v, gfx_operation_t op) {

  uint8_t cell_height = font->cell_height, cell_width = font->cell_width;
  uint32_t matrix_stride = cell_width << 1; // bytes
  uint32_t start_bit = (c & 0xf) * cell_width;
  uint8_t *matrix_row = font->data + (cell_height * (c >> 4) * matrix_stride);
  uint8_t *fb_row = gfx_frame_buffer + (gfx_frame_buffer_stride * y);
  for (int cy = 0; cy < cell_height;
       cy++, matrix_row += matrix_stride, fb_row += gfx_frame_buffer_stride) {
    uint8_t cell_data = matrix_row[start_bit >> 3];
    for (int cx = 0, bit = start_bit; cx < cell_width; cx++, bit++) {
      if ((cx != 0) && ((bit & 0x7) == 0)) {
        cell_data = matrix_row[bit >> 3];
      }
      uint8_t char_cell_px = (cell_data >> (7 - (bit & 0x7))) & 0x1;
      gfx_update_pixel(x + cx, y + cy, char_cell_px ? active_v : inactive_v, op);
    }
  }
}
