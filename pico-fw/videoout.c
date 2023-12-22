#include <stdalign.h>
#include <stdatomic.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "pico/sync.h"

#include "videoout.h"
#include "videoout.pio.h"

// Resolution
#define VISIBLE_DOTS_PER_LINE 720   // Horizontal resolution
#define VISIBLE_LINES_PER_FRAME 350 // Number of visible lines per frame

// Signal timing. This is tweaked slightly from the mode used by the terminal itself to slightly
// widen each line. This lets us fit the 720 pixels in with a dot clock being a nice integer
// multiple of the line sync clock. This reduces noise in the output.

#define LINE_PERIOD_NS 44400       // Period of one line of video (ns)
#define LINES_PER_FRAME 375        // Number of lines in a *frame*
#define VSYNC_LINES_PER_FRAME 3    // V-sync lines at start of frame
#define VERT_VISIBLE_START_LINE 22 // Start line of visible data (0-based)
#define HSYNC_WIDTH_NS 16560       // Line sync pulse width (ns)
#define VISIBLE_WIDTH_NS 34560     // Period of active video (ns)

// Implied period from start of line to start of active video (ns)
#define BACK_PORCH_WIDTH_NS (LINE_PERIOD_NS - VISIBLE_WIDTH_NS)

// Implied dot period. Ideally this implies a line is an integer number of dots.
#define DOT_CLOCK_PERIOD_NS (VISIBLE_WIDTH_NS / VISIBLE_DOTS_PER_LINE)

#define SYNC_CLOCK_PERIOD_NS DOT_CLOCK_PERIOD_NS

// Timing program for a blank line
alignas(8) uint32_t sync_timing_blank_line[] = {
    sync_timing_encode(1, 0, HSYNC_WIDTH_NS, SIDE_EFFECT_NOP, SYNC_CLOCK_PERIOD_NS),
    sync_timing_encode(0, 0, LINE_PERIOD_NS - HSYNC_WIDTH_NS, SIDE_EFFECT_NOP, SYNC_CLOCK_PERIOD_NS),
};
#define SYNC_TIMING_BLANK_LINE_LEN                                                                 \
  (sizeof(sync_timing_blank_line) / sizeof(sync_timing_blank_line[0]))

// Timing program for a vsync line
alignas(8) uint32_t sync_timing_vsync_line[] = {
    sync_timing_encode(1, 1, HSYNC_WIDTH_NS, SIDE_EFFECT_NOP, SYNC_CLOCK_PERIOD_NS),
    sync_timing_encode(0, 1, LINE_PERIOD_NS - HSYNC_WIDTH_NS, SIDE_EFFECT_NOP, SYNC_CLOCK_PERIOD_NS),
};
#define SYNC_TIMING_VSYNC_LINE_LEN                                                                 \
  (sizeof(sync_timing_vsync_line) / sizeof(sync_timing_vsync_line[0]))

// Timing program for a visible line
alignas(16) uint32_t sync_timing_visible_line[] = {
    sync_timing_encode(1, 0, BACK_PORCH_WIDTH_NS, SIDE_EFFECT_NOP, SYNC_CLOCK_PERIOD_NS),
    sync_timing_encode(1, 0, HSYNC_WIDTH_NS - BACK_PORCH_WIDTH_NS, SIDE_EFFECT_SET_TRIGGER, SYNC_CLOCK_PERIOD_NS),
    sync_timing_encode(0, 0, 16 * SYNC_CLOCK_PERIOD_NS, SIDE_EFFECT_CLEAR_TRIGGER, SYNC_CLOCK_PERIOD_NS),
    sync_timing_encode(0, 0, LINE_PERIOD_NS - HSYNC_WIDTH_NS - (16 * SYNC_CLOCK_PERIOD_NS), SIDE_EFFECT_NOP, SYNC_CLOCK_PERIOD_NS),
};
#define SYNC_TIMING_VISIBLE_LINE_LEN                                                               \
  (sizeof(sync_timing_visible_line) / sizeof(sync_timing_visible_line[0]))

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
  static uint phase = 0;

  dma_channel_acknowledge_irq0(sync_timing_dma_channel);

  switch (phase) {
  case 0:
    // VSYNC
    channel_config_set_ring(&sync_timing_dma_channel_config, false, 3);
    dma_channel_set_config(sync_timing_dma_channel, &sync_timing_dma_channel_config, false);
    dma_channel_transfer_from_buffer_now(sync_timing_dma_channel, sync_timing_vsync_line,
                                         SYNC_TIMING_VSYNC_LINE_LEN * VSYNC_LINES_PER_FRAME);

    // Start frame buffer transfer for the next field.
    dma_channel_transfer_from_buffer_now(video_dma_channel, (void *)atomic_load(&frame_buffer_ptr),
                                         VISIBLE_LINES_PER_FRAME * (VISIBLE_DOTS_PER_LINE >> 4));

    phase = 1;
    break;
  case 1:
    // blank line
    channel_config_set_ring(&sync_timing_dma_channel_config, false, 3);
    dma_channel_set_config(sync_timing_dma_channel, &sync_timing_dma_channel_config, false);
    dma_channel_transfer_from_buffer_now(sync_timing_dma_channel, sync_timing_blank_line,
                                         SYNC_TIMING_BLANK_LINE_LEN *
                                             (VERT_VISIBLE_START_LINE - VSYNC_LINES_PER_FRAME));
    phase = 2;
    break;
  case 2:
    // visible line
    channel_config_set_ring(&sync_timing_dma_channel_config, false, 4);
    dma_channel_set_config(sync_timing_dma_channel, &sync_timing_dma_channel_config, false);
    dma_channel_transfer_from_buffer_now(sync_timing_dma_channel, sync_timing_visible_line,
                                         SYNC_TIMING_VISIBLE_LINE_LEN * VISIBLE_LINES_PER_FRAME);
    phase = 3;
    break;
  case 3:
    // blank line
    channel_config_set_ring(&sync_timing_dma_channel_config, false, 3);
    dma_channel_set_config(sync_timing_dma_channel, &sync_timing_dma_channel_config, false);
    dma_channel_transfer_from_buffer_now(
        sync_timing_dma_channel, sync_timing_blank_line,
        SYNC_TIMING_BLANK_LINE_LEN *
            (LINES_PER_FRAME - VERT_VISIBLE_START_LINE - VISIBLE_LINES_PER_FRAME));
    phase = 0;
    break;
  }
}

