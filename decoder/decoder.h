#pragma once

#include <inttypes.h>
#include "decoder_types.h"

#define NUM_TO_BIT_POS_T(n)  { .byte = (n)/8, .bit = (n)%8 }

extern bit_position_t data_mapping[ROW_LEN][DIGIT_LEN][SEGMENT_LEN];


extern const uint8_t segment_mapping[];

extern screendata_t screendata;


/// @brief 
/// @param data 
/// @param row 
/// @param digit 
/// @return the number 0..9 of the digit in the row 
uint8_t get_digit_of_segment(uint8_t *data, row_t row, digit_t digit );


uint8_t get_value(uint8_t*data, value_t* value_ptr, row_t row);


