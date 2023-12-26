#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "vterm.h"

#include "cp437_map.h"
#include "graphics.h"
#include "videoout.h"

#define VSYNC_GPIO 2                 // == pin 4
#define HSYNC_GPIO (VSYNC_GPIO + 1)  // == pin 5
#define NOTDIM_GPIO 4                // == pin 6
#define VIDEO_GPIO (NOTDIM_GPIO + 1) // == pin 7

// Terminal state management.
VTerm *term;
VTermScreen *term_screen;
VTermState *term_state;
bool *line_damaged;

// Frame buffer and frame counter (used for blinking text).
uint8_t *frame_buffer;
uint32_t frame_counter = 0;

static void vblank_callback() { frame_counter++; }

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

static void term_output_cb(const char *s, size_t len, void *user) { fwrite(s, 1, len, stdout); }

static void redraw_term(void) {
  int n_rows, n_cols;
  VTermPos pos;
  VTermScreenCell cell;

  vterm_get_size(term, &n_rows, &n_cols);
  for (pos.row = 0; pos.row < n_rows; ++pos.row) {
    if (!line_damaged[pos.row]) {
      continue;
    }
    for (pos.col = 0; pos.col < n_cols; ++pos.col) {
      vterm_screen_get_cell(term_screen, pos, &cell);
      uint8_t c = codepoint_to_ch(cell.chars[0]);

      uint8_t fg = 0x3, bg = 0x0;
      if (!VTERM_COLOR_IS_DEFAULT_FG(&(cell.fg))) {
        vterm_screen_convert_color_to_rgb(term_screen, &(cell.fg));
        fg = rgb_to_px(cell.fg.rgb.red, cell.fg.rgb.green, cell.fg.rgb.blue);
      }
      if (!VTERM_COLOR_IS_DEFAULT_BG(&(cell.bg))) {
        vterm_screen_convert_color_to_rgb(term_screen, &(cell.bg));
        bg = rgb_to_px(cell.bg.rgb.red, cell.bg.rgb.green, cell.bg.rgb.blue);
      }

      if (cell.attrs.reverse) {
        uint8_t tmp = fg;
        fg = bg;
        bg = tmp;
      }

      gfx_draw_char(pos.col * 9, pos.row * 14, c, fg, bg, GFX_OP_SET);
    }
    line_damaged[pos.row] = false;
  }
}

static int term_screen_damage(VTermRect rect, void *user) {
  for (int i = rect.start_row; i < rect.end_row; i++) {
    line_damaged[i] = true;
  }
  return 1;
}

static VTermScreenCallbacks term_screen_cbs = {
    .damage = term_screen_damage,
};

int main(void) {
  stdio_init_all();
  videoout_init(pio0, VSYNC_GPIO, NOTDIM_GPIO);

  frame_buffer = malloc(videoout_get_screen_stride() * videoout_get_screen_height());
  videoout_set_frame_buffer(frame_buffer);
  gfx_set_frame_buffer(frame_buffer, videoout_get_screen_stride());

  memset(frame_buffer, 0xff, videoout_get_screen_stride() * videoout_get_screen_height());

  int n_rows = videoout_get_screen_height() / 14, n_cols = videoout_get_screen_width() / 9;
  term = vterm_new(n_rows, n_cols);
  line_damaged = malloc(sizeof(bool) * n_rows);
  for (int i = 0; i < n_rows; i++) {
    line_damaged[i] = true;
  }
  vterm_set_utf8(term, 1);
  term_state = vterm_obtain_state(term);
  vterm_state_reset(term_state, 1);
  term_screen = vterm_obtain_screen(term);
  vterm_screen_set_callbacks(term_screen, &term_screen_cbs, NULL);

  vterm_output_set_callback(term, term_output_cb, NULL);

  videoout_set_vblank_callback(vblank_callback);
  videoout_start();

  while (true) {
    redraw_term();

    char buf[1024];
    int i;
    for (i = 0; i < sizeof(buf); ++i) {
      int c = getchar_timeout_us(20000);
      if (c == PICO_ERROR_TIMEOUT) {
        break;
      }
      buf[i] = c;
    }

    if (i > 0) {
      vterm_input_write(term, buf, i);
    }
  }

  vterm_free(term);
  videoout_cleanup();
}
