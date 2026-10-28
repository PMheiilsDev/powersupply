#include "scpi_control.h"

#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "scpi/scpi.h"
#include "controller.h"
#include "decoder.h"
#include "calibration_job.h"
#include "diagnostics.h"
#include "spi_trace.h"

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

    return !calibration_job_busy() && !diagnostics_armed() && controller_set_voltage(voltage) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_current(scpi_t *context)
{
    double current;
    if (!SCPI_ParamDouble(context, &current, TRUE))
        return SCPI_RES_ERR;
    return !calibration_job_busy() && !diagnostics_armed() && controller_set_current(current) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t requested_value(scpi_t *context, row_t row, bool actual)
{
    value_t value;
    if (calibration_job_busy() || diagnostics_armed() ||
        !(actual ? controller_read_actual(row, &value) : controller_read_target(row, &value)))
        return SCPI_RES_ERR;
    SCPI_ResultDouble(context, value.fac * pow(10.0, value.exp));
    return SCPI_RES_OK;
}

static scpi_result_t target_voltage(scpi_t *context) { return requested_value(context, VOLTAGE, false); }
static scpi_result_t target_current(scpi_t *context) { return requested_value(context, CURRENT, false); }
static scpi_result_t actual_voltage(scpi_t *context) { return requested_value(context, VOLTAGE, true); }
static scpi_result_t actual_current(scpi_t *context) { return requested_value(context, CURRENT, true); }

static bool configuration_allowed(void)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    return !calibration_job_busy() && !diagnostics_armed() && snapshot.sequence && !snapshot.screen.output_on &&
           time_us_32() - snapshot.updated_us < DISPLAY_STALE_US;
}

static int parameter_field(scpi_t *context)
{
    char name[32];
    size_t length;
    if (!SCPI_ParamCopyText(context, name, sizeof(name), &length, TRUE))
        return -1;
    int field = calibration_find_field(name, length);
    if (field < 0)
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
    return field;
}

static scpi_result_t calibration_parameter_query(scpi_t *context)
{
    int field = parameter_field(context);
    if (field < 0)
        return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, calibration_get((calibration_field_t)field));
    return SCPI_RES_OK;
}

