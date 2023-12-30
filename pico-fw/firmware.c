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
VTermColor default_bg_color, default_fg_color;
VTermPos cursor_pos = {.row = 0, .col = 0};
bool cursor_visible = true;

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
#if 1
  lum += (uint32_t)(0.2126 * (1 << 16)) * (uint32_t)r;
  lum += (uint32_t)(0.7152 * (1 << 16)) * (uint32_t)g;
  lum += (uint32_t)(0.0722 * (1 << 16)) * (uint32_t)b;
  lum >>= 16;
#else
  lum += r;
  lum += ((uint32_t)g) << 1;
  lum += b;
  lum >>= 2;
#endif

  if (lum < 85) {
    return 0;
  } else if (lum < 171) {
    return 2;
  } else {
    return 3;
  }
}

static int indexed_to_px(uint8_t idx) {
  if (idx == 0) {
    return 0;
  }
  if (idx < 5) {
    return 2;
  }
  if (idx < 8) {
    return 3;
  }
  if (idx < 9) {
    return 2;
  }
  return 3;
}

static uint8_t color_to_px(VTermColor *color) {
  if (VTERM_COLOR_IS_INDEXED(color) && (color->indexed.idx < 16)) {
    return indexed_to_px(color->indexed.idx);
  }
  vterm_screen_convert_color_to_rgb(term_screen, color);
  return rgb_to_px(color->rgb.red, color->rgb.green, color->rgb.blue);
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

      uint8_t fg = color_to_px(&cell.fg), bg = color_to_px(&cell.bg);

      if (cell.attrs.reverse) {
        uint8_t tmp = fg;
        fg = bg;
        bg = tmp;
      }

      if (cursor_visible && (pos.col == cursor_pos.col) && (pos.row == cursor_pos.row)) {
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

static int term_screen_setttermprop(VTermProp prop, VTermValue *val, void *user) {
  switch (prop) {
  case VTERM_PROP_CURSORVISIBLE:
    cursor_visible = !!val->boolean;
    break;
  default:
    break;
  }
  return 1;
}

static int term_screen_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user) {
  line_damaged[oldpos.row] = true;
  line_damaged[pos.row] = true;
  cursor_pos = pos;
  cursor_visible = !!visible;
  return 1;
}

static VTermScreenCallbacks term_screen_cbs = {
    .damage = term_screen_damage,
    .movecursor = term_screen_movecursor,
    .settermprop = term_screen_setttermprop,
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
  vterm_output_set_callback(term, term_output_cb, NULL);
  vterm_set_utf8(term, 1);

  term_screen = vterm_obtain_screen(term);
  line_damaged = malloc(sizeof(bool) * n_rows);
  for (int i = 0; i < n_rows; i++) {
    line_damaged[i] = true;
  }
  vterm_color_rgb(&default_bg_color, 0, 0, 0);
  vterm_color_rgb(&default_fg_color, 255, 255, 255);
  vterm_screen_set_default_colors(term_screen, &default_fg_color, &default_bg_color);
  vterm_screen_set_callbacks(term_screen, &term_screen_cbs, NULL);

  term_state = vterm_obtain_state(term);
  vterm_state_reset(term_state, 1);

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
