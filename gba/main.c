#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <seven/prelude.h>
#include <seven/bios.h>
#include <seven/dma.h>
#include <seven/timer.h>
#include <seven/video.h>
#include <seven/video/bg_bitmap.h>
#include <seven/video/bg_transform.h>
#include <seven/waitstate.h>

// libseven doesn't have sound support
#define REG_SOUNDCNT_H VOLADDR(0x04000082, uint16_t)
#define REG_SOUNDCNT_X VOLADDR(0x04000084, uint16_t)

#define REG_FIFO_A     VOLADDR(0x040000A0, uint32_t)
#define REG_FIFO_B     VOLADDR(0x040000A4, uint32_t)

enum SoundControlH {
    #define BF_SOUND_1_4_VOLUME_OFF 0
    #define BF_SOUND_1_4_VOLUME_LEN 2

    #define SOUND_1_4_VOLUME(n) BITFIELD(SOUND_1_4_VOLUME, (n))

    SOUND_1_4_VOLUME_25    = SOUND_1_4_VOLUME(0),
    SOUND_1_4_VOLUME_50    = SOUND_1_4_VOLUME(1),
    SOUND_1_4_VOLUME_100   = SOUND_1_4_VOLUME(2),

    SOUND_DMA_A_VOLUME_50  = 0,
    SOUND_DMA_A_VOLUME_100 = BIT(2),
    SOUND_DMA_B_VOLUME_50  = 0,
    SOUND_DMA_B_VOLUME_100 = BIT(3),

    SOUND_DMA_A_RIGHT      = BIT(8),
    SOUND_DMA_A_LEFT       = BIT(9),
    SOUND_DMA_A_TIMER_0    = 0,
    SOUND_DMA_A_TIMER_1    = BIT(10),
    SOUND_DMA_A_FIFO_RESET = BIT(11),

    SOUND_DMA_B_RIGHT      = BIT(12),
    SOUND_DMA_B_LEFT       = BIT(13),
    SOUND_DMA_B_TIMER_0    = 0,
    SOUND_DMA_B_TIMER_1    = BIT(14),
    SOUND_DMA_B_FIFO_RESET = BIT(15),
};

enum SoundControlX {
    SOUND_1_ACTIVE      = BIT(0),
    SOUND_2_ACTIVE      = BIT(1),
    SOUND_3_ACTIVE      = BIT(2),
    SOUND_4_ACTIVE      = BIT(3),

    SOUND_MASTER_ENABLE = BIT(7),
};

struct CartAPI {
    volatile uint32_t fb_addr;
    volatile uint16_t vblank_flag;
    volatile uint16_t fb_pa, fb_pd;
    volatile uint16_t fb_height;

    volatile uint32_t audio_addr[2];
    volatile uint16_t audio_buf_size;
    volatile uint16_t audio_timer;

    volatile uint16_t buttons;
};

// storage for API
__attribute ((section(".crt0.postheader")))
uint8_t api_data[32];

static bool audio_started = false;
static int audio_buf = 0;

 __attribute__ ((section(".iwram")))
void audio_timer_irq(uint16_t) {
    struct CartAPI *cart_api = (struct CartAPI *)(MEM_ROM + 0xC0);
    cart_api->audio_addr[audio_buf] = 0;
    audio_buf ^= 1;

    // restart DMA from next buffer
    REG_DMA1SRC = (void *)cart_api->audio_addr[audio_buf];
    REG_DMA1CNT ^= DMA_ENABLE;
    REG_DMA1CNT ^= DMA_ENABLE;
}

int main() {
    // values used by official software (according to gbatek)
    // (default would be 4/2)
    //REG_WAITCNT = WAIT_ROM_N_3 | WAIT_ROM_S_1 | WAIT_PREFETCH_ENABLE;
    REG_WAITCNT = WAIT_ROM_S_1 | WAIT_PREFETCH_ENABLE;

    REG_DISPCNT = DISPLAY_MODE_BITMAP | DISPLAY_BG2_ENABLE;

    // enable irq/vblank
    irqInitVectored();
    irqEnable(IRQ_VBLANK);
    REG_DISPSTAT = LCD_VBLANK_IRQ_ENABLE;

    struct CartAPI *cart_api = (struct CartAPI *)(MEM_ROM + 0xC0);

    // audio setup
    if(cart_api->audio_timer) {
        REG_SOUNDCNT_H = SOUND_DMA_A_VOLUME_100 | SOUND_DMA_A_RIGHT | SOUND_DMA_A_RIGHT | SOUND_DMA_A_TIMER_0 | SOUND_DMA_A_FIFO_RESET;
        REG_SOUNDCNT_X = SOUND_MASTER_ENABLE;

        REG_DMA1DST = (void *)&REG_FIFO_A;
        REG_DMA1CNT = DMA_DST_INCREMENT | DMA_SRC_INCREMENT | DMA_REPEAT | DMA_32BIT | DMA_START_SOUND;// | DMA_ENABLE;

        // sample rate timer
        REG_TM0VAL = cart_api->audio_timer; // pre-calculated

        // cascade to count samples
        REG_TM1VAL = 0x10000 - cart_api->audio_buf_size;
        REG_TM1CNT = TIMER_FREQ_CASCADE | TIMER_IRQ_ENABLE | TIMER_ENABLE;

        irqEnable(IRQ_TIMER_1);
        irqCallbackSet(IRQ_TIMER_1, audio_timer_irq);
    }

    while(true) {
        cart_api->buttons = REG_KEYINPUT;

        if(!audio_started && cart_api->audio_addr[0]) {
            // have first audio buffer, start audio
            REG_FIFO_A = *(uint32_t *)cart_api->audio_addr[0]; // first 4 samples
            REG_DMA1SRC = (void *)cart_api->audio_addr[0] + 4;
            REG_DMA1CNT |= DMA_ENABLE;
            REG_TM0CNT = TIMER_ENABLE;
            audio_started = true;
        }

        uint16_t *fb_ptr = (uint16_t *)cart_api->fb_addr;
        if(fb_ptr) {
            REG_BG2PA = cart_api->fb_pa;
            REG_BG2PD = cart_api->fb_pd;

            // break up screen copy to not miss interrupts
            uint16_t *out = (uint16_t *)MODE3_FRAME;
            uint16_t *in = fb_ptr;

            for(int i = 0; i < cart_api->fb_height; i++) {
                struct DmaTransfer screen_dma = {in, out, 240, DMA_16BIT | DMA_ENABLE};
                dmaSet(3, screen_dma);
                out += 240;
                in += 240;
            }

            cart_api->fb_addr = 0;
        }

        cart_api->vblank_flag = 1;

        biosVBlankIntrWait();
    }
}
