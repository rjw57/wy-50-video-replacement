#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "tmt.h"

#include "graphics.h"
#include "videoout.h"

#define VSYNC_GPIO 2                 // == pin 4
#define HSYNC_GPIO (VSYNC_GPIO + 1)  // == pin 5
#define NOTDIM_GPIO 4                // == pin 6
#define VIDEO_GPIO (NOTDIM_GPIO + 1) // == pin 7

uint8_t *frame_buffer;

TMT *vt;

bool terminal_dirty = false;

int cursor_r = -1, cursor_c = -1;

static void redraw_terminal(TMT *vt) {
  const TMTSCREEN *s = tmt_screen(vt);
  const TMTPOINT *c = tmt_cursor(vt);

  if (cursor_r >= 0) {
    gfx_draw_char(cursor_c * 9, cursor_r * 14, 219, 0x3, 0x0, GFX_OP_XOR);
  }

  for (size_t r = 0; r < s->nline; r++) {
    if (s->lines[r]->dirty) {
      for (size_t c = 0; c < s->ncol; c++) {
        uint8_t fg = 0x3, bg = 0x0;

        switch (s->lines[r]->chars[c].a.fg) {
        case TMT_COLOR_BLACK:
          fg = 0x0;
          break;
        case TMT_COLOR_RED:
        case TMT_COLOR_GREEN:
        case TMT_COLOR_YELLOW:
        case TMT_COLOR_BLUE:
        case TMT_COLOR_MAGENTA:
        case TMT_COLOR_CYAN:
        case TMT_COLOR_WHITE:
          fg = 0x3;
          break;
        default:
          // nop
          break;
        }

        switch (s->lines[r]->chars[c].a.bg) {
        case TMT_COLOR_BLACK:
          bg = 0x0;
          break;
        case TMT_COLOR_RED:
        case TMT_COLOR_GREEN:
        case TMT_COLOR_YELLOW:
        case TMT_COLOR_BLUE:
        case TMT_COLOR_MAGENTA:
        case TMT_COLOR_CYAN:
        case TMT_COLOR_WHITE:
          bg = 0x2;
          break;
        default:
          // nop
          break;
        }

        if (s->lines[r]->chars[c].a.reverse) {
          uint8_t tmp = fg;
          fg = bg;
          bg = tmp;
        }
        gfx_draw_char(c * 9, r * 14, s->lines[r]->chars[c].c, fg, bg, GFX_OP_SET);
      }
    }
  }

  cursor_r = c->r;
  cursor_c = c->c;
  gfx_draw_char(cursor_c * 9, cursor_r * 14, 219, 0x3, 0x0, GFX_OP_XOR);

  tmt_clean(vt);
  terminal_dirty = false;
}

void term_callback(tmt_msg_t m, TMT *vt, const void *a, void *p) {
  // const TMTSCREEN *s = tmt_screen(vt);
  // const TMTPOINT *c = tmt_cursor(vt);

  switch (m) {
  case TMT_MSG_BELL:
    // nop
    break;

  case TMT_MSG_UPDATE:
    terminal_dirty = true;
    break;

  case TMT_MSG_ANSWER:
    // nop
    break;

  case TMT_MSG_MOVED:
    terminal_dirty = true;
    // printf("cursor is now at %zd,%zd\n", c->r, c->c);
    break;

  case TMT_MSG_CURSOR:
    break;
  }
}

int main(void) {
  stdio_init_all();
  videoout_init(pio0, VSYNC_GPIO, NOTDIM_GPIO);

  frame_buffer = malloc(videoout_get_screen_stride() * videoout_get_screen_height());
  videoout_set_frame_buffer(frame_buffer);
  gfx_set_frame_buffer(frame_buffer, videoout_get_screen_stride());

  memset(frame_buffer, 0xff, videoout_get_screen_stride() * videoout_get_screen_height());

  videoout_start();

  vt = tmt_open(videoout_get_screen_height() / 14, videoout_get_screen_width() / 9, term_callback,
                NULL, NULL);

  while (true) {
    while (true) {
      int c = getchar_timeout_us(10000);
      if (c == PICO_ERROR_TIMEOUT) {
        break;
      }
      char tc = c;
      tmt_write(vt, &tc, 1);
    }

    if (terminal_dirty) {
      redraw_terminal(vt);
    }
  }

  tmt_close(vt);
  videoout_cleanup();
}
