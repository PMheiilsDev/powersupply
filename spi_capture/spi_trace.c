#include "spi_trace.h"
#include <string.h>

static spi_trace_event_t events[SPI_TRACE_CAPACITY];
static uint32_t head;
static uint32_t count;

void spi_trace_clear(void)
{
    head = count = 0;
}

void spi_trace_record(uint8_t kind, uint8_t flags, uint32_t started_us,
                      uint32_t ended_us, uint32_t action_id,
                      const uint8_t *data, uint16_t length,
                      const display_snapshot_t *snapshot)
{
    spi_trace_event_t *event = &events[head % SPI_TRACE_CAPACITY];
    memset(event, 0, sizeof(*event));
    event->sequence = ++head;
    event->started_us = started_us;
    event->ended_us = ended_us;
    event->action_id = action_id;
    event->kind = kind;
    event->flags = flags | (length > SPI_TRACE_DATA_SIZE ? SPI_TRACE_TRUNCATED : 0);
    event->length = length;
    if (data)
        memcpy(event->data, data, length < SPI_TRACE_DATA_SIZE ? length : SPI_TRACE_DATA_SIZE);
    event->screen_sequence = snapshot->sequence;
    event->output_on = snapshot->screen.output_on;
    for (row_t row = VOLTAGE; row < ROW_LEN; row++)
    {
        event->rows[row] = snapshot->screen.rows[row];
        event->editing_digit[row] = snapshot->editing_digit[row];
        if (snapshot->row_valid[row])
            event->valid_mask |= 1u << row;
        if ((flags & SPI_TRACE_DISPLAY) && snapshot->row_sequence[row] == snapshot->sequence)
            event->fresh_mask |= 1u << row;
        if ((flags & SPI_TRACE_DISPLAY) && snapshot->editing_sequence[row] == snapshot->sequence)
            event->blink_mask |= 1u << row;
    }
    if (count < SPI_TRACE_CAPACITY)
        count++;
}

void spi_trace_read(uint32_t after, spi_trace_batch_t *batch)
{
    batch->head = head;
    batch->oldest = count ? head - count + 1 : head + 1;
    batch->lost = 0;
    batch->count = 0;
    if (!count || after == head)
        return;
    uint32_t next = after + 1;
    if ((int32_t)(batch->oldest - next) > 0)
    {
        batch->lost = batch->oldest - next;
        next = batch->oldest;
    }
    if ((int32_t)(next - head) > 0)
        return;
    uint32_t available = head - next + 1;
    batch->count = available < SPI_TRACE_BATCH_SIZE ? available : SPI_TRACE_BATCH_SIZE;
    for (uint32_t index = 0; index < batch->count; index++)
        batch->events[index] = events[(next + index - 1) % SPI_TRACE_CAPACITY];
}