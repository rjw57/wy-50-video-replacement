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

static inline uint16_t get_font_row(gfx_font_t *font, uint8_t char_idx, uint32_t y) {
  // Font is packed into (cell width) bits x (cell height) row cells arranged in 16 x 16 cells. Work
  // out which row in the font matrix we need to extract.
  const uint32_t matrix_stride = font->cell_width << 1; // bytes
  uint32_t matrix_row = font->cell_height * (char_idx >> 4) + y;

  uint32_t start_bit = (char_idx & 0xf) * font->cell_width;
  uint32_t start_byte = start_bit >> 3;
  uint32_t start_bit_within_byte = start_bit & 0x7;

  uint8_t left = font->data[matrix_row * matrix_stride + start_byte];
  if (font->cell_width == 8) {
    return left;
  } else if (font->cell_width == 9) {
    uint16_t out_row = left;
    uint8_t right = font->data[matrix_row * matrix_stride + start_byte + 1];

    out_row <<= (1 + start_bit_within_byte);
    out_row |= right >> (7 - start_bit_within_byte);
    out_row &= 0x1ff;

    return out_row;
  }

  return 0;
}

void gfx_set_frame_buffer(uint8_t *frame_buffer, uint32_t stride) {
  gfx_frame_buffer = frame_buffer;
  gfx_frame_buffer_stride = stride;
}

void gfx_update_pixel(uint32_t x, uint32_t y, uint8_t v, gfx_operation_t op) {
  uint8_t *row = gfx_frame_buffer + (gfx_frame_buffer_stride * y);
  uint32_t byte_idx = x >> 2;
  uint32_t px_idx = x & 0x3;

  uint32_t p = row[byte_idx];

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
  for (uint32_t ridx = 0; ridx < cell_height; ridx++, y++) {
    uint16_t row = get_font_row(font, c, ridx);
    for (uint32_t cidx = 0; cidx < cell_width; cidx++) {
      gfx_update_pixel(x + cidx, y, (row & (1 << (cell_width - 1 - cidx))) ? active_v : inactive_v,
                       op);
    }
  }
}
