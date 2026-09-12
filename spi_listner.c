#include <stdio.h>
#include "pico/stdlib.h"
#include "spi_capture.h"
#include "decoder.h"
#include "pico/multicore.h"
#include "controller.h"
#include "scpi_control.h"


static uint32_t state = 0x12345678;
uint32_t my_ctr = 1;

uint32_t random32(void)
{
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

volatile int target = 0;
volatile uint8_t digit = 0;

volatile bool wait_for_debugger = false;

int main()
{
    sleep_ms(10);

    while(wait_for_debugger)
    {
        sleep_ms(100);
    }

    // gpio_init(25);
    // gpio_set_dir(25, GPIO_OUT);

    stdio_init_all();
    scpi_control_init();

    spi_capture_init();
    controller_init();

    multicore_launch_core1(lambda_capture_callback);    

    while (1)
    {
        scpi_control_process();
        sleep_ms(1);
    }

}
