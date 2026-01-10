#include <stdio.h>
#include "pico/stdlib.h"
#include "spi_capture.h"
#include "decoder.h"
#include "pico/multicore.h"

spi_frame_t frame;

void lambda_capture_callback()
{
    while(1)
    {    
        update_screendata_callback();
    }
}


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

    multicore_launch_core1(lambda_capture_callback);    

    while (1)
    {
        printf("U = %04d cV\t", screendata.voltage);
        
        printf("position editing: row=%d digit=%d\n", screendata.editing_row, screendata.editing_digit);

        sleep_ms(50);
    }

}
