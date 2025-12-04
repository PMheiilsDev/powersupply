#include <stdio.h>
#include "pico/stdlib.h"
#include "spi_capture.h"
#include "decoder.h"


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

    spi_frame_t frame;

    uint32_t cs_count = 0;
    uint32_t clk_count = 0;
    uint32_t dio_count = 0;

    // while(1)
    // {
        
    //     if ( gpio_get(PIN_CS) == 0 )
    //     {
    //         cs_count++;
    //     }
    //     if ( gpio_get(PIN_CLK) == 0 )
    //     {
    //         clk_count++;
    //     }
    //     if ( gpio_get(PIN_DIO) == 0 )
    //     {
    //         dio_count++;
    //     }
    //     if ( gpio_get(PIN_CS) == 1 )
    //     {
    //         printf("CS low count: %lu\n", cs_count);
    //         printf("CLK low count: %lu\n", clk_count);
    //         printf("DIO low count: %lu\n", dio_count);
    //     }
    // }

    while (1)
    {
        spi_capture_blocking(&frame);

        // print frame
        if ( frame.length > 5 )
        {
            printf("Frame (%lu bytes): ", frame.length);
            for (uint32_t i = 0; i < frame.length; i++)
            {
                printf("%02X ", frame.data[i]);
            }
            printf("\n");
        }
    }
}
