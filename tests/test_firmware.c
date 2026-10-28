#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include "decoder.h"
#include "spi_capture.h"
#include "spi_trace.h"
#include "diagnostics.h"
#include "controller.h"
#include "scpi_control.h"
#include "calibration.h"
#include "calibration_job.h"

static int threshold_trial(uint32_t value, void *context)
{
    return value >= *(uint32_t *)context;
}

static void test_search(void)
{
    uint32_t threshold = 2050000, low = 500000, high = 5000000;
    assert(calibration_bisect(&low, &high, 10000, true, threshold_trial, &threshold));
    assert(low < threshold && high >= threshold && high - low <= 10000);
    low = 3000000;
    high = 5000000;
    assert(!calibration_bisect(&low, &high, 10000, true, threshold_trial, &threshold));
}

static calibration_record_t stored_records[2];
static bool storage_failure;

bool calibration_storage_read(unsigned slot, calibration_record_t *record)
{
    *record = stored_records[slot];
    return true;
}

bool calibration_storage_write(unsigned slot, const calibration_record_t *record)
{
    if (storage_failure)
        return false;
    stored_records[slot] = *record;
    return true;
}

static void test_configuration(void)
{
    calibration_config_t config;
    calibration_defaults(&config);
    assert(calibration_valid(&config));
    config.values[CAL_V_A] = 21;
    assert(!calibration_valid(&config));
    calibration_defaults(&config);
    config.values[CAL_V_EXIT_US] = config.values[CAL_V_IDLE_US];
    assert(!calibration_valid(&config));
    calibration_init();
    assert(!calibration_loaded());
    assert(calibration_set(CAL_V_A, 20));
    assert(calibration_get(CAL_V_B) == 19);
    assert(!calibration_set(CAL_V_A, 28));
    assert(calibration_save());
    assert(calibration_loaded() && !calibration_dirty());
    assert(calibration_set(CAL_V_IDLE_US, 2100000));
    assert(calibration_dirty());
    storage_failure = true;
    assert(!calibration_save());
    storage_failure = false;
    assert(calibration_load());
    assert(calibration_get(CAL_V_IDLE_US) == 2000000);
    stored_records[0].crc ^= 1;
    assert(!calibration_load());
    calibration_defaults(&config);
    calibration_record_encode(&stored_records[0], &config, UINT32_MAX);
    calibration_record_encode(&stored_records[1], &config, 0);
    assert(calibration_record_select(stored_records) == 1);
    stored_records[1].version++;
    assert(calibration_record_select(stored_records) == 0);
    assert(calibration_find_field("v_idle_us", 9) == CAL_V_IDLE_US);
    calibration_defaults(&config);
    assert(calibration_apply(&config));
}

static display_snapshot_t simulated;
static uint32_t simulated_us;
static unsigned simulated_values[2];
static digit_t simulated_digits[2];
static bool simulated_edits[2];
static uint32_t last_action_us[2];
static bool was_blank[2];
static uint32_t last_blank_us[2];
static bool blank_seen[2];
static unsigned physical_pins[7];
static unsigned encoder_states[2];
static unsigned encoder_edges[2];
static bool encoder_good[2];
static uint32_t encoder_edge_us[2];
static uint32_t encoder_finished_us[2];
static bool pin_levels[30];
static uint32_t pressed_us[30];
static uint32_t released_us[30];
static unsigned coupling_flags;
#define simulated_factor simulated_values[VOLTAGE]
#define simulated_digit simulated_digits[VOLTAGE]
#define simulated_editing simulated_edits[VOLTAGE]
static bool simulated_connected;
static bool simulated_output;
static bool missed_pulse;
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

void spi_capture_reset_timing(void)
{
    memset(&simulated.timing, 0, sizeof(simulated.timing));
}

void spi_capture_trace_read(uint32_t after, spi_trace_batch_t *batch) { spi_trace_read(after, batch); }
void spi_capture_trace_clear(void) { spi_trace_clear(); }
void spi_capture_trace_marker(uint8_t kind, uint32_t action_id, uint32_t started_us,
                              const uint8_t *data, uint16_t length)
{
    spi_trace_record(kind, 0, started_us, simulated_us, action_id, data, length, &simulated);
}

