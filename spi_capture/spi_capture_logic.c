#include "spi_capture.h"
#include <string.h>

#define CS_MASK (1u << PIN_CS)
#define CLK_MASK (1u << PIN_CLK)
#define DIO_MASK (1u << PIN_DIO)

static void record_range(uint32_t value, uint32_t *minimum, uint32_t *maximum)
{
    if (*minimum == 0 || value < *minimum)
    {
        *minimum = value;
    }
    if (value > *maximum)
    {
        *maximum = value;
    }
}

void capture_engine_init(capture_engine_t *engine)
{
    memset(engine, 0, sizeof(*engine));
    engine->display.screen.editing_row = UNKNOWN_ROW;
    engine->display.screen.editing_digit = UNKNOWN_DIGIT;
    for (row_t row = VOLTAGE; row < ROW_LEN; row++)
    {
        engine->display.editing_digit[row] = UNKNOWN_DIGIT;
    }
}

static void decode_frame(capture_engine_t *engine, uint32_t now_us)
{
    display_snapshot_t *display = &engine->display;
    uint8_t *data = engine->frame.data + 1;
    display->sequence++;
    display->updated_us = now_us;
    display->screen.output_on =
        (data[output_on_bit_pos.byte] >> output_on_bit_pos.bit) & 1u;
    display->screen.editing_row = UNKNOWN_ROW;
    display->screen.editing_digit = UNKNOWN_DIGIT;
    for (row_t row = VOLTAGE; row < ROW_LEN; row++)
    {
        if (get_value(data, &display->screen.rows[row], row))
        {
            display->row_valid[row] = true;
            display->row_updated_us[row] = now_us;
            display->row_sequence[row] = display->sequence;
        }
        digit_t digit;
        bool blank = decoder_editing_digit(data, row, &digit);
        if (blank)
        {
            display->editing_digit[row] = digit;
            display->editing_sequence[row] = display->sequence;
            display->screen.editing_row = row;
            display->screen.editing_digit = digit;
            if (!engine->blank[row])
            {
                if (engine->blank_seen[row])
                {
                    display->timing.blink_period_us[row] =
                        now_us - engine->blank_started_us[row];
                }
                engine->blank_started_us[row] = now_us;
                engine->blank_seen[row] = true;
            }
        }
        engine->blank[row] = blank;
    }
}

bool capture_engine_sample(capture_engine_t *engine, uint32_t levels, uint32_t now_us)
{
    if (!engine->initialized)
    {
        engine->previous_levels = levels;
        engine->initialized = true;
        return false;
    }
    uint32_t changed = levels ^ engine->previous_levels;
    engine->previous_levels = levels;
    if ((changed & CS_MASK) && !(levels & CS_MASK))
    {
        engine->active = true;
        engine->overflow = false;
        engine->clock_seen = false;
        engine->frame.length = 0;
        engine->byte = 0;
        engine->bits = 0;
        engine->started_us = now_us;
        engine->clock_edge_us = now_us;
        if (engine->start_seen)
        {
            record_range(now_us - engine->previous_start_us,
                         &engine->display.timing.refresh_min_us,
                         &engine->display.timing.refresh_max_us);
        }
        engine->previous_start_us = now_us;
        engine->start_seen = true;
        return false;
    }
    if (!engine->active)
    {
        return false;
    }
    if ((levels & CS_MASK) || now_us - engine->started_us >= CAPTURE_TIMEOUT_US)
    {
        engine->active = false;
        record_range(now_us - engine->started_us,
                     &engine->display.timing.cs_min_us,
                     &engine->display.timing.cs_max_us);
        engine->display.timing.frames++;
        if (!(levels & CS_MASK) || engine->overflow || engine->bits ||
            engine->frame.length != 17 || engine->frame.data[0] != 0xC0)
        {
            engine->display.timing.rejected++;
        }
        else
        {
            decode_frame(engine, now_us);
        }
        return true;
    }
    if (changed & CLK_MASK)
    {
        if (engine->clock_seen)
        {
            if (levels & CLK_MASK)
            {
                record_range(now_us - engine->clock_edge_us,
                             &engine->display.timing.clock_low_min_us,
                             &engine->display.timing.clock_low_max_us);
            }
            else
            {
                record_range(now_us - engine->clock_edge_us,
                             &engine->display.timing.clock_high_min_us,
                             &engine->display.timing.clock_high_max_us);
            }
        }
        engine->clock_seen = true;
        engine->clock_edge_us = now_us;
        if (levels & CLK_MASK)
        {
            engine->byte = (engine->byte >> 1) | ((levels & DIO_MASK) ? 0x80 : 0);
            if (++engine->bits == 8)
            {
                if (engine->frame.length < CAPTURE_BUF_SIZE)
                {
                    engine->frame.data[engine->frame.length++] = engine->byte;
                }
                else
                {
                    engine->overflow = true;
                }
                engine->bits = 0;
                engine->byte = 0;
            }
        }
    }
    return false;
}