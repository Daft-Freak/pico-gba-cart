#include <cmath>
#include <cstdio>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"

#include "gbacart.h"

static uint16_t fb[240 * 160];

// must be a multiple of 16
static constexpr int audio_buf_size = 544;
static int8_t sine_buf[audio_buf_size];

// run update code from RAM to avoid interrupt delays from XIP cache misses
// ... at least, I think that's what happens...
void __no_inline_not_in_flash_func(update)() {
    static int cursor_x = 0, cursor_y = 0;
    auto cart_api = gbacart_get_api();

    // update audio buffers
    if(!cart_api->audio_addr[0])
        cart_api->audio_addr[0] = gbacart_to_gba_addr(sine_buf);

    if(!cart_api->audio_addr[1])
        cart_api->audio_addr[1] = gbacart_to_gba_addr(sine_buf);

    // draw something to framebuffer
    if(cart_api->vblank_flag) {
        auto button_state = ~cart_api->buttons;
    
        if(button_state & (1 << 5)) // left
            cursor_x = cursor_x == 0 ? 239 : cursor_x - 1;
        else if(button_state & (1 << 4)) // right
            cursor_x = cursor_x == 239 ? 0 : cursor_x + 1;

        if(button_state & (1 << 6)) // up
            cursor_y = cursor_y == 0 ? 159 : cursor_y - 1;
        else if(button_state & (1 << 7)) // down
            cursor_y = cursor_y == 159 ? 0 : cursor_y + 1;

        for(int y = 0; y < 160; y++) {
            for(int x = 0; x < 240; x++) {
                fb[x + y * 240] = (x == cursor_x || y == cursor_y) ? 0xFFFF : 0;
            }
        }

        cart_api->vblank_flag = 0;
        cart_api->fb_addr = gbacart_to_gba_addr(fb);
    }
}

int main() {
    set_sys_clock_khz(250000, true);
    // lowest stable?
    //set_sys_clock_khz(195000, true);

    //stdio_init_all();

    for(int i = 0; i < audio_buf_size; i++)
        sine_buf[i] = std::sin((float(i) / audio_buf_size * 16) * M_PI) * 127;

    gbacart_init();

    auto cart_api = gbacart_get_api();

    // 1x scale, full height
    cart_api->fb_pa = 1 << 8;
    cart_api->fb_pd = 1 << 8;
    cart_api->fb_height = 160;

    cart_api->audio_buf_size = audio_buf_size;
    cart_api->audio_timer = 0x10000 - ((1 << 24) / 32768); // 32768Hz

    gbacart_start(true);

    while(true) {
        update();
        __wfe();
    }

    return 0;
}
