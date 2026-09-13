#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "decoder.h"

#define PIN_DIO 28
#define PIN_CLK 21
#define PIN_CS 22
#define CAPTURE_BUF_SIZE 256
#define CAPTURE_TIMEOUT_US 100000u
#define DISPLAY_STALE_US 2000000u

typedef struct
{
    uint8_t data[CAPTURE_BUF_SIZE];
    uint32_t length;
} spi_frame_t;

typedef struct
{
    uint32_t frames;
    uint32_t rejected;
    uint32_t clock_high_min_us;
    uint32_t clock_high_max_us;
    uint32_t clock_low_min_us;
    uint32_t clock_low_max_us;
    uint32_t cs_min_us;
    uint32_t cs_max_us;
    uint32_t refresh_min_us;
    uint32_t refresh_max_us;
    uint32_t blink_period_us[ROW_LEN];
} display_timing_t;

typedef struct
{
    screendata_t screen;
    bool row_valid[ROW_LEN];
    uint32_t row_updated_us[ROW_LEN];
    uint32_t row_sequence[ROW_LEN];
    digit_t editing_digit[ROW_LEN];
    uint32_t editing_sequence[ROW_LEN];
    uint32_t sequence;
    uint32_t updated_us;
    display_timing_t timing;
} display_snapshot_t;

typedef struct
{
    display_snapshot_t display;
    spi_frame_t frame;
    uint32_t previous_levels;
    uint32_t started_us;
    uint32_t previous_start_us;
    uint32_t clock_edge_us;
    uint32_t blank_started_us[ROW_LEN];
    uint8_t byte;
    uint8_t bits;
    bool initialized;
    bool active;
    bool overflow;
    bool clock_seen;
    bool start_seen;
    bool blank[ROW_LEN];
    bool blank_seen[ROW_LEN];
} capture_engine_t;

void capture_engine_init(capture_engine_t *engine);
bool capture_engine_sample(capture_engine_t *engine, uint32_t levels, uint32_t now_us);
void spi_capture_init(void);
void lambda_capture_callback(void);
void spi_capture_snapshot(display_snapshot_t *snapshot);
bool spi_capture_wait(uint32_t after_sequence, uint32_t timeout_us,
                      display_snapshot_t *snapshot);

