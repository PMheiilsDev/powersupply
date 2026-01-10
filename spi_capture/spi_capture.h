#pragma once
#include <stdint.h>
#include "decoder.h"

#define PIN_DIO  16
#define PIN_CLK  19
#define PIN_CS   17

#define CAPTURE_BUF_SIZE 256

typedef struct 
{
    uint8_t data[CAPTURE_BUF_SIZE];
    uint32_t length;
} spi_frame_t;

typedef struct
{
    uint16_t voltage;
    uint8_t current;
    uint8_t power;

    row_t editing_row;
    digit_t editing_digit;
}
screendata_t;

extern screendata_t screendata;

// declaration must EXACTLY match definition
void spi_capture_blocking(spi_frame_t *frame);

/// @brief read spi data and update screendata
void update_screendata_callback();