uint32_t time_us_32(void) { return simulated_us; }
void gpio_init(unsigned pin) { (void)pin; }
void gpio_set_dir(unsigned pin, bool output) { (void)pin; (void)output; }

void sleep_us(uint64_t duration)
{
    uint32_t previous_us = simulated_us;
    simulated_us += duration;
    if (!simulated_connected || previous_us / 50000u == simulated_us / 50000u)
        return;
    simulated.sequence++;
    simulated.updated_us = simulated_us;
    simulated.timing.scan_frames++;
    simulated.timing.scan_min_us = simulated.timing.scan_max_us = 10000;
    simulated.timing.display_min_us = simulated.timing.display_max_us = 50000;
    simulated.screen.output_on = simulated_output;
    for (row_t row = VOLTAGE; row < ROW_LEN; row++)
    {
        uint32_t elapsed = simulated_us - last_action_us[row];
        if (elapsed >= 9000000)
            simulated_edits[row] = false;
        bool blank = simulated_edits[row] && elapsed < 2000000 && (elapsed / 500000u) % 2;
        if (!blank)
        {
            simulated.row_valid[row] = true;
            simulated.row_updated_us[row] = simulated_us;
            simulated.row_sequence[row] = simulated.sequence;
            simulated.screen.rows[row] = (value_t){simulated_values[row], row == VOLTAGE ? -2 : -3};
        }
        else
        {
            simulated.editing_digit[row] = simulated_digits[row];
            simulated.editing_sequence[row] = simulated.sequence;
            simulated.editing_updated_us[row] = simulated_us;
            if (!was_blank[row])
            {
                if (blank_seen[row])
                    simulated.timing.blink_period_us[row] = simulated_us - last_blank_us[row];
                blank_seen[row] = true;
                last_blank_us[row] = simulated_us;
            }
        }
        was_blank[row] = blank;
    }
}

void sleep_ms(uint32_t duration) { sleep_us(duration * 1000u); }

bool spi_capture_wait(uint32_t after, uint32_t timeout, display_snapshot_t *snapshot)
{
    sleep_us(timeout);
    spi_capture_snapshot(snapshot);
    return snapshot->sequence != after;
}

static void panel_event(row_t row, bool button, bool up)
{
    uint32_t elapsed = simulated_us - last_action_us[row];
    if (!simulated_edits[row] || elapsed >= 9000000)
    {
        simulated_edits[row] = true;
        simulated_digits[row] = DIGIT_2;
    }
    else if (button)
        simulated_digits[row] = (simulated_digits[row] + DIGIT_LEN - 1) % DIGIT_LEN;
    else if (elapsed < 2000000 && !missed_pulse)
    {
        const int divisors[] = {1000, 100, 10, 1};
        int next = (int)simulated_values[row] + (up ? 1 : -1) * divisors[simulated_digits[row]];
        simulated_values[row] = next < 0 ? 0 : next > 9999 ? 9999 : (unsigned)next;
    }
    last_action_us[row] = simulated_us;
    row_t other = row == VOLTAGE ? CURRENT : VOLTAGE;
    if ((coupling_flags & (1u << row)) && simulated_edits[other])
        last_action_us[other] = simulated_us;
}

