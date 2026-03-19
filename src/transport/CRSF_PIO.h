#pragma once
#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/dma.h"

// PIO UART RX Program
static uint16_t crsf_uart_rx_program_instructions[] = {
            //     .wrap_target
    0x2020, //  0: wait   0 pin, 0                   
    0xea27, //  1: set    x, 7                   [10]
    0x4001, //  2: in     pins, 1                    
    0x0642, //  3: jmp    x--, 2                 [6] 
    0x8020, //  4: push   block                      
            //     .wrap
};

static const struct pio_program crsf_uart_rx_program = {
    .instructions = crsf_uart_rx_program_instructions,
    .length = 5,
    .origin = -1,
};

class CRSFTransport {
public:
    static const int BUFFER_SIZE = 256;
    static const uint32_t DMA_INIT_COUNT = 0xF0000000; // Use large multiple of 256
    
    void begin(uint32_t pin, uint32_t baud) {
        _pin = pin;
        _pio = pio0;
        _sm = pio_claim_unused_sm(_pio, true);
        uint offset = pio_add_program(_pio, &crsf_uart_rx_program);

        pio_gpio_init(_pio, pin); // Correct GPIO init
        
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_in_pins(&c, pin);
        sm_config_set_jmp_pin(&c, pin);
        sm_config_set_in_shift(&c, true, false, 0);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
        
        float div = (float)clock_get_hz(clk_sys) / (8.0f * (float)baud);
        sm_config_set_clkdiv(&c, div);
        
        pio_sm_init(_pio, _sm, offset, &c);
        pio_sm_set_enabled(_pio, _sm, true);

        // Setup DMA
        _dma_chan = dma_claim_unused_channel(true);
        dma_channel_config dc = dma_channel_get_default_config(_dma_chan);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
        channel_config_set_read_increment(&dc, false);
        channel_config_set_write_increment(&dc, true);
        channel_config_set_dreq(&dc, pio_get_dreq(_pio, _sm, false));
        channel_config_set_ring(&dc, true, 8); // 256 byte ring buffer

        dma_channel_configure(
            _dma_chan, &dc,
            _rx_buffer,     // Dest
            &_pio->rxf[_sm], // Source
            DMA_INIT_COUNT, // Large predictable count
            true            // Start
        );
    }

    uint32_t available() {
        uint32_t remaining = dma_hw->ch[_dma_chan].transfer_count;
        uint32_t curr_pos = (DMA_INIT_COUNT - remaining) % BUFFER_SIZE;
        
        if (curr_pos >= _read_ptr) return curr_pos - _read_ptr;
        return (BUFFER_SIZE - _read_ptr) + curr_pos;
    }

    uint8_t read() {
        uint8_t b = _rx_buffer[_read_ptr];
        _read_ptr = (_read_ptr + 1) % BUFFER_SIZE;
        return b;
    }

private:
    uint32_t _pin;
    PIO _pio;
    uint _sm;
    int _dma_chan;
    uint8_t _rx_buffer[256] __attribute__((aligned(256)));
    uint32_t _read_ptr = 0;
};
