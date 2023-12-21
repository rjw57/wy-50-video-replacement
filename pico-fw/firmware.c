#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "graphics.h"
#include "videoout.h"

#define VSYNC_GPIO 2                 // == pin 4
#define HSYNC_GPIO (VSYNC_GPIO + 1)  // == pin 5
#define NOTDIM_GPIO 4                // == pin 6
#define VIDEO_GPIO (NOTDIM_GPIO + 1) // == pin 7

uint8_t *frame_buffer;

int main(void) {
  stdio_init_all();
  videoout_init(pio0, VSYNC_GPIO, NOTDIM_GPIO);

  frame_buffer = malloc(videoout_get_screen_stride() * videoout_get_screen_height());
  videoout_set_frame_buffer(frame_buffer);
  gfx_set_frame_buffer(frame_buffer, videoout_get_screen_stride());

  memset(frame_buffer, 0x00, videoout_get_screen_stride() * videoout_get_screen_height());

  int y = 0;
  for (int c = 0; c < 256; c++) {
    gfx_draw_char((c * 9) % videoout_get_screen_width(),
                  y + 14 * ((c * 9) / videoout_get_screen_width()), c, 0x3, 0x0, GFX_OP_SET);
  }
  y += 14 * 3;
  for (int c = 0; c < 256; c++) {
    gfx_draw_char((c * 9) % videoout_get_screen_width(),
                  y + 14 * ((c * 9) / videoout_get_screen_width()), c, 0x2, 0x0, GFX_OP_SET);
  }
  y += 14 * 3;
  for (int c = 0; c < 256; c++) {
    gfx_draw_char((c * 9) % videoout_get_screen_width(),
                  y + 14 * ((c * 9) / videoout_get_screen_width()), c, 0x3, 0x2, GFX_OP_SET);
  }
  y += 14 * 3;
  for (int c = 0; c < 256; c++) {
    gfx_draw_char((c * 9) % videoout_get_screen_width(),
                  y + 14 * ((c * 9) / videoout_get_screen_width()), c, 0x2, 0x3, GFX_OP_SET);
  }
  y += 14 * 3;
  for (int c = 0; c < 256; c++) {
    gfx_draw_char((c * 9) % videoout_get_screen_width(),
                  y + 14 * ((c * 9) / videoout_get_screen_width()), c, 0x3, 0x0, GFX_OP_SET);
  }
  y += 14 * 3;
  for (int c = 0; c < 256; c++) {
    gfx_draw_char((c * 9) % videoout_get_screen_width(),
                  y + 14 * ((c * 9) / videoout_get_screen_width()), c, 0x2, 0x0, GFX_OP_SET);
  }
  y += 14 * 3;
  for (int c = 0; c < 256; c++) {
    gfx_draw_char((c * 9) % videoout_get_screen_width(),
                  y + 14 * ((c * 9) / videoout_get_screen_width()), c, 0x0, 0x3, GFX_OP_SET);
  }
  y += 14 * 3;
  for (int c = 0; c < 256; c++) {
    gfx_draw_char((c * 9) % videoout_get_screen_width(),
                  y + 14 * ((c * 9) / videoout_get_screen_width()), c, 0x0, 0x2, GFX_OP_SET);
  }
  y += 14 * 3;

  videoout_start();
  puts("Started up.");

  while (true) {
  }

  videoout_cleanup();
}