void gpio_put(unsigned pin, bool value)
{
    gpio_writes++;
    if (pin_levels[pin] == value)
        return;
    pin_levels[pin] = value;
    if (value)
        pressed_us[pin] = simulated_us;
    else
    {
        bool recognized = simulated_us - pressed_us[pin] >= 50000 &&
                          pressed_us[pin] - released_us[pin] >= 10000;
        released_us[pin] = simulated_us;
        if (recognized)
        {
            if (pin == physical_pins[CAL_OUTPUT_BUTTON])
                simulated_output = !simulated_output;
            for (row_t row = VOLTAGE; row <= CURRENT; row++)
                if (pin == physical_pins[row == VOLTAGE ? CAL_V_BUTTON : CAL_I_BUTTON])
                    panel_event(row, true, false);
        }
    }
    for (row_t row = VOLTAGE; row <= CURRENT; row++)
    {
        unsigned pin_a = physical_pins[row == VOLTAGE ? CAL_V_A : CAL_I_A];
        unsigned pin_b = physical_pins[row == VOLTAGE ? CAL_V_B : CAL_I_B];
        if (pin != pin_a && pin != pin_b)
            continue;
        unsigned previous = encoder_states[row];
        unsigned state = (pin_levels[pin_a] ? 1u : 0u) | (pin_levels[pin_b] ? 2u : 0u);
        if (previous == 0)
        {
            encoder_edges[row] = 0;
            encoder_good[row] = simulated_us - encoder_finished_us[row] >= 10000;
        }
        else if (simulated_us - encoder_edge_us[row] < 1000)
            encoder_good[row] = false;
        encoder_edges[row]++;
        encoder_edge_us[row] = simulated_us;
        encoder_states[row] = state;
        if (state == 0)
        {
            if (encoder_good[row] && encoder_edges[row] == 4)
            {
                panel_event(row, false, previous == 2);
                encoder_finished_us[row] = simulated_us;
            }
        }
    }
}

