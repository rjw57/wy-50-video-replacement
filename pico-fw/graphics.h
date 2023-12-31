#include <stdint.h>

typedef struct gfx_font gfx_font_t;

extern gfx_font_t gfx_cga_8x8_font;
extern gfx_font_t gfx_mda_8x14_font;
extern gfx_font_t gfx_mda_9x14_font;

// Possible operations for pixel setting.
typedef enum {
  GFX_OP_SET,
  GFX_OP_XOR,
  GFX_OP_AND,
} gfx_operation_t;

void gfx_set_frame_buffer(uint8_t *frame_buffer, uint32_t stride);
uint8_t *gfx_get_frame_buffer(void);
uint32_t gfx_get_frame_buffer_stride(void);

void gfx_update_pixel(uint32_t x, uint32_t y, uint8_t v, gfx_operation_t op);

uint8_t gfx_font_get_cell_width(gfx_font_t *font);
uint8_t gfx_font_get_cell_height(gfx_font_t *font);

void gfx_font_draw_char(gfx_font_t *font, uint32_t x, uint32_t y, uint8_t c, uint8_t active_v,
                        uint8_t inactive_v, gfx_operation_t op);
