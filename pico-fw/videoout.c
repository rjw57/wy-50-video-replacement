#include <stdalign.h>
#include <stdatomic.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "pico/sync.h"

#include "videoout.h"
#include "videoout.pio.h"

struct videoout_mode {
  uint visible_dots_per_line;
  uint visible_lines_per_frame;

  uint line_period_ns;
  uint lines_per_frame;
  uint vsync_lines_per_frame;
  uint visible_start_line;

  uint hsync_width_ns;
  uint visible_width_ns;
};

// This is tweaked slightly from the mode used by the terminal itself to slightly widen each line.
// This lets us fit the 720 pixels in with a dot clock being a nice integer multiple of the line
// sync clock. This reduces noise in the output.
//
// Note that the video modes here have been carefully chosen so that the dot clock becomes an
// integer multiple of the 125MHz base clock of the pico. This reduces clock jitter and improves
// picture quality.
videoout_mode_t videoout_mode_720_350 = {
    .visible_dots_per_line = 720,
    .visible_lines_per_frame = 350,

    .line_period_ns = 44400,
    .lines_per_frame = 375,
    .vsync_lines_per_frame = 3,
    .visible_start_line = 22,

    .hsync_width_ns = 8256,
    .visible_width_ns = 34560,
};

videoout_mode_t videoout_mode_864_350 = {
    .visible_dots_per_line = 864,
    .visible_lines_per_frame = 350,

    .line_period_ns = 44400,
    .lines_per_frame = 375,
    .vsync_lines_per_frame = 3,
    .visible_start_line = 22,

    .hsync_width_ns = 8280,
    .visible_width_ns = 34560,
};

videoout_mode_t videoout_mode_1024_350 = {
    .visible_dots_per_line = 1024,
    .visible_lines_per_frame = 350,

    .line_period_ns = 44384,
    .lines_per_frame = 375,
    .vsync_lines_per_frame = 3,
    .visible_start_line = 22,

    .hsync_width_ns = 8288,
    .visible_width_ns = 32768,
};

const videoout_mode_t *default_mode = &videoout_mode_720_350;

static bool videoout_is_running = false;
static const videoout_mode_t *active_mode = NULL;
static uint video_pin_base, sync_pin_base;

static inline uint mode_back_porch_width_ns(const videoout_mode_t *m) {
  return m->line_period_ns - m->visible_width_ns;
}

static inline uint mode_dot_clock_period_ns(const videoout_mode_t *m) {
  return m->visible_width_ns / m->visible_dots_per_line;
}

static bool mode_is_valid(const videoout_mode_t *m) {
  if (m->visible_width_ns % m->visible_dots_per_line != 0) {
    return false;
  }
  if (m->line_period_ns % mode_dot_clock_period_ns(m) != 0) {
    return false;
  }
  if ((m->visible_dots_per_line & 0xf) != 0) {
    return false;
  }
  if (mode_back_porch_width_ns(m) == m->hsync_width_ns) {
    return false;
  }
  if (m->lines_per_frame <= m->vsync_lines_per_frame + m->vsync_lines_per_frame) {
    return false;
  }
  if (m->lines_per_frame <= m->visible_start_line + m->visible_lines_per_frame) {
    return false;
  }

  return true;
}

// Timing program for a blank line
alignas(8) uint32_t sync_timing_blank_line[2];
#define SYNC_TIMING_BLANK_LINE_LEN                                                                 \
  (sizeof(sync_timing_blank_line) / sizeof(sync_timing_blank_line[0]))

// Timing program for a vsync line
alignas(8) uint32_t sync_timing_vsync_line[2];
#define SYNC_TIMING_VSYNC_LINE_LEN                                                                 \
  (sizeof(sync_timing_vsync_line) / sizeof(sync_timing_vsync_line[0]))

// Timing program for a visible line
alignas(16) uint32_t sync_timing_visible_line[4];
#define SYNC_TIMING_VISIBLE_LINE_LEN                                                               \
  (sizeof(sync_timing_visible_line) / sizeof(sync_timing_visible_line[0]))

