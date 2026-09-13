
#include "controller.h"
#include "pico/stdlib.h"
#include <math.h>
#include <stdlib.h>

void controller_init()
{
    for (unsigned index = 0; index < CALIBRATION_PIN_COUNT; index++)
    {
        unsigned pin = calibration_candidate_pins[index];
        gpio_init(pin);
        gpio_put(pin, 0);
        gpio_set_dir(pin, true);
    }
}

void controller_raw_rotate(unsigned pin_a, unsigned pin_b, bool up,
                           uint32_t phase_us, uint32_t gap_us)
{
    unsigned first = up ? pin_a : pin_b;
    unsigned second = up ? pin_b : pin_a;
    gpio_put(first, 1);
    sleep_us(phase_us);
    gpio_put(second, 1);
    sleep_us(phase_us);
    gpio_put(first, 0);
    sleep_us(phase_us);
    gpio_put(second, 0);
    sleep_us(phase_us);
    sleep_us(gap_us);
}

void controller_rotate(row_t row, bool up)
{
    controller_raw_rotate(calibration_get(row == VOLTAGE ? CAL_V_A : CAL_I_A),
                          calibration_get(row == VOLTAGE ? CAL_V_B : CAL_I_B), up,
                          calibration_get(row == VOLTAGE ? CAL_V_PHASE_US : CAL_I_PHASE_US),
                          calibration_get(row == VOLTAGE ? CAL_V_GAP_US : CAL_I_GAP_US));
}

void rotate_digit_voltage_once_hard(bool up) { controller_rotate(VOLTAGE, up); }

void rotate_digit_voltage(bool up, uint16_t n)
{
    for ( int i = 0; i < n; i++ )
    {
        rotate_digit_voltage_once_hard(up);
    }
}


void change_digit_voltage()
{
    controller_press(VOLTAGE);
}

void controller_raw_press(unsigned pin, uint32_t press_us, uint32_t release_us)
{
    gpio_put(pin, 1);
    sleep_us(press_us);
    gpio_put(pin, 0);
    sleep_us(release_us);
}

void controller_press(row_t row)
{
    controller_raw_press(calibration_get(row == VOLTAGE ? CAL_V_BUTTON : CAL_I_BUTTON),
                         calibration_get(row == VOLTAGE ? CAL_V_PRESS_US : CAL_I_PRESS_US),
                         calibration_get(row == VOLTAGE ? CAL_V_RELEASE_US : CAL_I_RELEASE_US));
}

static bool wait_row(row_t row, uint32_t after, bool editing, digit_t expected_digit,
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
              if ((int32_t)(snapshot->editing_sequence[row] - after) > 0 &&
                (expected_digit == UNKNOWN_DIGIT ||
                  snapshot->editing_digit[row] == expected_digit))
                return true;
        }
        else if (snapshot->row_valid[row] &&
             (int32_t)(snapshot->row_sequence[row] - after) > 0 &&
             (expected_factor < 0 || snapshot->screen.rows[row].fac == expected_factor))
            return true;
        sleep_ms(1);
    } while (time_us_32() - started < calibration_get(CAL_FEEDBACK_US));
    return false;
}

static bool select_digit(row_t row, digit_t digit, display_snapshot_t *snapshot)
{
    if (!wait_row(row, snapshot->sequence, true, UNKNOWN_DIGIT, -1, snapshot))
        return false;
    for (unsigned moves = 0; moves < DIGIT_LEN; moves++)
    {
        digit_t current = snapshot->editing_digit[row];
        if (current == digit)
            return true;
        controller_press(row);
        spi_capture_snapshot(snapshot);
        digit_t expected = (digit_t)((current + DIGIT_LEN - 1) % DIGIT_LEN);
        if (!wait_row(row, snapshot->sequence, true, expected, -1, snapshot))
            return false;
    }
    return false;
}

bool controller_prepare_edit(row_t row, digit_t digit)
{
    if (row > CURRENT || digit >= DIGIT_LEN)
        return false;
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!wait_row(row, snapshot.sequence, false, UNKNOWN_DIGIT, -1, &snapshot))
        return false;
    controller_press(row);
    spi_capture_snapshot(&snapshot);
    return select_digit(row, digit, &snapshot);
}

void move_digit_voltage(digit_t digit) { (void)controller_prepare_edit(VOLTAGE, digit); }

