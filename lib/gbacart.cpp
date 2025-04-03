#include <cstring>

#include "gbacart.h"

#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#include "gba-cart.pio.h"

alignas(4) extern char _binary_gba_rom_gba_start[];
static auto rom_ptr = _binary_gba_rom_gba_start;

static constexpr int cs_pin = CS_PIN;
static constexpr int rd_pin = RD_PIN;
static constexpr int wr_pin = WR_PIN;
static constexpr int addr_bits = ADDR_BITS;

static uint32_t rom_addr;

// SMs and DMA channels accessed in IRQ handlers should be pre-allocated constants
static constexpr int rom_cs_sm = 0, rom_rd_sm = 1;
static int rom_wr_sm;
static const PIO gba_cart_pio = pio0;
static uint8_t rom_cs_offset, rom_rd_offset, rom_wr_offset;

static constexpr int rom_read_dma_channel = 0, rom_write_dma_channel = 1;
static int rom_addr_dma_channel, rom_addr_sniff_dma_channel;

static void __not_in_flash_func(dma_irq_handler)() {
    dma_sniffer_set_data_accumulator((uint32_t)rom_ptr);
    dma_channel_acknowledge_irq0(rom_addr_dma_channel);

    // also set up the write channel (less time sensitive)
    dma_channel_set_write_addr(rom_write_dma_channel, rom_ptr + rom_addr, true);
}

static void __not_in_flash_func(pio_irq0_handler)() {
    // abort DMA and clear FIFO

    // this saves a couple of instructions
    static const auto dma_abort = &dma_hw->abort;
    *dma_abort = 1u << rom_read_dma_channel | 1u << rom_write_dma_channel;

    // assume TX FIFO was full so no transfers were left in flight
    //while(dma_channel_hw_addr(rom_read_dma_channel)->ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS);
    
    pio_interrupt_clear(gba_cart_pio, 0);
    pio_sm_clear_fifos(gba_cart_pio, rom_rd_sm);

    // GBA might have requested a bad address
    // happens during boot
    hw_set_bits(&dma_hw->ch[rom_read_dma_channel].al1_ctrl, DMA_CH0_CTRL_TRIG_READ_ERROR_BITS);
}

static void pio_init() {
    pio_claim_sm_mask(gba_cart_pio, 1 << rom_cs_sm | 1 << rom_rd_sm);

    rom_wr_sm = pio_claim_unused_sm(gba_cart_pio, true);

    // cs
    auto offset = rom_cs_offset = pio_add_program(gba_cart_pio, &gba_rom_cs_program);
    auto cfg = gba_rom_cs_program_get_default_config(offset);

    sm_config_set_in_shift(&cfg, false, true, addr_bits + 1);

    pio_sm_init(gba_cart_pio, rom_cs_sm, offset, &cfg);

    // rd
    offset = rom_rd_offset = pio_add_program(gba_cart_pio, &gba_rom_rd_program);
    cfg = gba_rom_rd_program_get_default_config(offset);

    sm_config_set_out_pins(&cfg, 0, 16);
    sm_config_set_out_shift(&cfg, false, false, 16);

    pio_sm_init(gba_cart_pio, rom_rd_sm, offset, &cfg);

    // wr
    offset = rom_wr_offset = pio_add_program(gba_cart_pio, &gba_rom_wr_program);
    cfg = gba_rom_wr_program_get_default_config(offset);

    sm_config_set_in_shift(&cfg, false, true, 16);

    pio_sm_init(gba_cart_pio, rom_wr_sm, offset, &cfg);

    // init all io
    auto mask = ((1 << addr_bits) - 1) | 1 << cs_pin | 1 << rd_pin | 1 << wr_pin;
    pio_sm_set_pins(gba_cart_pio, rom_cs_sm, 0);
    pio_sm_set_consecutive_pindirs(gba_cart_pio, rom_cs_sm, 0, 32, false);
    pio_sm_set_pindirs_with_mask(gba_cart_pio, rom_cs_sm, 0, mask);

    // address/data
    for(int i = 0; i < addr_bits; i++)
        pio_gpio_init(gba_cart_pio, i);

    pio_gpio_init(gba_cart_pio, cs_pin);
    pio_gpio_init(gba_cart_pio, rd_pin);
    pio_gpio_init(gba_cart_pio, wr_pin);

    // bypass synchroniser
    hw_set_bits(&gba_cart_pio->input_sync_bypass, mask);
}

