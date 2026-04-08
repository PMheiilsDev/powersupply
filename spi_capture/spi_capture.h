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


// declaration must EXACTLY match definition
void spi_capture_blocking(spi_frame_t *frame);

/// @brief read spi data and update screendata
void update_screendata_callback();