static scpi_result_t calibration_parameter(scpi_t *context)
{
    int field = parameter_field(context);
    uint32_t value;
    if (field < 0 || !SCPI_ParamUInt32(context, &value, TRUE) || !configuration_allowed())
        return SCPI_RES_ERR;
    if (!calibration_set((calibration_field_t)field, value))
    {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    if (field != CAL_TESTED_MASK)
        (void)calibration_set(CAL_TESTED_MASK, 0);
    controller_init();
    return SCPI_RES_OK;
}

static scpi_result_t calibration_parameters(scpi_t *context)
{
    for (calibration_field_t field = 0; field < CAL_FIELD_COUNT; field++)
        SCPI_ResultText(context, calibration_field_name(field));
    return SCPI_RES_OK;
}

static scpi_result_t calibration_values(scpi_t *context)
{
    for (calibration_field_t field = 0; field < CAL_FIELD_COUNT; field++)
        SCPI_ResultUInt32(context, calibration_get(field));
    return SCPI_RES_OK;
}

static scpi_result_t calibration_store(scpi_t *context)
{
    (void)context;
    return configuration_allowed() && calibration_save() ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t calibration_recall(scpi_t *context)
{
    (void)context;
    if (!configuration_allowed() || !calibration_load())
        return SCPI_RES_ERR;
    controller_init();
    return SCPI_RES_OK;
}

static scpi_result_t calibration_full(scpi_t *context)
{
    scpi_bool_t confirmed;
    if (!SCPI_ParamBool(context, &confirmed, TRUE))
        return SCPI_RES_ERR;
    return !diagnostics_armed() && calibration_job_start(confirmed != FALSE) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t calibration_abort(scpi_t *context)
{
    (void)context;
    calibration_job_abort();
    return SCPI_RES_OK;
}

static scpi_result_t calibration_status(scpi_t *context)
{
    calibration_job_status_t status;
    calibration_job_status(&status);
    SCPI_ResultUInt32(context, status.state);
    SCPI_ResultText(context, status.phase);
    SCPI_ResultUInt32(context, status.row);
    SCPI_ResultUInt32(context, status.lower_us);
    SCPI_ResultUInt32(context, status.upper_us);
    SCPI_ResultUInt32(context, status.trials);
    SCPI_ResultText(context, status.error);
    SCPI_ResultBool(context, calibration_loaded());
    SCPI_ResultBool(context, calibration_dirty());
    return SCPI_RES_OK;
}

static scpi_result_t operation_complete(scpi_t *context)
{
    return calibration_job_busy() || diagnostics_busy() ? SCPI_RES_ERR : SCPI_CoreOpcQ(context);
}

static scpi_result_t diagnostic_version(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1);
    return SCPI_RES_OK;
}

static scpi_result_t diagnostic_arm(scpi_t *context)
{
    uint32_t mode;
    if (!SCPI_ParamUInt32(context, &mode, TRUE))
        return SCPI_RES_ERR;
    return diagnostics_arm(mode) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t diagnostic_run(scpi_t *context)
{
    (void)context;
    return diagnostics_run() ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t diagnostic_enqueue(scpi_t *context)
{
    uint32_t kind, row, up;
    diagnostic_action_t action = {0};
    if (!SCPI_ParamUInt32(context, &kind, TRUE) || !SCPI_ParamUInt32(context, &row, TRUE) ||
        !SCPI_ParamUInt32(context, &up, TRUE) || !SCPI_ParamUInt32(context, &action.delay_us, TRUE) ||
        !SCPI_ParamUInt32(context, &action.pulse_us, TRUE) || !SCPI_ParamUInt32(context, &action.gap_us, TRUE) ||
        !SCPI_ParamUInt32(context, &action.id, TRUE) || kind > 2 || row > CURRENT || up > 1)
        return SCPI_RES_ERR;
    action.kind = (uint8_t)kind;
    action.row = (uint8_t)row;
    action.up = up != 0;
    if (!diagnostics_enqueue(&action))
        return SCPI_RES_ERR;
    diagnostic_status_t status;
    diagnostics_status(&status);
    SCPI_ResultUInt32(context, status.queued);
    return SCPI_RES_OK;
}

static scpi_result_t diagnostic_state(scpi_t *context)
{
    diagnostic_status_t status;
    display_snapshot_t snapshot;
    diagnostics_status(&status);
    spi_capture_snapshot(&snapshot);
    SCPI_ResultUInt32(context, 1);
    SCPI_ResultUInt32(context, time_us_32());
    SCPI_ResultUInt32(context, status.arm_mode);
    SCPI_ResultUInt32(context, status.queued);
    SCPI_ResultBool(context, status.running);
    SCPI_ResultUInt32(context, status.completed_id);
    SCPI_ResultUInt32(context, status.ended_us);
    SCPI_ResultUInt32(context, status.error);
    SCPI_ResultUInt32(context, snapshot.sequence);
    SCPI_ResultUInt32(context, snapshot.updated_us);
    SCPI_ResultBool(context, snapshot.screen.output_on);
    for (row_t row = VOLTAGE; row < ROW_LEN; row++)
    {
        SCPI_ResultUInt32(context, snapshot.screen.rows[row].fac);
        SCPI_ResultInt32(context, snapshot.screen.rows[row].exp);
        SCPI_ResultBool(context, snapshot.row_valid[row]);
        SCPI_ResultUInt32(context, snapshot.row_sequence[row]);
        SCPI_ResultUInt32(context, snapshot.row_updated_us[row]);
        SCPI_ResultUInt32(context, snapshot.editing_digit[row]);
        SCPI_ResultUInt32(context, snapshot.editing_sequence[row]);
        SCPI_ResultUInt32(context, snapshot.editing_updated_us[row]);
    }
    return SCPI_RES_OK;
}

static scpi_result_t diagnostic_trace_clear(scpi_t *context)
{
    (void)context;
    if (diagnostics_busy() || calibration_job_busy())
        return SCPI_RES_ERR;
    spi_capture_trace_clear();
    return SCPI_RES_OK;
}

static scpi_result_t diagnostic_trace(scpi_t *context)
{
    uint32_t after;
    if (!SCPI_ParamUInt32(context, &after, TRUE))
        return SCPI_RES_ERR;
    diagnostics_contact();
    static spi_trace_batch_t batch;
    spi_capture_trace_read(after, &batch);
    SCPI_ResultUInt32(context, batch.head);
    SCPI_ResultUInt32(context, batch.oldest);
    SCPI_ResultUInt32(context, batch.lost);
    SCPI_ResultUInt32(context, batch.count);
    for (uint32_t index = 0; index < batch.count; index++)
    {
        const spi_trace_event_t *event = &batch.events[index];
        char hex[SPI_TRACE_DATA_SIZE * 2 + 1];
        unsigned length = event->length < SPI_TRACE_DATA_SIZE ? event->length : SPI_TRACE_DATA_SIZE;
        for (unsigned byte = 0; byte < length; byte++)
            snprintf(hex + byte * 2, 3, "%02X", event->data[byte]);
        hex[length * 2] = 0;
        char record[320];
        snprintf(record, sizeof(record),
                 "%lu|%lu|%lu|%u|%u|%lu|%u|%s|%lu|%u|%u|%u|%u|%u|%d|%u|%u|%d|%u|%u|%d|%u",
                 (unsigned long)event->sequence, (unsigned long)event->started_us,
                 (unsigned long)event->ended_us, event->kind, event->flags,
                 (unsigned long)event->action_id, event->length, hex,
                 (unsigned long)event->screen_sequence, event->output_on,
                 event->valid_mask, event->fresh_mask, event->blink_mask,
                 event->rows[VOLTAGE].fac, event->rows[VOLTAGE].exp, event->editing_digit[VOLTAGE],
                 event->rows[CURRENT].fac, event->rows[CURRENT].exp, event->editing_digit[CURRENT],
                 event->rows[POWER].fac, event->rows[POWER].exp, event->editing_digit[POWER]);
        SCPI_ResultText(context, record);
    }
    return SCPI_RES_OK;
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

static scpi_result_t scpi_display_cadence(scpi_t *context)
{
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    SCPI_ResultUInt32(context, snapshot.timing.scan_frames);
    SCPI_ResultUInt32(context, snapshot.timing.scan_min_us);
    SCPI_ResultUInt32(context, snapshot.timing.scan_max_us);
    SCPI_ResultUInt32(context, snapshot.timing.display_min_us);
    SCPI_ResultUInt32(context, snapshot.timing.display_max_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_output(scpi_t *context)
{
    scpi_bool_t enabled;
    if (!SCPI_ParamBool(context, &enabled, TRUE))
    {
        return SCPI_RES_ERR;
    }

    return !calibration_job_busy() && !diagnostics_armed() && controller_set_output(enabled != FALSE) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_output_toggle(scpi_t *context)
{
    (void)context;
    return !calibration_job_busy() && !diagnostics_armed() && controller_toggle_output() ? SCPI_RES_OK : SCPI_RES_ERR;
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
    { .pattern = "DIAGnostic:VERSion?", .callback = diagnostic_version },
    { .pattern = "DIAGnostic:ARM", .callback = diagnostic_arm },
    { .pattern = "DIAGnostic:RUN", .callback = diagnostic_run },
    { .pattern = "DIAGnostic:ACTion?", .callback = diagnostic_enqueue },
    { .pattern = "DIAGnostic:STATe?", .callback = diagnostic_state },
    { .pattern = "DIAGnostic:TRACe?", .callback = diagnostic_trace },
    { .pattern = "DIAGnostic:TRACe:CLEar", .callback = diagnostic_trace_clear },
    { .pattern = "*IDN?", .callback = SCPI_CoreIdnQ },
    { .pattern = "*OPC?", .callback = operation_complete },
    { .pattern = "CALibration:PARameter?", .callback = calibration_parameter_query },
    { .pattern = "CALibration:PARameter", .callback = calibration_parameter },
    { .pattern = "CALibration:LIST?", .callback = calibration_parameters },
    { .pattern = "CALibration:CONFiguration?", .callback = calibration_values },
    { .pattern = "CALibration:SAVE", .callback = calibration_store },
    { .pattern = "CALibration:LOAD", .callback = calibration_recall },
    { .pattern = "CALibration:FULL", .callback = calibration_full },
    { .pattern = "CALibration:ABORt", .callback = calibration_abort },
    { .pattern = "CALibration:STATus?", .callback = calibration_status },
    { .pattern = "*CLS", .callback = SCPI_CoreCls },
    { .pattern = "SYSTem:ERRor[:NEXT]?", .callback = SCPI_SystemErrorNextQ },
    { .pattern = "SYSTem:ERRor:COUNt?", .callback = SCPI_SystemErrorCountQ },
    { .pattern = "SYSTem:DISPlay:TIMing?", .callback = scpi_display_timing },
    { .pattern = "SYSTem:DISPlay:STATus?", .callback = scpi_display_status },
    { .pattern = "SYSTem:DISPlay:CADence?", .callback = scpi_display_cadence },
    { .pattern = "VOLTage", .callback = scpi_voltage },
    { .pattern = "VOLTage?", .callback = scpi_voltage_query },
    { .pattern = "SOURce:VOLTage", .callback = scpi_voltage },
    { .pattern = "SOURce:VOLTage?", .callback = target_voltage },
    { .pattern = "CURRent", .callback = scpi_current },
    { .pattern = "SOURce:CURRent", .callback = scpi_current },
    { .pattern = "SOURce:CURRent?", .callback = target_current },
    { .pattern = "MEASure:VOLTage[:DC]?", .callback = actual_voltage },
    { .pattern = "MEASure:CURRent[:DC]?", .callback = actual_current },
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
    calibration_job_set_service(scpi_control_process);
    SCPI_Init(&scpi_context,
              scpi_commands,
              &scpi_interface,
              scpi_units_def,
              "Pico", "Powersupply", NULL, "3",
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
