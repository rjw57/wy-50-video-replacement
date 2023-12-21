#include <stdint.h>

// Possible operations for pixel setting.
typedef enum {
  GFX_OP_SET,
  GFX_OP_XOR,
  GFX_OP_AND,
} gfx_operation_t;

void gfx_set_frame_buffer(uint8_t *frame_buffer, uint32_t stride);

void gfx_update_pixel(uint32_t x, uint32_t y, uint8_t v, gfx_operation_t op);

void gfx_draw_char(uint32_t x, uint32_t y, uint8_t c, uint8_t active_v, uint8_t inactive_v, gfx_operation_t op);
