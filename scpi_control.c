#include "scpi_control.h"

#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "scpi/scpi.h"
#include "controller.h"
#include "decoder.h"

#define SCPI_INPUT_BUFFER_LENGTH 256
#define SCPI_ERROR_QUEUE_SIZE 8

static char scpi_input_buffer[SCPI_INPUT_BUFFER_LENGTH];
static scpi_error_t scpi_error_queue[SCPI_ERROR_QUEUE_SIZE];
static scpi_t scpi_context;

static size_t scpi_write(scpi_t *context, const char *data, size_t length)
{
    (void)context;
    return fwrite(data, 1, length, stdout);
}

static scpi_result_t scpi_flush(scpi_t *context)
{
    (void)context;
    stdio_flush();
    return SCPI_RES_OK;
}

static int scpi_error(scpi_t *context, int_fast16_t error)
{
    (void)context;
    (void)error;
    return 0;
}

static scpi_result_t scpi_control(scpi_t *context, scpi_ctrl_name_t control, scpi_reg_val_t value)
{
    (void)context;
    (void)control;
    (void)value;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_reset(scpi_t *context)
{
    (void)context;
    return SCPI_RES_OK;
}

static scpi_result_t display_value(scpi_t *context, row_t row)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!snapshot.row_valid[row] ||
        time_us_32() - snapshot.row_updated_us[row] >= DISPLAY_STALE_US)
    {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    value_t value = snapshot.screen.rows[row];
    SCPI_ResultDouble(context, (double)value.fac * pow(10.0, value.exp));
    return SCPI_RES_OK;
}

static scpi_result_t scpi_voltage(scpi_t *context)
{
    double voltage;
    if (!SCPI_ParamDouble(context, &voltage, TRUE))
    {
        return SCPI_RES_ERR;
    }

    return controller_set_voltage(voltage) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_voltage_query(scpi_t *context)
{
    return display_value(context, VOLTAGE);
}

static scpi_result_t scpi_current_query(scpi_t *context)
{
    return display_value(context, CURRENT);
}

static scpi_result_t scpi_power_query(scpi_t *context)
{
    return display_value(context, POWER);
}

static scpi_result_t scpi_display_timing(scpi_t *context)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    display_timing_t *timing = &snapshot.timing;
    SCPI_ResultUInt32(context, timing->frames);
    SCPI_ResultUInt32(context, timing->rejected);
    SCPI_ResultUInt32(context, timing->clock_high_min_us);
    SCPI_ResultUInt32(context, timing->clock_high_max_us);
    SCPI_ResultUInt32(context, timing->clock_low_min_us);
    SCPI_ResultUInt32(context, timing->clock_low_max_us);
    SCPI_ResultUInt32(context, timing->cs_min_us);
    SCPI_ResultUInt32(context, timing->cs_max_us);
    SCPI_ResultUInt32(context, timing->refresh_min_us);
    SCPI_ResultUInt32(context, timing->refresh_max_us);
    for (row_t row = VOLTAGE; row < ROW_LEN; row++)
        SCPI_ResultUInt32(context, timing->blink_period_us[row]);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_display_status(scpi_t *context)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    SCPI_ResultUInt32(context, snapshot.sequence);
    SCPI_ResultInt32(context, snapshot.sequence ?
                     (int32_t)(time_us_32() - snapshot.updated_us) : -1);
    for (row_t row = VOLTAGE; row < ROW_LEN; row++)
        SCPI_ResultInt32(context, snapshot.row_valid[row] ?
                         (int32_t)(time_us_32() - snapshot.row_updated_us[row]) : -1);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_output(scpi_t *context)
{
    scpi_bool_t enabled;
    if (!SCPI_ParamBool(context, &enabled, TRUE))
    {
        return SCPI_RES_ERR;
    }

    return controller_set_output(enabled != FALSE) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_output_toggle(scpi_t *context)
{
    (void)context;
    return controller_toggle_output() ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_output_query(scpi_t *context)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!snapshot.sequence || time_us_32() - snapshot.updated_us >= DISPLAY_STALE_US)
        return SCPI_RES_ERR;
    SCPI_ResultBool(context, snapshot.screen.output_on);
    return SCPI_RES_OK;
}

static const scpi_command_t scpi_commands[] = {
    { .pattern = "*IDN?", .callback = SCPI_CoreIdnQ },
    { .pattern = "*OPC?", .callback = SCPI_CoreOpcQ },
    { .pattern = "*CLS", .callback = SCPI_CoreCls },
    { .pattern = "SYSTem:ERRor[:NEXT]?", .callback = SCPI_SystemErrorNextQ },
    { .pattern = "SYSTem:ERRor:COUNt?", .callback = SCPI_SystemErrorCountQ },
    { .pattern = "SYSTem:DISPlay:TIMing?", .callback = scpi_display_timing },
    { .pattern = "SYSTem:DISPlay:STATus?", .callback = scpi_display_status },
    { .pattern = "VOLTage", .callback = scpi_voltage },
    { .pattern = "VOLTage?", .callback = scpi_voltage_query },
    { .pattern = "SOURce:VOLTage", .callback = scpi_voltage },
    { .pattern = "SOURce:VOLTage?", .callback = scpi_voltage_query },
    { .pattern = "CURRent?", .callback = scpi_current_query },
    { .pattern = "DISPlay:VOLTage?", .callback = scpi_voltage_query },
    { .pattern = "DISPlay:CURRent?", .callback = scpi_current_query },
    { .pattern = "DISPlay:POWer?", .callback = scpi_power_query },
    { .pattern = "OUTPut", .callback = scpi_output },
    { .pattern = "OUTPut:Toggle", .callback = scpi_output_toggle },
    { .pattern = "OUTPut?", .callback = scpi_output_query },
    { .pattern = "OUT", .callback = scpi_output },
    { .pattern = "OUT:Toggle", .callback = scpi_output_toggle },
    { .pattern = "OUT?", .callback = scpi_output_query },
    SCPI_CMD_LIST_END
};

static scpi_interface_t scpi_interface = {
    .error = scpi_error,
    .write = scpi_write,
    .control = scpi_control,
    .flush = scpi_flush,
    .reset = scpi_reset,
};

void scpi_control_init(void)
{
    SCPI_Init(&scpi_context,
              scpi_commands,
              &scpi_interface,
              scpi_units_def,
              "Pico", "Powersupply", NULL, "2",
              scpi_input_buffer, sizeof(scpi_input_buffer),
              scpi_error_queue, SCPI_ERROR_QUEUE_SIZE);
}

void scpi_control_process(void)
{
    int character;
    while ((character = getchar_timeout_us(0)) >= 0)
    {
        char input = (char)character;
        SCPI_Input(&scpi_context, &input, 1);
    }
}
