#include "diagnostics.h"
#include "calibration_job.h"
#include "spi_trace.h"
#include "pico/stdlib.h"

static diagnostic_action_t actions[DIAGNOSTIC_QUEUE_SIZE];
static diagnostic_status_t status;
static unsigned read_index;
static bool expected_output;
static uint32_t previous_gap_us;
static uint32_t contacted_us;

void diagnostics_contact(void) { contacted_us = time_us_32(); }

bool diagnostics_armed(void) { return status.arm_mode != 0; }
bool diagnostics_busy(void) { return status.queued != 0; }
void diagnostics_status(diagnostic_status_t *result)
{
    diagnostics_contact();
    *result = status;
}

static void release_lines(void)
{
    for (calibration_field_t field = CAL_V_A; field <= CAL_I_BUTTON; field++)
        gpio_put(calibration_get(field), 0);
}

bool diagnostics_arm(unsigned mode)
{
    if (mode == 0)
    {
        release_lines();
        status.arm_mode = status.queued = 0;
        status.running = false;
        read_index = 0;
        return true;
    }
    if (mode > 2 || diagnostics_busy() || calibration_job_busy())
        return false;
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!snapshot.sequence || time_us_32() - snapshot.updated_us >= DISPLAY_STALE_US ||
        (snapshot.screen.output_on && mode != 2))
        return false;
    expected_output = snapshot.screen.output_on;
    status = (diagnostic_status_t){.arm_mode = mode, .ended_us = time_us_32()};
    diagnostics_contact();
    previous_gap_us = 0;
    read_index = 0;
    return true;
}

bool diagnostics_enqueue(const diagnostic_action_t *action)
{
    if (!diagnostics_armed() || status.running || status.error || status.queued >= DIAGNOSTIC_QUEUE_SIZE ||
        action->row > CURRENT || !action->id || action->delay_us > 30000000u ||
        action->gap_us > 1000000u || (action->kind != 1 && action->kind != 2) ||
        (action->kind == 1 && (action->pulse_us < 1000 || action->pulse_us > 300000)) ||
        (action->kind == 2 && (action->pulse_us < 100 || action->pulse_us > 20000)))
        return false;
    actions[(read_index + status.queued) % DIAGNOSTIC_QUEUE_SIZE] = *action;
    status.queued++;
    diagnostics_contact();
    return true;
}

bool diagnostics_run(void)
{
    if (!diagnostics_armed() || status.running || !status.queued)
        return false;
    diagnostics_contact();
    status.running = true;
    if (!status.completed_id)
        status.ended_us = time_us_32();
    return true;
}

static bool check_feedback(void)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!snapshot.sequence || time_us_32() - snapshot.updated_us >= DISPLAY_STALE_US)
        status.error = 1;
    else if (snapshot.screen.output_on != expected_output)
        status.error = 2;
    if (status.error)
    {
        status.queued = status.arm_mode = 0;
        status.running = false;
        release_lines();
        return false;
    }
    return true;
}

static void edge(unsigned pin, bool high, uint32_t action_id, row_t row, unsigned index)
{
    gpio_put(pin, high);
    uint32_t timestamp = time_us_32();
    uint8_t data[] = {(uint8_t)row, (uint8_t)pin, high, (uint8_t)index};
    spi_capture_trace_marker(SPI_TRACE_EDGE, action_id, timestamp, data, sizeof(data));
}

void diagnostics_process(void)
{
    if (diagnostics_armed() && time_us_32() - contacted_us >= 3000000u)
        status.error = 3;
    if (!diagnostics_armed() || !check_feedback() || !status.running || !status.queued)
        return;
    diagnostic_action_t action = actions[read_index];
    uint32_t delay = action.delay_us > previous_gap_us ? action.delay_us : previous_gap_us;
    if (time_us_32() - status.ended_us < delay)
        return;
    status.queued--;
    read_index = (read_index + 1) % DIAGNOSTIC_QUEUE_SIZE;
    uint32_t started = time_us_32();
    uint8_t metadata[] = {action.kind, action.row, action.up};
    spi_capture_trace_marker(SPI_TRACE_ACTION_START, action.id, started, metadata, sizeof(metadata));
    row_t row = (row_t)action.row;
    if (action.kind == 1)
    {
        unsigned pin = calibration_get(row == VOLTAGE ? CAL_V_BUTTON : CAL_I_BUTTON);
        edge(pin, true, action.id, row, 0);
        sleep_us(action.pulse_us);
        edge(pin, false, action.id, row, 1);
    }
    else
    {
        unsigned pin_a = calibration_get(row == VOLTAGE ? CAL_V_A : CAL_I_A);
        unsigned pin_b = calibration_get(row == VOLTAGE ? CAL_V_B : CAL_I_B);
        unsigned first = action.up ? pin_a : pin_b;
        unsigned second = action.up ? pin_b : pin_a;
        edge(first, true, action.id, row, 0);
        sleep_us(action.pulse_us);
        edge(second, true, action.id, row, 1);
        sleep_us(action.pulse_us);
        edge(first, false, action.id, row, 2);
        sleep_us(action.pulse_us);
        edge(second, false, action.id, row, 3);
    }
    status.ended_us = time_us_32();
    status.completed_id = action.id;
    previous_gap_us = action.gap_us;
    spi_capture_trace_marker(SPI_TRACE_ACTION_END, action.id, started, metadata, sizeof(metadata));
    if (!status.queued)
        status.running = false;
    (void)check_feedback();
}