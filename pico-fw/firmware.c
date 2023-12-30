#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/multicore.h"
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
VTermColor default_bg_color, default_fg_color;

// Mutex-protected terminal state
auto_init_mutex(term_state_mutex);
VTermPos volatile cursor_pos = {.row = 0, .col = 0};
bool volatile cursor_visible = true, cursor_moved = true;
uint64_t line_damage_map = 0;

// Font handling
gfx_font_t *current_font;

// Frame buffer and frame counter (used for blinking text).
uint8_t *frame_buffer = NULL;
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
#if 0
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
  if ((idx < 5) || (idx == 8)) {
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
  uint32_t cell_height = gfx_font_get_cell_height(current_font);
  uint32_t cell_width = gfx_font_get_cell_width(current_font);
  static uint32_t prev_frame_counter = 0;

  mutex_enter_blocking(&term_state_mutex);
  bool cursor_visible_copy = cursor_visible;
  bool should_redraw_cursor = cursor_moved && cursor_visible;

  if ((frame_counter & 0xf) != (prev_frame_counter & 0xf)) {
    should_redraw_cursor = cursor_visible;
  }
  prev_frame_counter = frame_counter;

  VTermPos cursor_pos_copy = {.row = cursor_pos.row, .col = cursor_pos.col};
  vterm_get_size(term, &n_rows, &n_cols);
  uint64_t line_damage_map_copy = line_damage_map;
  line_damage_map = 0;
  mutex_exit(&term_state_mutex);

  for (pos.row = 0; pos.row < n_rows; ++pos.row) {
    if (!(line_damage_map_copy & (1 << pos.row)) &&
        (!should_redraw_cursor || (pos.row != cursor_pos.row))) {
      continue;
    }
    for (pos.col = 0; pos.col < n_cols; ++pos.col) {
      vterm_screen_get_cell(term_screen, pos, &cell);
      uint8_t c = codepoint_to_ch(cell.chars[0]);
      uint8_t fg = color_to_px(&cell.fg), bg = color_to_px(&cell.bg);
      bool reverse = false;

      if (cell.attrs.reverse) {
        reverse = !reverse;
      }

      if (cursor_visible_copy && (pos.col == cursor_pos_copy.col) &&
          (pos.row == cursor_pos_copy.row)) {
        if ((frame_counter >> 5) & 0x1) {
          reverse = !reverse;
        }
      }

      if (reverse) {
        uint8_t tmp = fg;
        fg = bg;
        bg = tmp;
      }

      gfx_font_draw_char(current_font, pos.col * cell_width, pos.row * cell_height, c, fg, bg,
                         GFX_OP_SET);
    }
  }
}

static void redraw_thread(void) {
  while (true) {
    videoout_wait_for_vblank();
    redraw_term();
  }
}

static int term_screen_damage(VTermRect rect, void *user) {
  for (int i = rect.start_row; i < rect.end_row; i++) {
    line_damage_map |= (1 << i);
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
  line_damage_map |= (1 << oldpos.row);
  cursor_pos = pos;
  cursor_visible = !!visible;
  cursor_moved = true;
  return 1;
}

static VTermScreenCallbacks term_screen_cbs = {
    .damage = term_screen_damage,
    .movecursor = term_screen_movecursor,
    .settermprop = term_screen_setttermprop,
};

static void set_font(gfx_font_t *font) {
  int n_rows = videoout_get_screen_height() / gfx_font_get_cell_height(font);
  int n_cols = videoout_get_screen_width() / gfx_font_get_cell_width(font);
  current_font = font;
  line_damage_map = (1 << n_rows) - 1;
  vterm_set_size(term, n_rows, n_cols);
}

static void set_mode(videoout_mode_t *mode) {
  videoout_set_mode(mode);
  frame_buffer = realloc(frame_buffer, videoout_get_screen_stride() * videoout_get_screen_height());
  videoout_set_frame_buffer(frame_buffer);
  gfx_set_frame_buffer(frame_buffer, videoout_get_screen_stride());
  memset(frame_buffer, 0x00, videoout_get_screen_stride() * videoout_get_screen_height());
}

int main(void) {
  stdio_init_all();
  videoout_init(pio0, VSYNC_GPIO, NOTDIM_GPIO);

  set_mode(&videoout_mode_864_350);

  term = vterm_new(80, 25);
  vterm_output_set_callback(term, term_output_cb, NULL);
  vterm_set_utf8(term, 1);
  set_font(&gfx_mda_8x14_font);

  term_screen = vterm_obtain_screen(term);
  vterm_color_rgb(&default_bg_color, 0, 0, 0);
  vterm_color_rgb(&default_fg_color, 255, 255, 255);
  vterm_screen_set_default_colors(term_screen, &default_fg_color, &default_bg_color);
  vterm_screen_set_callbacks(term_screen, &term_screen_cbs, NULL);

  term_state = vterm_obtain_state(term);
  vterm_state_reset(term_state, 1);

  videoout_set_vblank_callback(vblank_callback);
  videoout_start();

  vterm_input_write(term, "Started.\n\r", 10);

  multicore_reset_core1();
  multicore_launch_core1(redraw_thread);

  while (true) {
    char c = getchar();
    mutex_enter_blocking(&term_state_mutex);
    vterm_input_write(term, &c, 1);
    mutex_exit(&term_state_mutex);
  }

  multicore_reset_core1();
  vterm_free(term);
  videoout_cleanup();
}
