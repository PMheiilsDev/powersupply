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
    printf("-%d,\"SCPI error\"\r\n", (int)error);
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

static double current_voltage(void)
{
    value_t value = screendata.rows[VOLTAGE];
    return (double)value.fac * pow(10.0, (double)value.exp);
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
    SCPI_ResultDouble(context, current_voltage());
    return SCPI_RES_OK;
}

static scpi_result_t scpi_output(scpi_t *context)
{
    scpi_bool_t enabled;
    if (!SCPI_ParamBool(context, &enabled, TRUE))
    {
        return SCPI_RES_ERR;
    }

    controller_set_output(enabled != FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_output_toggle(scpi_t *context)
{
    (void)context;
    controller_toggle_output();
    return SCPI_RES_OK;
}

static scpi_result_t scpi_output_query(scpi_t *context)
{
    SCPI_ResultBool(context, controller_get_output());
    return SCPI_RES_OK;
}

static const scpi_command_t scpi_commands[] = {
    { .pattern = "VOLTage", .callback = scpi_voltage },
    { .pattern = "VOLTage?", .callback = scpi_voltage_query },
    { .pattern = "OUTput", .callback = scpi_output },
    { .pattern = "OUTput:Toggle", .callback = scpi_output_toggle },
    { .pattern = "OUTput?", .callback = scpi_output_query },
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
              "OpenAI", "Powersupply", NULL, "1",
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
