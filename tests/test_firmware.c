#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include "decoder.h"
#include "spi_capture.h"
#include "controller.h"
#include "scpi_control.h"

static display_snapshot_t simulated;
static uint32_t simulated_us;
static unsigned simulated_factor;
static digit_t simulated_digit;
static bool simulated_editing;
static bool simulated_connected;
static bool simulated_output;
static bool missed_pulse;
static unsigned encoder_state;
static unsigned gpio_writes;
static const char *serial_input;

int getchar_timeout_us(uint32_t timeout)
{
    (void)timeout;
    return serial_input && *serial_input ? (unsigned char)*serial_input++ : -1;
}

void stdio_flush(void) { fflush(stdout); }

void spi_capture_snapshot(display_snapshot_t *snapshot)
{
    *snapshot = simulated;
}

uint32_t time_us_32(void) { return simulated_us; }
void gpio_init(unsigned pin) { (void)pin; }
void gpio_set_dir(unsigned pin, bool output) { (void)pin; (void)output; }

void sleep_us(uint64_t duration)
{
    uint32_t previous_us = simulated_us;
    simulated_us += duration;
    if (!simulated_connected || previous_us / 10000u == simulated_us / 10000u)
        return;
    simulated.sequence++;
    simulated.updated_us = simulated_us;
    simulated.screen.output_on = simulated_output;
    bool blank = simulated_editing && (simulated_us / 250000u) % 2;
    for (row_t row = VOLTAGE; row < ROW_LEN; row++)
    {
        if (row == VOLTAGE && blank)
            continue;
        simulated.row_valid[row] = true;
        simulated.row_updated_us[row] = simulated_us;
        simulated.row_sequence[row] = simulated.sequence;
    }
    if (!blank)
        simulated.screen.rows[VOLTAGE] = (value_t){simulated_factor, -2};
    if (blank)
    {
        simulated.editing_digit[VOLTAGE] = simulated_digit;
        simulated.editing_sequence[VOLTAGE] = simulated.sequence;
    }
}

void sleep_ms(uint32_t duration) { sleep_us(duration * 1000u); }

bool spi_capture_wait(uint32_t after, uint32_t timeout, display_snapshot_t *snapshot)
{
    sleep_us(timeout);
    spi_capture_snapshot(snapshot);
    return snapshot->sequence != after;
}

void gpio_put(unsigned pin, bool value)
{
    gpio_writes++;
    if (pin == PIN_VOLTAGE_BUTTON && value)
        simulated_digit = (simulated_digit + DIGIT_LEN - 1) % DIGIT_LEN;
    if (pin == PIN_OUTPUT_BUTTON && value)
        simulated_output = !simulated_output;
    if (pin == PIN_VOLTAGE_A || pin == PIN_VOLTAGE_B)
    {
        unsigned bit = pin == PIN_VOLTAGE_A ? 1u : 2u;
        unsigned previous = encoder_state;
        encoder_state = value ? encoder_state | bit : encoder_state & ~bit;
        if (encoder_state == 0 && previous != 0)
        {
            if (!simulated_editing)
            {
                simulated_editing = true;
                simulated_digit = DIGIT_2;
            }
            else if (!missed_pulse)
            {
                const int divisors[] = {1000, 100, 10, 1};
                int next = (int)simulated_factor +
                           (previous == 2 ? divisors[simulated_digit] : -divisors[simulated_digit]);
                simulated_factor = next < 0 ? 0 : next > 9999 ? 9999 : (unsigned)next;
            }
        }
    }
}

static void reset_supply(unsigned factor)
{
    memset(&simulated, 0, sizeof(simulated));
    simulated_factor = factor;
    simulated_digit = DIGIT_2;
    simulated_editing = false;
    simulated_connected = true;
    simulated_output = false;
    missed_pulse = false;
    simulated_us = 0;
    encoder_state = 0;
    gpio_writes = 0;
    sleep_ms(10);
}

static void test_controller(void)
{
    reset_supply(950);
    assert(controller_set_voltage(6.50));
    assert(simulated_factor == 650);
    assert(controller_set_voltage(9.50));
    assert(simulated_factor == 950);
    unsigned writes = gpio_writes;
    assert(controller_set_voltage(9.50));
    assert(gpio_writes == writes);
    assert(controller_set_voltage(10.60));
    assert(simulated_factor == 1060);
    assert(controller_set_voltage(0));
    assert(simulated_factor == 0);
    assert(controller_set_voltage(99.99));
    assert(simulated_factor == 9999);
    assert(!controller_set_voltage(NAN));
    assert(!controller_set_voltage(INFINITY));
    writes = gpio_writes;
    assert(!controller_set_voltage(-1));
    assert(!controller_set_voltage(100));
    assert(gpio_writes == writes);
    assert(controller_set_output(true));
    assert(controller_get_output());
    assert(!controller_set_voltage(1));
    assert(controller_set_output(false));
    reset_supply(950);
    missed_pulse = true;
    assert(!controller_set_voltage(6.50));
    assert(simulated_us < 10000000);
    reset_supply(950);
    simulated_connected = false;
    assert(!controller_set_voltage(6.50));
    assert(simulated_us < 3000000);
    assert(gpio_writes == 0);
}

