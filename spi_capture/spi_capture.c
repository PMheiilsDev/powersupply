#include "pico/stdlib.h"
#include "pico/critical_section.h"
#include "pico/flash.h"
#include "spi_capture.h"

static critical_section_t display_lock;
static display_snapshot_t published;
static capture_engine_t engine;
static volatile bool reset_timing;

void spi_capture_init(void)
{
    capture_engine_init(&engine);
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

void lambda_capture_callback(void)
{
    (void)flash_safe_execute_core_init();
    while (true)
    {
        uint32_t levels = gpio_get_all();
        if (capture_engine_sample(&engine, levels, time_us_32()))
        {
            critical_section_enter_blocking(&display_lock);
            if (reset_timing)
            {
                capture_engine_reset_timing(&engine);
                reset_timing = false;
            }
            published = engine.display;
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

