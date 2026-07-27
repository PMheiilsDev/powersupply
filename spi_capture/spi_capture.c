#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "spi_capture.h"

#include <math.h>

#define CS_MASK   (1u << PIN_CS)
#define CLK_MASK  (1u << PIN_CLK)
#define DIO_MASK  (1u << PIN_DIO)

static inline uint32_t gpio_fast() 
{
    return sio_hw->gpio_in;
}

spi_frame_t frame;

volatile int wait_for_spi_capture_sem = 0;

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


void update_screendata_callback()
{
    spi_frame_t frame;
    spi_capture_blocking(&frame);

    if ( (frame.length == 17 && frame.data[0] == 0xC0) ) 
    {

        // get voltage 
        // todo this still only works if the decimal point is at the expected place

        uint16_t voltage_fac = 0;

        for (uint8_t digit = 0; digit < DIGIT_LEN; digit++) 
        {
            uint8_t data = get_digit_of_segment((frame.data + 1), VOLTAGE, (digit_t)digit);
            if (data == 0xFF)
            {
                screendata.editing_row = VOLTAGE;
                screendata.editing_digit = (digit_t)digit; 
                return;
            }
            else
            {
                voltage_fac += data * pow((uint16_t)10, (uint16_t)(3-digit) );
            }

        }

        screendata.rows[VOLTAGE].fac = voltage_fac;



    }
}

void spi_capture_init()
{
    // configure pins as input
    gpio_init(PIN_DIO);
    gpio_set_dir(PIN_DIO, GPIO_IN);
    gpio_init(PIN_CLK);
    gpio_set_dir(PIN_CLK, GPIO_IN);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_IN);
    gpio_pull_up(PIN_DIO);
    gpio_pull_up(PIN_CLK);
    gpio_pull_up(PIN_CS);

}


void lambda_capture_callback()
{
    while(1)
    {   
        // spi_frame_t frame;
        spi_capture_blocking(&frame);

        if ( (frame.length == 17 && frame.data[0] == 0xC0) ) 
        {

            get_value((frame.data+1), &(screendata.rows[VOLTAGE]), VOLTAGE);
            get_value((frame.data+1), &(screendata.rows[CURRENT]), CURRENT);
            get_value((frame.data+1), &(screendata.rows[POWER]), POWER);

        }

        // update_screendata_callback();
    }
}

void wait_for_spi_capture()
{
    wait_for_spi_capture_sem = 1;
    while(wait_for_spi_capture_sem)
    {
        tight_loop_contents();
    }
}