static void command(const char *input, char *response, size_t capacity)
{
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    assert(saved_stdout >= 0);
    FILE *capture = tmpfile();
    assert(capture);
    assert(dup2(fileno(capture), STDOUT_FILENO) >= 0);
    serial_input = input;
    scpi_control_process();
    fflush(stdout);
    rewind(capture);
    size_t length = fread(response, 1, capacity - 1, capture);
    response[length] = 0;
    assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);
    fclose(capture);
}

static void test_scpi(void)
{
    char response[512];
    reset_supply(950);
    simulated.row_valid[CURRENT] = true;
    simulated.row_updated_us[CURRENT] = simulated_us;
    simulated.screen.rows[CURRENT] = (value_t){1234, -3};
    scpi_control_init();
    command("*IDN?\n", response, sizeof(response));
    assert(strstr(response, "Pico,Powersupply,"));
    command("VOLT 6.50\n*OPC?\n", response, sizeof(response));
    assert(strcmp(response, "1\r\n") == 0);
    assert(simulated_factor == 650);
    command("VOLT?\n", response, sizeof(response));
    assert(fabs(strtod(response, NULL) - 6.5) < 0.00001);
    command("CURR?\n", response, sizeof(response));
    assert(fabs(strtod(response, NULL) - 1.234) < 0.00001);
    command("SOUR:VOLT 9.50\nSOUR:VOLT?\n", response, sizeof(response));
    assert(fabs(strtod(response, NULL) - 9.5) < 0.00001);
    command("SYST:DISP:TIM?\n", response, sizeof(response));
    assert(strcmp(response, "0,0,0,0,0,0,0,0,0,0,0,0,0\r\n") == 0);
    command("OUTP ON\nOUTP?\n", response, sizeof(response));
    if (strcmp(response, "1\r\n") != 0)
        fprintf(stderr, "Unexpected output response: '%s'\n", response);
    assert(strcmp(response, "1\r\n") == 0);
    command("VOLT 1\n", response, sizeof(response));
    assert(response[0] == 0);
    command("SYST:ERR?\n", response, sizeof(response));
    assert(strstr(response, "-200,"));
    command("*CLS\nBOGUS\n", response, sizeof(response));
    assert(response[0] == 0);
    command("SYST:ERR?\n", response, sizeof(response));
    assert(strstr(response, "-113,"));
    simulated_connected = false;
    sleep_ms(2100);
    command("*CLS\nVOLT?\n", response, sizeof(response));
    assert(response[0] == 0);
    command("SYST:ERR?\n", response, sizeof(response));
    assert(strstr(response, "-200,"));
    command("SYST:ERR?\n", response, sizeof(response));
    assert(strstr(response, "0,\"No error\""));
}

static void encode_row(uint8_t *data, row_t row, unsigned factor, int decimal)
{
    for (int digit = DIGIT_3; digit >= DIGIT_0; digit--)
    {
        uint8_t segments = segment_mapping[factor % 10];
        factor /= 10;
        if (digit == decimal)
        {
            segments |= SEG_DP;
        }
        for (segment_t segment = SEGMENT_A; segment < SEGMENT_LEN; segment++)
        {
            bit_position_t position = data_mapping[row][digit][segment];
            data[position.byte] &= ~(1u << position.bit);
            if (segments & (1u << segment))
            {
                data[position.byte] |= 1u << position.bit;
            }
        }
    }
}

static uint32_t replay(capture_engine_t *engine, uint8_t *data,
                       unsigned length, uint32_t started)
{
    uint32_t levels = 1u << PIN_CS;
    capture_engine_sample(engine, levels, started - 10);
    levels = 0;
    capture_engine_sample(engine, levels, started);
    uint32_t now = started;
    for (unsigned index = 0; index < length; index++)
    {
        for (unsigned bit = 0; bit < 8; bit++)
        {
            levels = (data[index] & (1u << bit)) ? 1u << PIN_DIO : 0;
            capture_engine_sample(engine, levels, now += 5);
            capture_engine_sample(engine, levels | (1u << PIN_CLK), now += 5);
            capture_engine_sample(engine, levels, now += 5);
        }
    }
    assert(capture_engine_sample(engine, levels | (1u << PIN_CS), now += 5));
    return now;
}

