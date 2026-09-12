
#include "controller.h"
#include "pico/stdlib.h"
#include <math.h>

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
    gpio_init(PIN_OUTPUT_BUTTON);
    gpio_set_dir(PIN_OUTPUT_BUTTON, true);
    gpio_put(PIN_OUTPUT_BUTTON, 0);
    
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
    // screendata.editing_digit = (screendata.editing_digit-1+DIGIT_LEN) % DIGIT_LEN;
    gpio_put(PIN_VOLTAGE_BUTTON, 1);
    sleep_us(BUTTON_PRESS_DELAY_US);
    gpio_put(PIN_VOLTAGE_BUTTON, 0);
    sleep_us(BUTTON_RELEASE_DELAY_US);
}

void move_digit_voltage(digit_t digit)
{
    // rotate once to get the editing digit to blink
    // only if the last 0xFF(empty) has been more than about 2000ms 
    // maybe not necessary when last read was under 2000ms ago as then its still blinking
    // and if longer then 2000ms then it resets to 100mV (DIGIT_2) 
    rotate_digit_voltage_once_hard(false);
    wait_for_spi_capture();

    uint8_t start = (uint8_t)screendata.editing_digit;
    uint8_t goal = (uint8_t)digit;
    // moves = (start - goal + N) % N
    uint8_t moves = (start - goal + DIGIT_LEN) % DIGIT_LEN;
    for (int i = 0; i < moves; i++)
    {
        change_digit_voltage();
    }
}

bool controller_set_voltage(double voltage)
{
    if (voltage < 0.0 || voltage > 9999.0)
    {
        return false;
    }

    uint16_t target = (uint16_t)lround(voltage);
    uint16_t current = screendata.rows[VOLTAGE].fac;

    bool up = target > current;

    // The first pulse makes the active digit visible. Then select DIGIT_3.
    rotate_digit_voltage_once_hard(false);
    wait_for_spi_capture();
    while (screendata.editing_digit != DIGIT_3)
    {
        change_digit_voltage();
        wait_for_spi_capture();
    }

    // Work from the least significant digit so each carry is intentional.
    for (int digit = DIGIT_3; digit >= DIGIT_0; digit--)
    {
        uint16_t divisor = (uint16_t)pow(10.0, (double)(DIGIT_3 - digit));
        current = screendata.rows[VOLTAGE].fac;
        uint8_t current_digit = (uint8_t)((current / divisor) % 10);
        uint8_t target_digit = (uint8_t)((target / divisor) % 10);
        uint8_t steps;

        if (up)
        {
            steps = (uint8_t)((target_digit - current_digit + 10) % 10);
        }
        else
        {
            steps = (uint8_t)((current_digit - target_digit + 10) % 10);
        }

        if (steps != 0)
        {
            rotate_digit_voltage(up, steps);
            sleep_ms(20);
        }

        if (digit != DIGIT_0)
        {
            change_digit_voltage();
            wait_for_spi_capture();
        }
    }

    return true;
}

void controller_set_output(bool enabled)
{
    if (enabled == screendata.output_on)
    {
        return;
    }

    gpio_put(PIN_OUTPUT_BUTTON, 1);
    sleep_us(BUTTON_PRESS_DELAY_US);
    gpio_put(PIN_OUTPUT_BUTTON, 0);
    sleep_us(BUTTON_RELEASE_DELAY_US);
}

void controller_toggle_output(void)
{
    controller_set_output(!screendata.output_on);
}

bool controller_get_output(void)
{
    return screendata.output_on;
}


