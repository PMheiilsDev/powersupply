
#include "controller.h"
#include "pico/stdlib.h"
#include <math.h>
#include <stdlib.h>

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
    gpio_put(PIN_VOLTAGE_BUTTON, 1);
    sleep_us(BUTTON_PRESS_DELAY_US);
    gpio_put(PIN_VOLTAGE_BUTTON, 0);
    sleep_us(BUTTON_RELEASE_DELAY_US);
}

static bool wait_voltage(uint32_t after, bool editing, digit_t expected_digit,
                         int expected_factor, display_snapshot_t *snapshot)
{
    uint32_t started = time_us_32();
    do
    {
        spi_capture_snapshot(snapshot);
        if (snapshot->screen.output_on)
            return false;
        if (editing)
        {
            if ((int32_t)(snapshot->editing_sequence[VOLTAGE] - after) > 0 &&
                (expected_digit == UNKNOWN_DIGIT ||
                 snapshot->editing_digit[VOLTAGE] == expected_digit))
                return true;
        }
        else if (snapshot->row_valid[VOLTAGE] &&
                 (int32_t)(snapshot->row_sequence[VOLTAGE] - after) > 0 &&
                 (expected_factor < 0 || snapshot->screen.rows[VOLTAGE].fac == expected_factor))
            return true;
        sleep_ms(1);
    } while (time_us_32() - started < DISPLAY_STALE_US);
    return false;
}

static bool select_voltage_digit(digit_t digit, display_snapshot_t *snapshot)
{
    if (!wait_voltage(snapshot->sequence, true, UNKNOWN_DIGIT, -1, snapshot))
        return false;
    for (unsigned moves = 0; moves < DIGIT_LEN; moves++)
    {
        digit_t current = snapshot->editing_digit[VOLTAGE];
        if (current == digit)
            return true;
        change_digit_voltage();
        spi_capture_snapshot(snapshot);
        digit_t expected = (digit_t)((current + DIGIT_LEN - 1) % DIGIT_LEN);
        if (!wait_voltage(snapshot->sequence, true, expected, -1, snapshot))
            return false;
    }
    return false;
}

void move_digit_voltage(digit_t digit)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (digit >= DIGIT_LEN ||
        !wait_voltage(snapshot.sequence, false, UNKNOWN_DIGIT, -1, &snapshot))
        return;
    rotate_digit_voltage_once_hard(false);
    spi_capture_snapshot(&snapshot);
    (void)select_voltage_digit(digit, &snapshot);
}

bool controller_set_voltage(double voltage)
{
    if (!isfinite(voltage) || voltage < 0)
        return false;
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!wait_voltage(snapshot.sequence, false, UNKNOWN_DIGIT, -1, &snapshot))
        return false;
    int8_t exponent = snapshot.screen.rows[VOLTAGE].exp;
    double factor = voltage / pow(10.0, exponent);
    if (factor > 9999.0)
        return false;
    uint16_t target = (uint16_t)lround(factor);
    if (target == snapshot.screen.rows[VOLTAGE].fac)
        return true;
    rotate_digit_voltage_once_hard(false);

    spi_capture_snapshot(&snapshot);
    const unsigned divisors[DIGIT_LEN] = {1000, 100, 10, 1};
    uint32_t started = time_us_32();
    for (digit_t digit = DIGIT_0; digit < DIGIT_LEN; digit++)
    {
        if (!select_voltage_digit(digit, &snapshot) ||
            !wait_voltage(snapshot.sequence, false, UNKNOWN_DIGIT, -1, &snapshot))
            return false;
        unsigned divisor = divisors[digit];
        int difference = (int)((target / divisor) % 10) -
                         (int)((snapshot.screen.rows[VOLTAGE].fac / divisor) % 10);
        for (int steps = 0; steps < abs(difference); steps++)
        {
            if (snapshot.screen.rows[VOLTAGE].exp != exponent ||
                time_us_32() - started > 30000000u)
                return false;
            int expected = snapshot.screen.rows[VOLTAGE].fac +
                           (difference > 0 ? (int)divisor : -(int)divisor);
            rotate_digit_voltage_once_hard(difference > 0);
            spi_capture_snapshot(&snapshot);
            if (!wait_voltage(snapshot.sequence, false, UNKNOWN_DIGIT, expected, &snapshot))
                return false;
        }
    }

    return snapshot.screen.rows[VOLTAGE].exp == exponent &&
           snapshot.screen.rows[VOLTAGE].fac == target;
}

bool controller_set_output(bool enabled)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!snapshot.sequence || time_us_32() - snapshot.updated_us >= DISPLAY_STALE_US)
        return false;
    if (enabled == snapshot.screen.output_on)
        return true;
    gpio_put(PIN_OUTPUT_BUTTON, 1);
    sleep_us(BUTTON_PRESS_DELAY_US);
    gpio_put(PIN_OUTPUT_BUTTON, 0);
    sleep_us(BUTTON_RELEASE_DELAY_US);
    spi_capture_snapshot(&snapshot);
    uint32_t started = time_us_32();
    do
    {
        if (!spi_capture_wait(snapshot.sequence, 100000u, &snapshot))
            continue;
        if (snapshot.screen.output_on == enabled)
            return true;
    } while (time_us_32() - started < DISPLAY_STALE_US);
    return false;
}

bool controller_toggle_output(void)
{
    return controller_set_output(!controller_get_output());
}

bool controller_get_output(void)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    return snapshot.screen.output_on;
}


