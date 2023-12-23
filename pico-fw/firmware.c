#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libtsm.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"

#include "cp437_map.h"
#include "graphics.h"
#include "videoout.h"

#define VSYNC_GPIO 2                 // == pin 4
#define HSYNC_GPIO (VSYNC_GPIO + 1)  // == pin 5
#define NOTDIM_GPIO 4                // == pin 6
#define VIDEO_GPIO (NOTDIM_GPIO + 1) // == pin 7

// Terminal state management.
struct tsm_screen *term_screen;
struct tsm_vte *term_vte;
tsm_age_t last_drawn_age = 0;

// Frame buffer and frame counter (used for blinking text).
uint8_t *frame_buffer;
uint32_t frame_counter = 0;

static void vblank_callback() { frame_counter++; }

static void term_write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data) {
  fwrite(u8, 1, len, stdout);
}

static uint8_t codepoint_to_ch(uint32_t cp) {
  if ((cp >= 0x20) && (cp < 0x7f)) {
    return cp;
  }
  uint16_t *map_row = cp437_map[cp & 0xff];
  for (int i = 0; i < CP437_ENTRY_LEN; i++) {
    uint16_t entry = map_row[i];
    if ((entry >> 8) == (cp >> 8)) {
      return entry & 0xff;
    }
  }
  return 0;
}

static int rgb_to_px(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t lum = 0;
  lum += (uint32_t)(0.2126 * (1 << 16)) * (uint32_t)r;
  lum += (uint32_t)(0.7152 * (1 << 16)) * (uint32_t)g;
  lum += (uint32_t)(0.0722 * (1 << 16)) * (uint32_t)b;
  lum >>= 16;

  if (lum < 10) {
    return 0;
  } else if (lum < 200) {
    return 2;
  } else {
    return 3;
  }
}

static int term_draw_cb(struct tsm_screen *con, uint32_t id, const uint32_t *ch, size_t len,
                        unsigned int width, unsigned int posx, unsigned int posy,
                        const struct tsm_screen_attr *attr, tsm_age_t age, void *data) {
  if (age <= last_drawn_age) {
    return 0;
  }

  uint8_t char_code = codepoint_to_ch(id);
  uint8_t fg = 0x2, bg = 0x0;

  fg = rgb_to_px(attr->fr, attr->fg, attr->fb);
  bg = rgb_to_px(attr->br, attr->bg, attr->bb);

  if (attr->bold) {
    fg |= 0x1;
  }

  if (attr->inverse) {
    fg ^= 0x2;
    bg ^= 0x2;
  }

  gfx_draw_char(posx * 9, posy * 14, char_code, fg, bg, GFX_OP_SET);

  return 0;
}

int main(void) {
  stdio_init_all();
  videoout_init(pio0, VSYNC_GPIO, NOTDIM_GPIO);

  frame_buffer = malloc(videoout_get_screen_stride() * videoout_get_screen_height());
  videoout_set_frame_buffer(frame_buffer);
  gfx_set_frame_buffer(frame_buffer, videoout_get_screen_stride());

  memset(frame_buffer, 0xff, videoout_get_screen_stride() * videoout_get_screen_height());

  tsm_screen_new(&term_screen, NULL, NULL);
  tsm_screen_resize(term_screen, videoout_get_screen_width() / 9,
                    videoout_get_screen_height() / 14);
  tsm_vte_new(&term_vte, term_screen, term_write_cb, NULL, NULL, NULL);

  videoout_set_vblank_callback(vblank_callback);

  videoout_start();

  while (true) {
    char buf[128];
    int i;
    for (i = 0; i < sizeof(buf); ++i) {
      int c = getchar_timeout_us(10000);
      if (c == PICO_ERROR_TIMEOUT) {
        break;
      }
      buf[i] = c;
    }

    if (i > 0) {
      tsm_vte_input(term_vte, buf, i);
    }

    last_drawn_age = tsm_screen_draw(term_screen, term_draw_cb, NULL);
  }

  videoout_cleanup();
}
