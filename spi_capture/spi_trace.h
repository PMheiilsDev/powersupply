#pragma once
#include "spi_capture.h"

#define SPI_TRACE_CAPACITY 256u
#define SPI_TRACE_DATA_SIZE 32u
#define SPI_TRACE_BATCH_SIZE 24u

enum
{
    SPI_TRACE_FRAME = 1,
    SPI_TRACE_ACTION_START = 2,
    SPI_TRACE_ACTION_END = 3,
    SPI_TRACE_MARK = 4,
    SPI_TRACE_EDGE = 5
};

enum
{
    SPI_TRACE_PARTIAL = 1,
    SPI_TRACE_TRUNCATED = 2,
    SPI_TRACE_DISPLAY = 4,
    SPI_TRACE_TIMEOUT = 8,
    SPI_TRACE_OVERFLOW = 16
};

typedef struct
{
    uint32_t sequence;
    uint32_t started_us;
    uint32_t ended_us;
    uint32_t action_id;
    uint32_t screen_sequence;
    uint16_t length;
    uint8_t kind;
    uint8_t flags;
    uint8_t output_on;
    uint8_t valid_mask;
    uint8_t fresh_mask;
    uint8_t blink_mask;
    value_t rows[ROW_LEN];
    uint8_t editing_digit[ROW_LEN];
    uint8_t data[SPI_TRACE_DATA_SIZE];
} spi_trace_event_t;

typedef struct
{
    uint32_t head;
    uint32_t oldest;
    uint32_t lost;
    uint32_t count;
    spi_trace_event_t events[SPI_TRACE_BATCH_SIZE];
} spi_trace_batch_t;

void spi_trace_clear(void);
void spi_trace_record(uint8_t kind, uint8_t flags, uint32_t started_us,
                      uint32_t ended_us, uint32_t action_id,
                      const uint8_t *data, uint16_t length,
                      const display_snapshot_t *snapshot);
void spi_trace_read(uint32_t after, spi_trace_batch_t *batch);
void spi_capture_trace_read(uint32_t after, spi_trace_batch_t *batch);
void spi_capture_trace_clear(void);
void spi_capture_trace_marker(uint8_t kind, uint32_t action_id, uint32_t started_us,
                              const uint8_t *data, uint16_t length);