// Must be called when video output has been stopped.
static inline void mode_setup(const videoout_mode_t *m) {
  uint dot_clock_period_ns = mode_dot_clock_period_ns(m);

  sync_timing_blank_line[0] =
      sync_timing_encode(1, 0, m->hsync_width_ns, SIDE_EFFECT_NOP, dot_clock_period_ns);
  sync_timing_blank_line[1] = sync_timing_encode(0, 0, m->line_period_ns - m->hsync_width_ns,
                                                 SIDE_EFFECT_NOP, dot_clock_period_ns);

  sync_timing_vsync_line[0] =
      sync_timing_encode(1, 1, m->hsync_width_ns, SIDE_EFFECT_NOP, dot_clock_period_ns);
  sync_timing_vsync_line[1] = sync_timing_encode(0, 1, m->line_period_ns - m->hsync_width_ns,
                                                 SIDE_EFFECT_NOP, dot_clock_period_ns);

  if (mode_back_porch_width_ns(m) < m->hsync_width_ns) {
    sync_timing_visible_line[0] =
        sync_timing_encode(1, 0, mode_back_porch_width_ns(m), SIDE_EFFECT_NOP, dot_clock_period_ns);
    sync_timing_visible_line[1] =
        sync_timing_encode(1, 0, m->hsync_width_ns - mode_back_porch_width_ns(m),
                           SIDE_EFFECT_SET_TRIGGER, dot_clock_period_ns);
    sync_timing_visible_line[2] = sync_timing_encode(
        0, 0, 16 * dot_clock_period_ns, SIDE_EFFECT_CLEAR_TRIGGER, dot_clock_period_ns);
    sync_timing_visible_line[3] =
        sync_timing_encode(0, 0, m->line_period_ns - m->hsync_width_ns - (16 * dot_clock_period_ns),
                           SIDE_EFFECT_NOP, dot_clock_period_ns);
  } else {
    sync_timing_visible_line[0] =
        sync_timing_encode(1, 0, m->hsync_width_ns, SIDE_EFFECT_NOP, dot_clock_period_ns);
    sync_timing_visible_line[1] =
        sync_timing_encode(1, 0, mode_back_porch_width_ns(m) - m->hsync_width_ns, SIDE_EFFECT_NOP,
                           dot_clock_period_ns);
    sync_timing_visible_line[2] = sync_timing_encode(0, 0, 16 * dot_clock_period_ns,
                                                     SIDE_EFFECT_SET_TRIGGER, dot_clock_period_ns);
    sync_timing_visible_line[3] =
        sync_timing_encode(0, 0, m->visible_width_ns - (16 * dot_clock_period_ns),
                           SIDE_EFFECT_CLEAR_TRIGGER, dot_clock_period_ns);
  }
}

// Semaphore used to signal vblank.
semaphore_t vblank_semaphore;

// Current frame buffer pointer. Marked as atomic so that the ISR always gets a valid value.
static atomic_uintptr_t frame_buffer_ptr;

// Blanking interval callback
static videoout_vblank_callback_t vblank_callback = NULL;

// PIO-related configuration values.
static PIO pio_instance;
static uint video_output_sm;
static uint video_output_offset;
static uint sync_timing_sm;
static uint sync_timing_offset;

// Frame timing DMA channel number and config.
static uint sync_timing_dma_channel;
static dma_channel_config sync_timing_dma_channel_config;

// Video data DMA channel number.
static uint video_dma_channel;

// Phase of frame.
static uint frame_phase = 0;

