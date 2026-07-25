#pragma once

#include <stdbool.h>
#include <inttypes.h>

#define PIN_VOLTAGE_A 27
#define PIN_VOLTAGE_B 26 
#define PIN_VOLTAGE_BUTTON 18

#define PIN_CURRENT_A 20 
#define PIN_CURRENT_B 19 
#define PIN_CURRENT_BUTTON 17

#define PIN_OUTPUT_BUTTON 16

#define ROT_DELAY_US 100

// this must atleast be longer than the delay between two button checks of the power supply (10ms) 
// this is bad as it takes long maybe try to sync it to the cs pin ? (wait for cs to go high for more than a few ms)
#define BUTTON_DELAY_US 100  

/// @brief initilise controller 
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


