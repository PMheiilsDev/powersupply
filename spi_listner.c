#include <stdio.h>
#include "pico/stdlib.h"
#include "spi_capture.h"
#include "decoder.h"

spi_frame_t frame;

int main()
{
    stdio_init_all();

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


    uint32_t cs_count = 0;
    uint32_t clk_count = 0;
    uint32_t dio_count = 0;


    while (1)
    {
        spi_capture_blocking(&frame);

        // print frame
        if ( (frame.length == 17 && frame.data[0] == 0xC0) )
        {
            // printf("Frame (%lu bytes): ", frame.length);
            // for (uint32_t i = 0; i < frame.length; i++)
            // {
            //     printf("%02X ", frame.data[i]);
            // }
            // printf("\n");

            // decode voltage digit 2 as test 
            printf("Decoding Voltage ROW: ");
            uint8_t data[4];
            for (uint8_t i = 0; i < 4; i++) 
            {
                data[i] = get_digit_of_segment((frame.data + 1), VOLTAGE, (digit_t)i);
                if (data[i] == 0xFF)
                {
                    printf("X"); 
                }
                else
                {
                    printf("%d", data[i]);
                }
            }
            printf("\n");

        }
    }
}
