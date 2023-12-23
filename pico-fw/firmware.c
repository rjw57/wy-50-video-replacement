#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libtsm.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"

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

static int term_draw_cb(struct tsm_screen *con, uint32_t id, const uint32_t *ch, size_t len,
                        unsigned int width, unsigned int posx, unsigned int posy,
                        const struct tsm_screen_attr *attr, tsm_age_t age, void *data) {
  if (age <= last_drawn_age) {
    return 0;
  }

  if ((id < 0x20) || (id > 0x7f)) {
    id = 0;
  }

  uint8_t fg = 0x2, bg = 0x0;

  if (attr->bold) {
    fg |= 0x1;
  }

  if (attr->inverse) {
    uint8_t tmp = fg;
    fg = bg;
    bg = tmp;
  }

  gfx_draw_char(posx * 9, posy * 14, id & 0xff, fg, bg, GFX_OP_SET);

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
