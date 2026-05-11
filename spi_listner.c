#include <stdio.h>
#include "pico/stdlib.h"
#include "spi_capture.h"
#include "decoder.h"
#include "pico/multicore.h"



int main()
{
    stdio_init_all();


    multicore_launch_core1(lambda_capture_callback);    

    while (1)
    {
        printf("U = %04de%dV\n", screendata.rows[VOLTAGE].fac, screendata.rows[VOLTAGE].exp);
        
        // printf("position editing: row=%d digit=%d\n", screendata.editing_row, screendata.editing_digit);

        sleep_ms(50);
    }

}
