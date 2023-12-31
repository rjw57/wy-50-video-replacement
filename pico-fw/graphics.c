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

uint32_t char_cell_rows[16];
#define MAX_CHAR_CELL_ROWS (sizeof(char_cell_rows) / sizeof(char_cell_rows[0]))

// Copy rows of font character cell into char_cell_rows. The LSB of each entry in char_cell_rows is
// the right-most pixel of the character cell. Each pixel is two bits with 11 meaning "active" and
// 00 meaning "inactive".
static inline void copy_font_rows(gfx_font_t *font, uint8_t char_idx) {
  const uint32_t matrix_stride = font->cell_width << 1; // bytes
  uint32_t start_bit = (char_idx & 0xf) * font->cell_width;
  uint8_t *matrix_row = font->data + (font->cell_height * (char_idx >> 4) * matrix_stride);
  for (int i = 0; i < font->cell_height; ++i, matrix_row += matrix_stride) {
    uint32_t v = 0;
    uint8_t byte = matrix_row[start_bit >> 3];
    for (int bit = start_bit; bit < start_bit + font->cell_width; ++bit) {
      if ((bit & 0x7) == 0) {
        byte = matrix_row[bit >> 3];
      }
      v <<= 2;
      if (byte & (1 << (7 - (bit & 0x7)))) {
        v |= 3;
      }
    }
    char_cell_rows[i] = v;
  }
}

void gfx_set_frame_buffer(uint8_t *frame_buffer, uint32_t stride) {
  gfx_frame_buffer = frame_buffer;
  gfx_frame_buffer_stride = stride;
}

inline void gfx_update_pixel(uint32_t x, uint32_t y, uint8_t v, gfx_operation_t op) {
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
  copy_font_rows(font, c);
  uint32_t active_pattern =
      ((active_v & 0x1) ? 0x55555555 : 0) | ((active_v & 0x2) ? 0xaaaaaaaa : 0);
  uint32_t inactive_pattern =
      ((inactive_v & 0x1) ? 0x55555555 : 0) | ((inactive_v & 0x2) ? 0xaaaaaaaa : 0);

  for (uint32_t ridx = 0; ridx < cell_height; ridx++, y++) {
    uint32_t row = char_cell_rows[ridx];
    row = (active_pattern & row) | (inactive_pattern & ~row);
    for (uint32_t cidx = 0; cidx < cell_width; cidx++) {
      gfx_update_pixel(x + cidx, y, (row >> ((cell_width - cidx - 1) << 1)) & 0x3, op);
    }
  }
}
