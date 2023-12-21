#include "graphics.h"

#include "mda_font.h"

static uint8_t *gfx_frame_buffer;
static uint32_t gfx_frame_buffer_stride;

static uint16_t get_font_row(uint8_t char_idx, uint32_t y) {
  // Font is packed into 9 bits x 14 row cells arranged in 32 x 8 cells. Work out which row in the
  // font matrix we need to extract.
  const uint32_t matrix_stride = 36; // bytes
  uint32_t matrix_row = 14 * (char_idx >> 5) + y;

  uint32_t start_bit = (char_idx & 0x1f) * 9;
  uint32_t start_byte = start_bit >> 3;
  uint32_t start_bit_within_byte = start_bit & 0x7;

  uint8_t left = mda_font[matrix_row * matrix_stride + start_byte];
  uint8_t right = mda_font[matrix_row * matrix_stride + start_byte + 1];

  uint16_t out_row = left;
  out_row <<= (1 + start_bit_within_byte);
  out_row |= right >> (7 - start_bit_within_byte);
  out_row &= 0x1ff;

  return out_row;
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

void gfx_draw_char(uint32_t x, uint32_t y, uint8_t c, uint8_t active_v, uint8_t inactive_v,
                   gfx_operation_t op) {
  for (uint32_t ridx = 0; ridx < 14; ridx++, y++) {
    uint16_t row = get_font_row(c, ridx);
    for (uint32_t cidx = 0; cidx < 9; cidx++) {
      gfx_update_pixel(x + cidx, y, (row & (1 << (9 - cidx))) ? active_v : inactive_v, op);
    }
  }
}