static bool set_value(row_t row, double requested)
{
    if (!isfinite(requested) || requested < 0)
        return false;
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!wait_row(row, snapshot.sequence, false, UNKNOWN_DIGIT, -1, &snapshot))
        return false;
    controller_press(row);
    spi_capture_snapshot(&snapshot);
    if (!wait_row(row, snapshot.sequence, true, UNKNOWN_DIGIT, -1, &snapshot) ||
        !wait_row(row, snapshot.sequence, false, UNKNOWN_DIGIT, -1, &snapshot))
        return false;
    int8_t exponent = snapshot.screen.rows[row].exp;
    double factor = requested / pow(10.0, exponent);
    if (factor > 9999.0)
        return false;
    uint16_t target = (uint16_t)lround(factor);
    if (target == snapshot.screen.rows[row].fac)
        return true;
    const unsigned divisors[DIGIT_LEN] = {1000, 100, 10, 1};
    uint32_t started = time_us_32();
    for (digit_t digit = DIGIT_0; digit < DIGIT_LEN; digit++)
    {
        if (!select_digit(row, digit, &snapshot) ||
            !wait_row(row, snapshot.sequence, false, UNKNOWN_DIGIT, -1, &snapshot))
            return false;
        unsigned divisor = divisors[digit];
        int difference = (int)((target / divisor) % 10) -
                         (int)((snapshot.screen.rows[row].fac / divisor) % 10);
        for (int steps = 0; steps < abs(difference); steps++)
        {
            if (snapshot.screen.rows[row].exp != exponent ||
                time_us_32() - started > 90000000u)
                return false;
            int expected = snapshot.screen.rows[row].fac +
                           (difference > 0 ? (int)divisor : -(int)divisor);
            controller_rotate(row, difference > 0);
            spi_capture_snapshot(&snapshot);
            if (!wait_row(row, snapshot.sequence, false, UNKNOWN_DIGIT, expected, &snapshot))
                return false;
        }
    }

    return snapshot.screen.rows[row].exp == exponent &&
           snapshot.screen.rows[row].fac == target;
}

bool controller_set_voltage(double voltage) { return set_value(VOLTAGE, voltage); }
bool controller_set_current(double current) { return set_value(CURRENT, current); }

bool controller_read_target(row_t row, value_t *value)
{
    if (!controller_prepare_edit(row, DIGIT_3))
        return false;
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!wait_row(row, snapshot.sequence, false, UNKNOWN_DIGIT, -1, &snapshot))
        return false;
    *value = snapshot.screen.rows[row];
    return true;
}

bool controller_read_actual(row_t row, value_t *value)
{
    if (row > CURRENT)
        return false;
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    uint32_t started = time_us_32();
    uint32_t editing_sequence = snapshot.editing_sequence[row];
    row_t other = row == VOLTAGE ? CURRENT : VOLTAGE;
    uint32_t other_sequence = snapshot.editing_sequence[other];
    uint32_t coupling = calibration_get(row == VOLTAGE ? CAL_I_TO_V_COUPLING : CAL_V_TO_I_COUPLING);
    bool shared_exit = coupling == 0 || coupling == 2 || coupling == 4;
    uint32_t quiet_us = calibration_get(row == VOLTAGE ? CAL_V_BLINK_US : CAL_I_BLINK_US) +
                        calibration_get(CAL_REFRESH_US) * 2;
    do
    {
        if (!snapshot.screen.output_on || !snapshot.sequence ||
            time_us_32() - snapshot.updated_us >= DISPLAY_STALE_US ||
            snapshot.editing_sequence[row] != editing_sequence ||
            (shared_exit && snapshot.editing_sequence[other] != other_sequence))
            return false;
        sleep_ms(1);
        spi_capture_snapshot(&snapshot);
    } while (time_us_32() - started < quiet_us);
    uint32_t now = time_us_32();
    uint32_t exit_us = calibration_get(row == VOLTAGE ? CAL_V_EXIT_US : CAL_I_EXIT_US);
    uint32_t since_edit = now - snapshot.editing_updated_us[row];
    uint32_t margin = calibration_get(CAL_REFRESH_US);
    if (!snapshot.screen.output_on || !snapshot.row_valid[row] ||
        now - snapshot.row_updated_us[row] >= DISPLAY_STALE_US ||
        since_edit <= exit_us + margin ||
        now - snapshot.row_updated_us[row] >= since_edit - exit_us - margin)
        return false;
    uint32_t other_exit = calibration_get(other == VOLTAGE ? CAL_V_EXIT_US : CAL_I_EXIT_US);
    if (shared_exit && now - snapshot.editing_updated_us[other] <= other_exit + margin)
        return false;
    *value = snapshot.screen.rows[row];
    return true;
}

bool controller_set_output(bool enabled)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!snapshot.sequence || time_us_32() - snapshot.updated_us >= DISPLAY_STALE_US)
        return false;
    if (enabled == snapshot.screen.output_on)
        return true;
    controller_raw_press(calibration_get(CAL_OUTPUT_BUTTON),
                         calibration_get(CAL_OUTPUT_PRESS_US),
                         calibration_get(CAL_OUTPUT_RELEASE_US));
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


