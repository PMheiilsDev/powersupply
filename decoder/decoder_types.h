#pragma once

#include <inttypes.h>

typedef enum ROW
{
    VOLTAGE = 0,
    CURRENT,
    POWER,
    ROW_LEN,
    UNKNOWN_ROW = 0xFF
} 
row_t;

typedef enum DIGIT 
{
    DIGIT_0 = 0,    // leftmost
    DIGIT_1,        // left middle
    DIGIT_2,        // right middle
    DIGIT_3,        // rightmost
    DIGIT_LEN,
    UNKNOWN_DIGIT = 0xFF
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
} 
value_t;



typedef struct
{
    value_t rows[ROW_LEN];

    // uint8_t current;
    // uint8_t power;

    row_t editing_row;
    digit_t editing_digit; // todo add editing_digit for voltage and current as they are independent and information persists when V->I->V 
}
screendata_t;

