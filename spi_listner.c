#include <stdio.h>
#include "pico/stdlib.h"
#include "spi_capture.h"
#include "decoder.h"
#include "pico/multicore.h"
#include "controller.h"

volatile int target = 0;


int main()
{
    // gpio_init(25);
    // gpio_set_dir(25, GPIO_OUT);

    stdio_init_all();

    spi_capture_init();
    controller_init();

    multicore_launch_core1(lambda_capture_callback);    

    while (1)
    {
        printf("U = %04de%dV\t", screendata.rows[VOLTAGE].fac, screendata.rows[VOLTAGE].exp);
        printf("I = %04de%dA\t", screendata.rows[CURRENT].fac, screendata.rows[CURRENT].exp);
        printf("P = %04de%dW\n", screendata.rows[POWER].fac, screendata.rows[POWER].exp);
        printf("Editing: row=%d digit=%d\n", screendata.editing_row, screendata.editing_digit);

        // printf("position editing: row=%d digit=%d\n", screendata.editing_row, screendata.editing_digit);
        
        sleep_ms(2*1000);
        
        // rotate_digit_voltage(true, 100);
        // gpio_put(25, 1);
        // // sleep_ms(250);
        // change_digit_voltage();
        // gpio_put(25, 0);
        // continue;
        
        // reach target
        int distance = target - screendata.rows[VOLTAGE].fac;

        if (distance != 0)
        {
            do
            {
                change_digit_voltage();
                wait_for_spi_capture();
            }
            while( (screendata.editing_digit != DIGIT_3) || (screendata.editing_row != VOLTAGE) );
        }
        // sleep_ms(0);
        if (distance < 0)
        {
            printf("moving down by %d", -distance );
            rotate_digit_voltage(false, -distance);
        }
        else if ( distance > 0)
        {
            printf("moving up by %d", distance );
            rotate_digit_voltage(true, distance);
        }
    }

}
