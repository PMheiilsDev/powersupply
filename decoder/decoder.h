#pragma once

#include <inttypes.h>
// #include "spi_capture.h"

typedef enum ROW
{
    VOLTAGE = 0,
    CURRENT,
    POWER,
    ROW_LEN
} 
row_t;

typedef enum DIGIT 
{
    DIGIT_0 = 0,    // leftmost
    DIGIT_1,        // left middle
    DIGIT_2,        // right middle
    DIGIT_3,        // rightmost
    DIGIT_LEN
} 
digit_t;

typedef enum SEGMENT
{
    SEGMENT_A = 0,
    SEGMENT_B,
    SEGMENT_C,
    SEGMENT_D,
    SEGMENT_E,
    SEGMENT_F,
    SEGMENT_G,
    SEGMENT_DP,
    SEGMENT_LEN
} 
segment_t;

typedef struct 
{
    uint8_t byte;
    uint8_t bit;
} 
bit_position_t;

#define NUM_TO_BIT_POS_T(n)  { .byte = (n)/8, .bit = (n)%8 }

extern bit_position_t data_mapping[ROW_LEN][DIGIT_LEN][SEGMENT_LEN];

typedef enum BCD_TO_SEGMENT
{
    SEG_A = (1u << SEGMENT_A),
    SEG_B = (1u << SEGMENT_B),
    SEG_C = (1u << SEGMENT_C),
    SEG_D = (1u << SEGMENT_D),
    SEG_E = (1u << SEGMENT_E),
    SEG_F = (1u << SEGMENT_F),
    SEG_G = (1u << SEGMENT_G),
    SEG_DP = (1u << SEGMENT_DP),
}
bcd_to_segment_t;


typedef struct
{
    // all values are given in std SI units (V,A,W) 
    // where value = fac * 10**exp;  
    uint16_t fac;
    int8_t exp;
} value_t;


extern const uint8_t segment_mapping[];


/// @brief 
/// @param data 
/// @param row 
/// @param digit 
/// @return the number 0..9 of the digit in the row 
uint8_t get_digit_of_segment(uint8_t *data, row_t row, digit_t digit );


uint8_t get_value(uint8_t*data, value_t* value_ptr, row_t row);


