#include "pico/stdlib.h"
#include "pico/critical_section.h"
#include "pico/flash.h"
#include "spi_capture.h"
#include "spi_trace.h"

static critical_section_t display_lock;
static display_snapshot_t published;
static capture_engine_t engine;
static volatile bool reset_timing;

void spi_capture_init(void)
{
    capture_engine_init(&engine);
    spi_trace_clear();
    published = engine.display;
    critical_section_init(&display_lock);
    const unsigned pins[] = {PIN_DIO, PIN_CLK, PIN_CS};
    for (unsigned index = 0; index < sizeof(pins) / sizeof(pins[0]); index++)
    {
        gpio_init(pins[index]);
        gpio_set_dir(pins[index], GPIO_IN);
        gpio_pull_up(pins[index]);
    }
}

void spi_capture_snapshot(display_snapshot_t *snapshot)
{
    critical_section_enter_blocking(&display_lock);
    *snapshot = published;
    critical_section_exit(&display_lock);
}

void spi_capture_reset_timing(void)
{
    critical_section_enter_blocking(&display_lock);
    reset_timing = true;
    critical_section_exit(&display_lock);
}

void spi_capture_trace_read(uint32_t after, spi_trace_batch_t *batch)
{
    critical_section_enter_blocking(&display_lock);
    spi_trace_read(after, batch);
    critical_section_exit(&display_lock);
}

void spi_capture_trace_clear(void)
{
    critical_section_enter_blocking(&display_lock);
    spi_trace_clear();
    critical_section_exit(&display_lock);
}

void spi_capture_trace_marker(uint8_t kind, uint32_t action_id, uint32_t started_us,
                              const uint8_t *data, uint16_t length)
{
    uint32_t ended_us = time_us_32();
    critical_section_enter_blocking(&display_lock);
    spi_trace_record(kind, 0, started_us, ended_us, action_id, data, length, &published);
    critical_section_exit(&display_lock);
}

void lambda_capture_callback(void)
{
    (void)flash_safe_execute_core_init();
    while (true)
    {
        uint32_t levels = gpio_get_all();
        if (capture_engine_sample(&engine, levels, time_us_32()))
        {
            uint32_t ended_us = time_us_32();
            uint8_t flags = engine.bits ? SPI_TRACE_PARTIAL : 0;
            if (engine.overflow)
                flags |= SPI_TRACE_OVERFLOW;
            if (!(levels & (1u << PIN_CS)))
                flags |= SPI_TRACE_TIMEOUT;
            if (!flags && engine.frame.length == 17 && engine.frame.data[0] == 0xC0)
                flags |= SPI_TRACE_DISPLAY;
            critical_section_enter_blocking(&display_lock);
            if (reset_timing)
            {
                capture_engine_reset_timing(&engine);
                reset_timing = false;
            }
            published = engine.display;
            spi_trace_record(SPI_TRACE_FRAME, flags, engine.started_us, ended_us, 0,
                             engine.frame.data, (uint16_t)engine.frame.length, &published);
            critical_section_exit(&display_lock);
        }
    }
}

bool spi_capture_wait(uint32_t after_sequence, uint32_t timeout_us,
                      display_snapshot_t *snapshot)
{
    uint32_t started = time_us_32();
    do
    {
        spi_capture_snapshot(snapshot);
        if (snapshot->sequence != after_sequence)
        {
            return true;
        }
        sleep_ms(1);
    } while (time_us_32() - started < timeout_us);
    return false;
}

