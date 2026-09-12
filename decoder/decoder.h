#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include "decoder_types.h"

#define NUM_TO_BIT_POS_T(n)  { .byte = (n)/8, .bit = (n)%8 }

extern bit_position_t data_mapping[ROW_LEN][DIGIT_LEN][SEGMENT_LEN];
extern bit_position_t output_on_bit_pos;


extern const uint8_t segment_mapping[];

extern screendata_t screendata;


/// @brief get the digit of a segment 
/// @param data the binary data array
/// @param row the row_t of the wanted digit 
/// @param digit the digit_t of the wanted digit 
/// @return the number 0..9 of the digit in the row 
uint8_t get_digit_of_segment(uint8_t* data, row_t row, digit_t digit );


/// @brief get the value of row 
/// @param data the binary data array
/// @param value_ptr pointer to value_t where the result is copied to 
/// @param row the row_t of which row the value is used 
/// @return false if any digit is invalid and value was not touched 
bool get_value(uint8_t* data, value_t* value_ptr, row_t row);