static void dma_init() {
    dma_claim_mask(1 << rom_read_dma_channel | 1 << rom_write_dma_channel);
    rom_addr_dma_channel = dma_claim_unused_channel(true);
    rom_addr_sniff_dma_channel = dma_claim_unused_channel(true);

    // read data
    auto config = dma_channel_get_default_config(rom_read_dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_16);
    channel_config_set_dreq(&config, pio_get_dreq(gba_cart_pio, rom_rd_sm, true));
    dma_channel_configure(rom_read_dma_channel, &config, &gba_cart_pio->txf[rom_rd_sm], rom_ptr, 0x10000, false);

    // write data
    config = dma_channel_get_default_config(rom_write_dma_channel);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_16);
    channel_config_set_dreq(&config, pio_get_dreq(gba_cart_pio, rom_wr_sm, false));
    dma_channel_configure(rom_write_dma_channel, &config, rom_ptr, &gba_cart_pio->rxf[rom_wr_sm], 0x10000, false);

    // read address through sniffer to add base ptr
    // destination also used for write setup 
    config = dma_channel_get_default_config(rom_addr_dma_channel);
    channel_config_set_dreq(&config, pio_get_dreq(gba_cart_pio, rom_cs_sm, false));
    channel_config_set_read_increment(&config, false);

    // chain to sniffer read
    channel_config_set_chain_to(&config, rom_addr_sniff_dma_channel);

    dma_channel_configure(rom_addr_dma_channel, &config, &rom_addr, &gba_cart_pio->rxf[rom_cs_sm], 1, false);

    // transfer sniffed address to trigger read channel
    config = dma_channel_get_default_config(rom_addr_sniff_dma_channel);
    channel_config_set_read_increment(&config, false);

    // chain to addr read (keep the address reads going forever)
    channel_config_set_chain_to(&config, rom_addr_dma_channel);

    dma_channel_configure(rom_addr_sniff_dma_channel, &config, &dma_hw->ch[rom_read_dma_channel].al3_read_addr_trig, &dma_hw->sniff_data, 1, false);

    // sniffer
    dma_sniffer_enable(rom_addr_dma_channel, 0xF/*addition*/, true);
    dma_sniffer_set_data_accumulator((uint32_t)rom_ptr);

    // irq to reset sniff
    dma_channel_set_irq0_enabled(rom_addr_dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

void gbacart_init() {
    // copy rom data
    extern char __gba_rom_data_start;
    extern char __gba_rom_data_end;
    extern char __gba_rom_data_init;

    memcpy(&__gba_rom_data_start, &__gba_rom_data_init, &__gba_rom_data_end - &__gba_rom_data_start);

    pio_init();
    dma_init();

    // cs high
    pio_set_irq0_source_enabled(gba_cart_pio, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq0_handler);
    irq_set_enabled(PIO0_IRQ_0, true);
}

void gbacart_start(bool wait_power) {

    if(wait_power) {
        auto wait_high = pio_encode_wait_gpio(1, rd_pin);
        pio_sm_exec(gba_cart_pio, rom_cs_sm, wait_high);
        pio_sm_exec(gba_cart_pio, rom_rd_sm, wait_high);
        pio_sm_exec(gba_cart_pio, rom_wr_sm, wait_high);
    }

    dma_channel_start(rom_addr_dma_channel);
    pio_set_sm_mask_enabled(gba_cart_pio, 1 << rom_cs_sm | 1 << rom_rd_sm | 1 << rom_wr_sm, true);
}

struct CartAPI *gbacart_get_api() {
    return reinterpret_cast<CartAPI *>(rom_ptr + 0xC0);
}

uint32_t gbacart_to_gba_addr(void *ptr) {
    // this assumes the "ROM" data is before anything we want to pass through
    return reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(rom_ptr) + 0x8000000;
}