static void reset_supply(unsigned factor)
{
    memset(&simulated, 0, sizeof(simulated));
    memset(simulated_values, 0, sizeof(simulated_values));
    memset(simulated_digits, 0, sizeof(simulated_digits));
    memset(simulated_edits, 0, sizeof(simulated_edits));
    memset(last_action_us, 0, sizeof(last_action_us));
    memset(was_blank, 0, sizeof(was_blank));
    memset(last_blank_us, 0, sizeof(last_blank_us));
    memset(blank_seen, 0, sizeof(blank_seen));
    memset(encoder_states, 0, sizeof(encoder_states));
    memset(encoder_edges, 0, sizeof(encoder_edges));
    memset(encoder_good, 0, sizeof(encoder_good));
    memset(encoder_edge_us, 0, sizeof(encoder_edge_us));
    memset(encoder_finished_us, 0, sizeof(encoder_finished_us));
    memset(pin_levels, 0, sizeof(pin_levels));
    memset(pressed_us, 0, sizeof(pressed_us));
    memset(released_us, 0, sizeof(released_us));
    calibration_config_t config;
    calibration_defaults(&config);
    for (unsigned field = CAL_V_A; field <= CAL_OUTPUT_BUTTON; field++)
        physical_pins[field] = config.values[field];
    simulated_factor = factor;
    simulated_values[CURRENT] = 1234;
    simulated_digit = DIGIT_2;
    simulated_editing = false;
    simulated_connected = true;
    simulated_output = false;
    missed_pulse = false;
    simulated_us = 0;
    coupling_flags = 0;
    gpio_writes = 0;
    sleep_ms(50);
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
    assert(simulated_factor == 950);
    assert(gpio_writes > writes);
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
    assert(simulated_factor == 9999);
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

static void test_current_and_modes(void)
{
    reset_supply(950);
    assert(controller_set_current(0.750));
    assert(simulated_values[CURRENT] == 750);
    value_t value;
    assert(controller_read_target(CURRENT, &value));
    assert(value.fac == 750 && value.exp == -3);
    sleep_ms(2100);
    controller_rotate(CURRENT, true);
    assert(simulated_values[CURRENT] == 750);
    controller_rotate(CURRENT, true);
    assert(simulated_values[CURRENT] == 751);
    sleep_ms(9100);
    controller_press(CURRENT);
    sleep_ms(600);
    assert(simulated.editing_digit[CURRENT] == DIGIT_2);
    assert(controller_set_output(true));
    assert(!controller_read_actual(CURRENT, &value));
    assert(!controller_read_target(CURRENT, &value));
    sleep_ms(11000);
    assert(controller_read_actual(CURRENT, &value));
    assert(value.fac == 751 && value.exp == -3);
    controller_press(VOLTAGE);
    assert(!controller_read_actual(VOLTAGE, &value));
    assert(controller_set_output(false));
}

static void abort_during_output(void)
{
    if (simulated_output)
        calibration_job_abort();
}

static void test_full_calibration(void)
{
    calibration_config_t config;
    calibration_defaults(&config);
    assert(calibration_apply(&config));
    reset_supply(950);
    physical_pins[CAL_V_A] = 26;
    physical_pins[CAL_V_B] = 27;
    physical_pins[CAL_I_A] = 20;
    physical_pins[CAL_I_B] = 19;
    physical_pins[CAL_V_BUTTON] = 18;
    physical_pins[CAL_I_BUTTON] = 16;
    physical_pins[CAL_OUTPUT_BUTTON] = 17;
    calibration_job_set_service(NULL);
    assert(calibration_job_start(true));
    calibration_job_process();
    calibration_job_status_t status;
    calibration_job_status(&status);
    if (status.state != CAL_JOB_DONE)
        fprintf(stderr, "Full calibration failed: %s row=%u trials=%u at=%u..%u\n",
                status.error, status.row, status.trials, status.lower_us, status.upper_us);
    assert(status.state == CAL_JOB_DONE);
    for (unsigned field = CAL_V_A; field <= CAL_OUTPUT_BUTTON; field++)
        assert(calibration_get((calibration_field_t)field) == physical_pins[field]);
    assert(calibration_get(CAL_V_IDLE_US) >= 1800000 && calibration_get(CAL_V_IDLE_US) <= 2100000);
    assert(calibration_get(CAL_I_EXIT_US) >= 8700000 && calibration_get(CAL_I_EXIT_US) <= 9300000);
    assert(calibration_get(CAL_V_TO_I_COUPLING) == 1);
    assert(calibration_get(CAL_I_TO_V_COUPLING) == 1);
    assert(calibration_loaded() && !calibration_dirty());
    assert(simulated_values[VOLTAGE] == 950 && simulated_values[CURRENT] == 1234);
    assert(!simulated_output);
    calibration_defaults(&config);
    assert(calibration_apply(&config));
    reset_supply(950);
    coupling_flags = 3;
    assert(calibration_job_start(true));
    calibration_job_process();
    calibration_job_status(&status);
    if (status.state != CAL_JOB_DONE)
        fprintf(stderr, "Shared calibration failed: %s\n", status.error);
    assert(status.state == CAL_JOB_DONE);
    assert(calibration_get(CAL_V_TO_I_COUPLING) == 2);
    assert(calibration_get(CAL_I_TO_V_COUPLING) == 2);
    calibration_get_config(&config);
    reset_supply(950);
    calibration_job_set_service(abort_during_output);
    assert(calibration_job_start(true));
    calibration_job_process();
    calibration_job_status(&status);
    assert(status.state == CAL_JOB_ABORTED);
    assert(!simulated_output);
    calibration_config_t restored;
    calibration_get_config(&restored);
    assert(memcmp(&restored, &config, sizeof(config)) == 0);
    assert(!calibration_dirty());
    reset_supply(950);
    calibration_job_set_service(NULL);
    assert(calibration_job_start(true));
    simulated_connected = false;
    sleep_ms(2100);
    calibration_job_process();
    calibration_job_status(&status);
    assert(status.state == CAL_JOB_FAILED);
    assert(strcmp(status.error, "DISPLAY_FEEDBACK_LOST") == 0);
    assert(!calibration_dirty());
    reset_supply(950);
    calibration_job_set_service(NULL);
    storage_failure = true;
    assert(calibration_job_start(true));
    calibration_job_process();
    storage_failure = false;
    calibration_job_status(&status);
    assert(status.state == CAL_JOB_FAILED);
    assert(strcmp(status.error, "FLASH_SAVE_FAILED") == 0);
    calibration_get_config(&restored);
    assert(memcmp(&restored, &config, sizeof(config)) == 0);
    assert(!calibration_dirty());
    calibration_defaults(&config);
    assert(calibration_apply(&config));
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

static void test_diagnostic_scpi(void)
{
    char response[8192];
    reset_supply(950);
    scpi_control_init();
    command("DIAG:ARM 0\nDIAG:VERS?\n", response, sizeof(response));
    assert(strcmp(response, "1\r\n") == 0);
    spi_trace_clear();
    uint8_t bytes[17] = {0xC0};
    spi_trace_record(SPI_TRACE_FRAME, SPI_TRACE_DISPLAY, 100, 200, 0, bytes, sizeof(bytes), &simulated);
    command("DIAG:TRAC? 0\n", response, sizeof(response));
    assert(strncmp(response, "1,1,0,1,\"1|100|200|1|4|0|17|C000", 32) == 0);
    command("DIAG:STAT?\n", response, sizeof(response));
    unsigned commas = 0;
    for (char *cursor = response; *cursor; cursor++)
        commas += *cursor == ',';
    assert(commas == 34);
    command("DIAG:ARM 1\nDIAG:ACT? 1,0,0,10000,61000,11000,7\n", response, sizeof(response));
    assert(strcmp(response, "1\r\n") == 0);
    command("DIAG:RUN\n", response, sizeof(response));
    sleep_ms(10);
    diagnostics_process();
    diagnostic_status_t status;
    diagnostics_status(&status);
    assert(status.completed_id == 7 && !status.running);
    unsigned writes = gpio_writes;
    command("VOLT 1\nSYST:ERR?\n", response, sizeof(response));
    assert(strstr(response, "-200,"));
    assert(gpio_writes == writes);
    command("CAL:FULL 1\nSYST:ERR?\n", response, sizeof(response));
    assert(strstr(response, "-200,"));
    command("DIAG:ARM 0\nSYST:ERR?\n", response, sizeof(response));
    assert(strstr(response, "0,\"No error\""));
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
    char expected_timing[128];
    snprintf(expected_timing, sizeof(expected_timing), "0,0,0,0,0,0,0,0,0,0,%u,%u,0\r\n",
             simulated.timing.blink_period_us[VOLTAGE], simulated.timing.blink_period_us[CURRENT]);
    assert(strcmp(response, expected_timing) == 0);
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
    reset_supply(950);
    command("CAL:PAR? \"V_IDLE_US\"\n", response, sizeof(response));
    assert(strtoul(response, NULL, 10) == 2000000);
    unsigned original_pin = calibration_get(CAL_V_A);
    command("CAL:PAR \"V_A\",28\nSYST:ERR?\n", response, sizeof(response));
    assert(strstr(response, "-224,"));
    assert(calibration_get(CAL_V_A) == original_pin);
    command("CAL:PAR \"V_IDLE_US\",2100000\nCAL:PAR? \"V_IDLE_US\"\n", response, sizeof(response));
    assert(strtoul(response, NULL, 10) == 2100000);
    command("CAL:SAVE\nCAL:PAR \"V_IDLE_US\",2200000\nCAL:LOAD\nCAL:PAR? \"V_IDLE_US\"\n", response, sizeof(response));
    assert(strtoul(response, NULL, 10) == 2100000);
    command("CAL:FULL 0\nSYST:ERR?\n", response, sizeof(response));
    assert(strstr(response, "-200,"));
    command("CAL:FULL 1\nCAL:STAT?\n", response, sizeof(response));
    assert(strstr(response, "\"PENDING\""));
    unsigned writes = gpio_writes;
    command("VOLT 1\n", response, sizeof(response));
    assert(gpio_writes == writes);
    command("CAL:ABOR\n", response, sizeof(response));
    calibration_job_process();
    calibration_job_status_t status;
    calibration_job_status(&status);
    assert(status.state == CAL_JOB_ABORTED);
    assert(calibration_get(CAL_V_IDLE_US) == 2100000);
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
    capture_engine_reset_timing(&engine);
    assert(engine.display.timing.frames == 0);
    assert(engine.display.screen.rows[VOLTAGE].fac == 950);
    uint8_t scan[] = {0x42, 0, 0, 0, 0};
    replay(&engine, scan, sizeof(scan), 600000);
    replay(&engine, scan, sizeof(scan), 610000);
    assert(engine.display.timing.scan_frames == 2);
    assert(engine.display.timing.scan_min_us == 10000);
    assert(engine.display.timing.rejected == 0);
}

static void test_trace(void)
{
    display_snapshot_t snapshot = {0};
    spi_trace_batch_t batch;
    uint8_t bytes[40] = {0xC0};
    snapshot.sequence = 4;
    snapshot.row_valid[VOLTAGE] = true;
    snapshot.row_sequence[VOLTAGE] = 4;
    snapshot.screen.rows[VOLTAGE] = (value_t){950, -2};
    snapshot.editing_sequence[CURRENT] = 4;
    snapshot.editing_digit[CURRENT] = DIGIT_2;
    spi_trace_clear();
    spi_trace_read(0, &batch);
    assert(batch.count == 0 && batch.head == 0);
    spi_trace_record(SPI_TRACE_FRAME, SPI_TRACE_DISPLAY, UINT32_MAX - 5, 10, 0,
                     bytes, sizeof(bytes), &snapshot);
    spi_trace_read(0, &batch);
    assert(batch.count == 1 && batch.lost == 0);
    assert(batch.events[0].flags & SPI_TRACE_TRUNCATED);
    assert(batch.events[0].fresh_mask == 1 && batch.events[0].blink_mask == 2);
    assert(batch.events[0].rows[VOLTAGE].fac == 950);
    assert(batch.events[0].data[0] == 0xC0);
    for (unsigned index = 0; index < SPI_TRACE_CAPACITY + 10; index++)
        spi_trace_record(SPI_TRACE_MARK, 0, index, index, index, NULL, 0, &snapshot);
    spi_trace_read(0, &batch);
    assert(batch.oldest == 12 && batch.lost == 11);
    assert(batch.count == SPI_TRACE_BATCH_SIZE);
    assert(batch.events[0].sequence == batch.oldest);
    spi_trace_read(batch.head, &batch);
    assert(batch.count == 0);
}

static void test_diagnostics(void)
{
    reset_supply(950);
    assert(diagnostics_arm(1));
    diagnostic_action_t first = {1, 100000, 61000, 11000, 1, VOLTAGE, false};
    diagnostic_action_t second = {2, 2000000, 1250, 11000, 2, VOLTAGE, true};
    spi_trace_clear();
    assert(diagnostics_enqueue(&first));
    assert(diagnostics_enqueue(&second));
    assert(diagnostics_run());
    unsigned writes = gpio_writes;
    diagnostics_process();
    assert(gpio_writes == writes);
    sleep_ms(100);
    diagnostics_process();
    diagnostic_status_t status;
    diagnostics_status(&status);
    assert(status.completed_id == 1 && status.queued == 1);
    sleep_ms(1999);
    diagnostics_process();
    diagnostics_status(&status);
    assert(status.completed_id == 1);
    sleep_ms(1);
    diagnostics_process();
    diagnostics_status(&status);
    assert(status.completed_id == 2 && status.queued == 0);
    spi_trace_batch_t batch;
    spi_trace_read(0, &batch);
    assert(batch.count == 10);
    assert(batch.events[0].kind == SPI_TRACE_ACTION_START);
    assert(batch.events[3].kind == SPI_TRACE_ACTION_END);
    assert(batch.events[4].started_us - batch.events[3].ended_us == 2000000);
    assert(diagnostics_arm(0));
    assert(!diagnostics_enqueue(&first));
    simulated_output = true;
    sleep_ms(100);
    assert(!diagnostics_arm(1));
    assert(diagnostics_arm(2));
    simulated_output = false;
    sleep_ms(100);
    diagnostics_process();
    diagnostics_status(&status);
    assert(status.error == 2 && !diagnostics_armed());
    reset_supply(950);
    assert(diagnostics_arm(1));
    assert(diagnostics_enqueue(&first));
    sleep_ms(3100);
    diagnostics_process();
    diagnostics_status(&status);
    assert(status.error == 3 && !status.queued && !diagnostics_armed());
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
    test_trace();
    test_diagnostics();
    test_configuration();
    test_search();
    test_controller();
    test_current_and_modes();
    test_full_calibration();
    test_diagnostic_scpi();
    test_scpi();
    puts("Decoder, bus replay, voltage/current control, modes, full calibration, flash records, and SCPI tests passed");
    return 0;
}