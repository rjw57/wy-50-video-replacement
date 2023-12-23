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

bool terminal_dirty = false, cursor_moved = false;

int cursor_r = -1, cursor_c = -1;

const wchar_t *acs_chars = L"\x1a\x1b\x18\x19\xdb\x04\xb1\xf8##\xd9\xbf\xda\xc0\xc5~-\xc4-_"
                           L"\xc3\xb4\xc1\xc2\xb3\xf3\xf2\xe3!\x9c\x07";

uint32_t frame_counter = 0;
uint32_t last_cursor_draw = 0;

static void vblank_callback() { frame_counter++; }

inline static uint8_t map_wchar(wchar_t c) {
  // ASCII subset
  if (!(c & ~0x7f) && (c >= 0x20)) {
    return c;
  }

  // Fall-back.
  return ' ';
}

static void redraw_terminal(TMT *vt) {
  if ((cursor_r >= 0) && ((frame_counter & 0x1f) == 0) && (last_cursor_draw != frame_counter)) {
    for (int y = 12; y < 14; y++) {
      for (int x = 0; x < 9; x++) {
        gfx_update_pixel(x + cursor_c * 9, y + cursor_r * 14, 0x2, GFX_OP_XOR);
      }
    }
    last_cursor_draw = frame_counter;
  }

  if (!terminal_dirty && !cursor_moved) {
    return;
  }

  const TMTSCREEN *s = tmt_screen(vt);
  const TMTPOINT *c = tmt_cursor(vt);

  for (size_t r = 0; r < s->nline; r++) {
    if (s->lines[r]->dirty || (cursor_r == r) || (c->r == r)) {
      for (size_t c = 0; c < s->ncol; c++) {
        uint8_t fg = 0x2, bg = 0x0;
        wchar_t ch = s->lines[r]->chars[c].c;

        switch (s->lines[r]->chars[c].a.fg) {
        case TMT_COLOR_BLACK:
          fg = 0x0;
          break;
        case TMT_COLOR_RED:
        case TMT_COLOR_GREEN:
        case TMT_COLOR_YELLOW:
        case TMT_COLOR_BLUE:
          fg = 0x2;
          break;
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
          bg = 0x2;
          break;
        case TMT_COLOR_MAGENTA:
        case TMT_COLOR_CYAN:
        case TMT_COLOR_WHITE:
          bg = 0x3;
          break;
        default:
          // nop
          break;
        }

        if (s->lines[r]->chars[c].a.bold) {
          fg |= 0x1;
        }

        if (s->lines[r]->chars[c].a.reverse) {
          uint8_t tmp = fg;
          fg = bg;
          bg = tmp;
        }

        gfx_draw_char(c * 9, r * 14, ch & 0xff, fg, bg, GFX_OP_SET);
      }
    }
  }

  cursor_r = c->r;
  cursor_c = c->c;

  tmt_clean(vt);
  terminal_dirty = false;
  cursor_moved = false;
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
    printf("%s", (const char *)a);
    break;

  case TMT_MSG_MOVED:
    cursor_moved = true;
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

  videoout_set_vblank_callback(vblank_callback);

  videoout_start();

  vt = tmt_open(videoout_get_screen_height() / 14, videoout_get_screen_width() / 9, term_callback,
                NULL, acs_chars);

  tmt_write(vt, "Hello, world!\r\n", 0);

  while (true) {
    for (int i = 0; i < 128; ++i) {
      int c = getchar_timeout_us(10000);
      if (c == PICO_ERROR_TIMEOUT) {
        break;
      }
      char tc = c;
      tmt_write(vt, &tc, 1);
    }

    redraw_terminal(vt);
  }

  tmt_close(vt);
  videoout_cleanup();
}