// Configure a DMA channel to copy the frame buffer into the video output PIO state machine.
static inline dma_channel_config
get_video_output_dma_channel_config(uint dma_chan, PIO pio, uint sm,
                                    bool byte_oriented_frame_buffer) {
  dma_channel_config c = dma_channel_get_default_config(dma_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
  channel_config_set_bswap(&c, byte_oriented_frame_buffer);
  return c;
}

// Configure a DMA channel to copy the timing configurations into the timing PIO state machine.
static inline dma_channel_config get_sync_timing_dma_channel_config(uint dma_chan, PIO pio,
                                                                    uint sm) {
  dma_channel_config c = dma_channel_get_default_config(dma_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
  return c;
}

// DMA handler called when each phase of a frame timing is finished.
static void sync_timing_dma_handler() {

  dma_channel_acknowledge_irq0(sync_timing_dma_channel);

  if (active_mode == NULL) {
    return;
  }

  switch (frame_phase) {
  case 0:
    if (!videoout_is_running) {
      break;
    }

    // VSYNC
    channel_config_set_ring(&sync_timing_dma_channel_config, false, 3);
    dma_channel_set_config(sync_timing_dma_channel, &sync_timing_dma_channel_config, false);
    dma_channel_transfer_from_buffer_now(sync_timing_dma_channel, sync_timing_vsync_line,
                                         SYNC_TIMING_VSYNC_LINE_LEN *
                                             active_mode->vsync_lines_per_frame);

    // Start frame buffer transfer for the next field.
    dma_channel_transfer_from_buffer_now(video_dma_channel, (void *)atomic_load(&frame_buffer_ptr),
                                         active_mode->visible_lines_per_frame *
                                             (active_mode->visible_dots_per_line >> 4));

    frame_phase = 1;
    break;
  case 1:
    // blank line
    channel_config_set_ring(&sync_timing_dma_channel_config, false, 3);
    dma_channel_set_config(sync_timing_dma_channel, &sync_timing_dma_channel_config, false);
    dma_channel_transfer_from_buffer_now(
        sync_timing_dma_channel, sync_timing_blank_line,
        SYNC_TIMING_BLANK_LINE_LEN *
            (active_mode->visible_start_line - active_mode->vsync_lines_per_frame));
    frame_phase = 2;
    break;
  case 2:
    // visible line
    channel_config_set_ring(&sync_timing_dma_channel_config, false, 4);
    dma_channel_set_config(sync_timing_dma_channel, &sync_timing_dma_channel_config, false);
    dma_channel_transfer_from_buffer_now(sync_timing_dma_channel, sync_timing_visible_line,
                                         SYNC_TIMING_VISIBLE_LINE_LEN *
                                             active_mode->visible_lines_per_frame);
    frame_phase = 3;
    break;
  case 3:
    // blank line
    channel_config_set_ring(&sync_timing_dma_channel_config, false, 3);
    dma_channel_set_config(sync_timing_dma_channel, &sync_timing_dma_channel_config, false);
    dma_channel_transfer_from_buffer_now(sync_timing_dma_channel, sync_timing_blank_line,
                                         SYNC_TIMING_BLANK_LINE_LEN *
                                             (active_mode->lines_per_frame -
                                              active_mode->visible_start_line -
                                              active_mode->visible_lines_per_frame));

    // Release the vblank semaphore which will wake anything waiting on it.
    sem_release(&vblank_semaphore);

    // Call any vsync callback
    if (vblank_callback != NULL) {
      vblank_callback();
    }

    frame_phase = 0;
    break;
  }
}

// This function contains all static asserts. It's never called but the compiler will raise a
// diagnostic if the assertions fail.
static inline void all_static_asserts() {
  // Statically assert alignment and length of timing programs. Alignment is necessary to allow DMA
  // in ring mode and the length needs to be known because we need to set the number of significan
  // bits in ring mode.
  static_assert(alignof(sync_timing_blank_line) == sizeof(sync_timing_blank_line));
  static_assert(SYNC_TIMING_BLANK_LINE_LEN == 2);
  static_assert(alignof(sync_timing_visible_line) == sizeof(sync_timing_visible_line));
  static_assert(SYNC_TIMING_VISIBLE_LINE_LEN == 4);
  static_assert(alignof(sync_timing_vsync_line) == sizeof(sync_timing_vsync_line));
  static_assert(SYNC_TIMING_VSYNC_LINE_LEN == 2);
}

void videoout_init(PIO pio, uint sync_pin_base_, uint video_pin_base_) {
  videoout_is_running = false;

  // Record which PIO instance is used.
  pio_instance = pio;

  // Ensure IRQ 4 of the PIO is clear
  pio_interrupt_clear(pio_instance, 4);

  // Configure output program
  video_output_offset = pio_add_program(pio_instance, &video_output_program);
  video_output_sm = pio_claim_unused_sm(pio_instance, true);
  video_pin_base = video_pin_base_;

  // Configure timing program.
  sync_timing_offset = pio_add_program(pio_instance, &sync_timing_program);
  sync_timing_sm = pio_claim_unused_sm(pio_instance, true);
  sync_pin_base = sync_pin_base_;

  // Configure frame timing DMA channel.
  sync_timing_dma_channel = dma_claim_unused_channel(true);
  sync_timing_dma_channel_config =
      get_sync_timing_dma_channel_config(sync_timing_dma_channel, pio_instance, sync_timing_sm);
  dma_channel_set_write_addr(sync_timing_dma_channel, &pio_instance->txf[sync_timing_sm], false);
  dma_channel_set_irq0_enabled(sync_timing_dma_channel, true);

  // Configure DMA channel for copying frame buffer to video output.
  video_dma_channel = dma_claim_unused_channel(true);
  dma_channel_config video_dma_channel_config =
      get_video_output_dma_channel_config(video_dma_channel, pio_instance, video_output_sm, true);
  dma_channel_set_config(video_dma_channel, &video_dma_channel_config, false);
  dma_channel_set_write_addr(video_dma_channel, &pio_instance->txf[video_output_sm], false);

  videoout_set_mode(default_mode);

  // Enable interrupt handler for frame timing.
  irq_set_exclusive_handler(DMA_IRQ_0, sync_timing_dma_handler);
  irq_set_enabled(DMA_IRQ_0, true);
}

bool videoout_set_mode(const videoout_mode_t *mode) {
  if (videoout_is_running) {
    return false;
  }
  if (!mode_is_valid(mode)) {
    return false;
  }
  mode_setup(mode);
  active_mode = mode;
  return true;
}

void videoout_start(void) {
  if (videoout_is_running || (active_mode == NULL)) {
    return;
  }

  sem_init(&vblank_semaphore, 0, 1);

  video_output_program_init(pio_instance, video_output_sm, video_output_offset, video_pin_base,
                            mode_dot_clock_period_ns(active_mode));
  sync_timing_program_init(pio_instance, sync_timing_sm, sync_timing_offset, sync_pin_base,
                           mode_dot_clock_period_ns(active_mode));

  pio_sm_restart(pio_instance, video_output_sm);
  pio_sm_put(pio_instance, video_output_sm, active_mode->visible_dots_per_line - 1);
  pio_sm_set_enabled(pio_instance, video_output_sm, true);
  pio_sm_restart(pio_instance, sync_timing_sm);
  pio_sm_set_enabled(pio_instance, sync_timing_sm, true);

  // Start frame timing.
  videoout_is_running = true;
  frame_phase = 0;
  sync_timing_dma_handler();
}

void videoout_stop(void) {
  if (!videoout_is_running) {
    return;
  }

  videoout_is_running = false;
  dma_channel_wait_for_finish_blocking(video_dma_channel);

  pio_sm_set_enabled(pio_instance, video_output_sm, false);
  pio_sm_set_enabled(pio_instance, sync_timing_sm, false);
}

void videoout_cleanup(void) {
  irq_set_enabled(DMA_IRQ_0, false);
  dma_channel_cleanup(video_dma_channel);
  dma_channel_unclaim(video_dma_channel);
  dma_channel_cleanup(sync_timing_dma_channel);
  dma_channel_unclaim(sync_timing_dma_channel);

  pio_sm_set_enabled(pio_instance, video_output_sm, false);
  pio_remove_program(pio_instance, &video_output_program, video_output_offset);
  pio_sm_unclaim(pio_instance, video_output_sm);
  pio_sm_set_enabled(pio_instance, sync_timing_sm, false);
  pio_remove_program(pio_instance, &sync_timing_program, sync_timing_offset);
  pio_sm_unclaim(pio_instance, sync_timing_sm);
}

void videoout_set_vblank_callback(videoout_vblank_callback_t callback) {
  vblank_callback = callback;
}

uint videoout_get_screen_width(void) {
  return (active_mode == NULL) ? 0 : active_mode->visible_dots_per_line;
}

uint videoout_get_screen_height(void) {
  return (active_mode == NULL) ? 0 : active_mode->visible_lines_per_frame;
}

uint videoout_get_screen_stride(void) {
  return (active_mode == NULL) ? 0 : (active_mode->visible_dots_per_line >> 4) << 2;
}

void videoout_set_frame_buffer(void *frame_buffer) {
  atomic_store(&frame_buffer_ptr, (uintptr_t)frame_buffer);
}

void videoout_wait_for_vblank(void) { sem_acquire_blocking(&vblank_semaphore); }