static void test_capture(void)
{
    capture_engine_t engine;
    capture_engine_init(&engine);
    uint8_t data[300] = {0xC0};
    encode_row(data + 1, VOLTAGE, 950, 1);
    encode_row(data + 1, CURRENT, 1234, 0);
    encode_row(data + 1, POWER, 1172, 1);
    replay(&engine, data, 17, 100);
    assert(engine.display.sequence == 1);
    assert(engine.display.screen.rows[VOLTAGE].fac == 950);
    assert(engine.display.screen.rows[CURRENT].exp == -3);
    assert(engine.display.timing.clock_high_min_us == 5);
    assert(engine.display.timing.clock_low_min_us == 10);
    assert(engine.display.timing.cs_min_us == 2045);
    replay(&engine, data, 17, 10100);
    assert(engine.display.timing.refresh_min_us == 10000);
    replay(&engine, data, 300, 20100);
    assert(engine.display.timing.rejected == 1);
    assert(engine.display.sequence == 2);
    replay(&engine, data, 16, 60100);
    assert(engine.display.timing.rejected == 2);
    data[0] = 0x80;
    replay(&engine, data, 17, 70100);
    assert(engine.display.timing.rejected == 3);
    capture_engine_sample(&engine, 0, 80100);
    assert(capture_engine_sample(&engine, 0, 80100 + CAPTURE_TIMEOUT_US));
    assert(engine.display.timing.rejected == 4);
    assert(!capture_engine_sample(&engine, 0, 80101 + CAPTURE_TIMEOUT_US));
    capture_engine_init(&engine);
    data[0] = 0xC0;
    replay(&engine, data, 17, UINT32_MAX - 1000);
    assert(engine.display.sequence == 1);
    assert(engine.display.timing.cs_min_us == 2045);
    replay(&engine, data, 17, 10000);
    uint32_t valid_sequence = engine.display.row_sequence[VOLTAGE];
    for (segment_t segment = SEGMENT_A; segment < SEGMENT_DP; segment++)
    {
        bit_position_t position = data_mapping[VOLTAGE][DIGIT_2][segment];
        data[1 + position.byte] &= ~(1u << position.bit);
    }
    replay(&engine, data, 17, 20000);
    assert(engine.display.screen.rows[VOLTAGE].fac == 950);
    assert(engine.display.row_sequence[VOLTAGE] == valid_sequence);
    assert(engine.display.editing_digit[VOLTAGE] == DIGIT_2);
    encode_row(data + 1, VOLTAGE, 950, 1);
    replay(&engine, data, 17, 30000);
    for (segment_t segment = SEGMENT_A; segment < SEGMENT_DP; segment++)
    {
        bit_position_t position = data_mapping[VOLTAGE][DIGIT_2][segment];
        data[1 + position.byte] &= ~(1u << position.bit);
    }
    replay(&engine, data, 17, 520000);
    assert(engine.display.timing.blink_period_us[VOLTAGE] == 500000);
}

int main(void)
{
    uint8_t data[16] = {0};
    value_t value = {0};
    for (row_t row = VOLTAGE; row < ROW_LEN; row++)
    {
        for (int decimal = -1; decimal < DIGIT_LEN; decimal++)
        {
            encode_row(data, row, 1234, decimal);
            assert(get_value(data, &value, row));
            assert(value.fac == 1234);
            assert(value.exp == (decimal < 0 ? 0 : decimal - 3));
        }
    }
    encode_row(data, VOLTAGE, 950, 1);
    value_t previous = {950, -2};
    value = previous;
    for (segment_t segment = SEGMENT_A; segment < SEGMENT_DP; segment++)
    {
        bit_position_t position = data_mapping[VOLTAGE][DIGIT_2][segment];
        data[position.byte] &= ~(1u << position.bit);
    }
    assert(!get_value(data, &value, VOLTAGE));
    assert(value.fac == previous.fac && value.exp == previous.exp);
    digit_t digit;
    assert(decoder_editing_digit(data, VOLTAGE, &digit));
    assert(digit == DIGIT_2);
    bit_position_t position = data_mapping[VOLTAGE][DIGIT_2][SEGMENT_A];
    data[position.byte] |= 1u << position.bit;
    assert(!decoder_editing_digit(data, VOLTAGE, &digit));
    encode_row(data, VOLTAGE, 950, 1);
    position = data_mapping[VOLTAGE][DIGIT_2][SEGMENT_DP];
    data[position.byte] |= 1u << position.bit;
    assert(!get_value(data, &value, VOLTAGE));
    test_capture();
    test_controller();
    test_scpi();
    puts("Decoder, capture replay, controller, and SCPI tests passed");
    return 0;
}