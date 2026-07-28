#pragma once

#include <stdbool.h>
#include <inttypes.h>

#include "decoder_types.h"
#include "decoder.h"

#define PIN_VOLTAGE_A 19 
#define PIN_VOLTAGE_B 20 
#define PIN_VOLTAGE_BUTTON 17


#define PIN_CURRENT_A 27
#define PIN_CURRENT_B 26 
#define PIN_CURRENT_BUTTON 18

#define PIN_OUTPUT_BUTTON 16

#define ROT_DELAY_US (2000-750)

// a button must atleast be pressed for 5 button scans (50ms) to be detected 
// to be sure that 5 scans are in the on time it must be atleast 6*10ms 
// todo: this could be reduced by syncing it with the cs signal minimum is still 50ms 
    // could be made to be in core1 
        // if cs == low => press button count to 5, stop */ 
#define BUTTON_DELAY_US 61*1000  

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


/// @brief move the digit to digit by pressing the button, waiting for screen update before doing anything 
/// @param digit 
void move_digit_voltage(digit_t digit);

