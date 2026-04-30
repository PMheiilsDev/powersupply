
#include "controller.h"
#include "pico/stdlib.h"

void controller_init()
{
    gpio_init(PIN_VOLTAGE_A);
    gpio_set_dir(PIN_VOLTAGE_A, true);
    gpio_put(PIN_VOLTAGE_A, 0);
    gpio_init(PIN_VOLTAGE_B);
    gpio_set_dir(PIN_VOLTAGE_B, true);
    gpio_put(PIN_VOLTAGE_B, 0);
    gpio_init(PIN_VOLTAGE_BUTTON);
    gpio_set_dir(PIN_VOLTAGE_BUTTON, true);
    gpio_put(PIN_VOLTAGE_BUTTON, 0);
    
    // maybe wait for first spi read ? 
}

void rotate_digit_voltage_once_hard(bool up)
{
    if (up)
    {
        gpio_put(PIN_VOLTAGE_A, 1);
        sleep_us(ROT_DELAY_US);
        gpio_put(PIN_VOLTAGE_B, 1);
        sleep_us(ROT_DELAY_US);
        
        gpio_put(PIN_VOLTAGE_A, 0);
        sleep_us(ROT_DELAY_US);
        gpio_put(PIN_VOLTAGE_B, 0);
        sleep_us(ROT_DELAY_US);
    }
    else
    {
        gpio_put(PIN_VOLTAGE_B, 1);
        sleep_us(ROT_DELAY_US);
        gpio_put(PIN_VOLTAGE_A, 1);
        sleep_us(ROT_DELAY_US);
        
        gpio_put(PIN_VOLTAGE_B, 0);
        sleep_us(ROT_DELAY_US);
        gpio_put(PIN_VOLTAGE_A, 0);
        sleep_us(ROT_DELAY_US);
    }
}


void rotate_digit_voltage(bool up, uint16_t n)
{
    for ( int i = 0; i < n; i++ )
    {
        rotate_digit_voltage_once_hard(up);
    }
}


void change_digit_voltage()
{
    gpio_put(PIN_VOLTAGE_BUTTON, 1);
    sleep_us(BUTTON_DELAY_US);
    gpio_put(PIN_VOLTAGE_BUTTON, 0);
    // sleep_us(BUTTON_DELAY_US);
}



