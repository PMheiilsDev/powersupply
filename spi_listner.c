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


    uint32_t cs_count = 0;
    uint32_t clk_count = 0;
    uint32_t dio_count = 0;


    while (1)
    {

        update_screendata_callback();
        
        printf("U = %d mV\n", screendata.voltage);

    }

}