// This function contains all static asserts. It's never called but the compiler will raise a
// diagnostic if the assertions fail.
static inline void all_static_asserts() {
  // Dot clock period should be integer number of nanoseconds.
  static_assert(VISIBLE_WIDTH_NS % VISIBLE_DOTS_PER_LINE == 0);

  // Dot clock should evenly divide the line.
  static_assert(LINE_PERIOD_NS % DOT_CLOCK_PERIOD_NS == 0);

  // Check that the number of *visible* dots per line is a multiple of 16.
  static_assert((VISIBLE_DOTS_PER_LINE & 0xf) == 0);

  // Our timing program assumes this.
  static_assert(BACK_PORCH_WIDTH_NS < HSYNC_WIDTH_NS);

  // Statically assert alignment and length of timing programs. Alignment is necessary to allow DMA
  // in ring mode and the length needs to be known because we need to set the number of significan
  // bits in ring mode.
  static_assert(alignof(sync_timing_blank_line) == sizeof(sync_timing_blank_line));
  static_assert(SYNC_TIMING_BLANK_LINE_LEN == 2);
  static_assert(alignof(sync_timing_visible_line) == sizeof(sync_timing_visible_line));
  static_assert(SYNC_TIMING_VISIBLE_LINE_LEN == 4);
  static_assert(alignof(sync_timing_vsync_line) == sizeof(sync_timing_vsync_line));
  static_assert(SYNC_TIMING_VSYNC_LINE_LEN == 2);

  // Vertical timing must make sense too.
  static_assert(LINES_PER_FRAME > (VSYNC_LINES_PER_FRAME + VISIBLE_LINES_PER_FRAME));
  static_assert(LINES_PER_FRAME > (VERT_VISIBLE_START_LINE + VISIBLE_LINES_PER_FRAME));
}

void videoout_init(PIO pio, uint sync_pin_base, uint video_pin_base) {
  // Record which PIO instance is used.
  pio_instance = pio;

  // Ensure IRQ 4 of the PIO is clear
  pio_interrupt_clear(pio_instance, 4);

  // Configure and enable output program
  video_output_offset = pio_add_program(pio_instance, &video_output_program);
  video_output_sm = pio_claim_unused_sm(pio_instance, true);
  video_output_program_init(pio_instance, video_output_sm, video_output_offset, video_pin_base,
                            DOT_CLOCK_PERIOD_NS);

  // Configure and enable timing program.
  sync_timing_offset = pio_add_program(pio_instance, &sync_timing_program);
  sync_timing_sm = pio_claim_unused_sm(pio_instance, true);
  sync_timing_program_init(pio_instance, sync_timing_sm, sync_timing_offset, sync_pin_base,
                           SYNC_CLOCK_PERIOD_NS);

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

  // Enable interrupt handler for frame timing.
  irq_set_exclusive_handler(DMA_IRQ_0, sync_timing_dma_handler);
}

void videoout_start(void) {
  sem_init(&vblank_semaphore, 0, 1);

  pio_sm_put(pio_instance, video_output_sm, VISIBLE_DOTS_PER_LINE - 1);
  pio_sm_set_enabled(pio_instance, video_output_sm, true);
  pio_sm_set_enabled(pio_instance, sync_timing_sm, true);

  // Start frame timing.
  irq_set_enabled(DMA_IRQ_0, true);
  sync_timing_dma_handler();
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

uint videoout_get_screen_width(void) { return VISIBLE_DOTS_PER_LINE; }

uint videoout_get_screen_height(void) { return VISIBLE_LINES_PER_FRAME; }

uint videoout_get_screen_stride(void) { return (VISIBLE_DOTS_PER_LINE >> 4) << 2; }

void videoout_set_frame_buffer(void *frame_buffer) {
  atomic_store(&frame_buffer_ptr, (uintptr_t)frame_buffer);
}

void videoout_wait_for_vblank(void) { sem_acquire_blocking(&vblank_semaphore); }
