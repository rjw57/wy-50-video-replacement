#include "pico/types.h"
#include "hardware/pio.h"

// Opaque video mode type.
typedef struct videoout_mode videoout_mode_t;

// Standard modes.
extern videoout_mode_t videoout_mode_720_350;

// Callback to be notified of video blanking period start.
typedef void (*videoout_vblank_callback_t) (void);

// Video out uses two DMA channels claimed via dma_claim_unused_channel(), DMA IRQ 0, two PIO state
// machines and IRQ for the PIO instance containing the state machines. Pass a PIO instance to
// videoout_init() to specify which instance is used.
//
// The VSYNC GPIO is sync_pin_base, HSYNC is sync_pin_base + 1.
// The !DIM GPIO is video_pin_base, VIDEO is video_pin_base + 1.
void videoout_init(PIO pio, uint sync_pin_base, uint video_pin_base);

// Must be called with output stopped. Return true if mode set successful.
bool videoout_set_mode(const videoout_mode_t *mode);

// Start video out. videoout_init() must have been called first.
void videoout_start(void);

// Stop video out.
void videoout_stop(void);

// Cleanup TV-out after videoout_init().
void videoout_cleanup(void);

// Get screen resolution.
uint videoout_get_screen_width(void);
uint videoout_get_screen_height(void);

// Get number of *bytes* corresponding to one line in the frame buffer.
uint videoout_get_screen_stride(void);

// Set vblank callback. Pass NULL to disable.
void videoout_set_vblank_callback(videoout_vblank_callback_t callback);

// Frame buffer is laid out byte-wise sequential in memory left to right, top to bottom. Each byte
// corresponds to 4 pixels with the two MSBs being the left-most pixel. Each pair of bits
// corresponds to one pixel. The MSB being the VIDEO signal and LSB being the !DIM signal. So:
//
// | Bits | Appearance |
// |------|------------|
// | 00   | Black      |
// | 01   | Black      |
// | 10   | Dim        |
// | 11   | Bright     |
void videoout_set_frame_buffer(void *frame_buffer);

// Wait until the next vblank interval
void videoout_wait_for_vblank(void);
