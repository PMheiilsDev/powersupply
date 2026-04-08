#pragma once

#include <stdbool.h>
#include <inttypes.h>

#define PIN_VOLTAGE_A
#define PIN_VOLTAGE_B
#define PIN_VOLTAGE_BUTTON

#define PIN_CURRENT_A
#define PIN_CURRENT_B
#define PIN_CURRENT_BUTTON



void controller_init();


/// @brief activate hardware to rotate the number once
/// @param up true for up
void rotate_digit_voltage_once_hard(bool up);


/// @brief rotate the selected digit by n and track the expected change  
/// @param up true for up
/// @param n 
void rotate_digit_voltage(bool up, uint16_t n);


/// @brief change the selection digit of voltage once to the left and track the expected change  
void change_digit_voltage();


