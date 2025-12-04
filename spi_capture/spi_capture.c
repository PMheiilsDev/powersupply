#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "spi_capture.h"

#define CS_MASK   (1u << PIN_CS)
#define CLK_MASK  (1u << PIN_CLK)
#define DIO_MASK  (1u << PIN_DIO)

static inline uint32_t gpio_fast() {
    return sio_hw->gpio_in;
}


void /*__not_in_flash_func*/(spi_capture_blocking)(spi_frame_t *frame)
{
    volatile uint8_t *dst = frame->data;
    uint32_t len = 0;

    frame->length = 0;

    // Wait CS low
    while (gpio_fast() & CS_MASK) tight_loop_contents();

    uint8_t byte = 0;
    uint32_t bitcount = 0;

    uint32_t flags = save_and_disable_interrupts();

    uint32_t last = gpio_fast();

    while (!(gpio_fast() & CS_MASK))
    {
        uint32_t now = gpio_fast();

        // Faster rising-edge detection
        if ( ((~last) & now) & CLK_MASK )
        {
            byte = (byte >> 1) | (((now & DIO_MASK) ? 1 : 0) << 7);
            bitcount++;

            if (bitcount == 8)
            {
                dst[len++] = byte;
                bitcount = 0;
                byte = 0;
            }
        }

        last = now;
    }

    if (bitcount)
    {
        byte <<= (8 - bitcount);
        dst[len++] = byte;
    }

    restore_interrupts(flags);

    frame->length = len;
}
