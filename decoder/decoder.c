
#include "decoder.h"
#include "spi_capture.h"
#include <math.h>


bit_position_t data_mapping[ROW_LEN][DIGIT_LEN][SEGMENT_LEN] =
{
    [VOLTAGE] =
    {
        [DIGIT_0] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x03),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x13),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x23),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x33),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x43),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x53),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x63),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x73),
        },
        [DIGIT_1] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x02),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x12),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x22),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x32),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x42),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x52),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x62),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x72),
        },
        [DIGIT_2] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x01),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x11),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x21),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x31),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x41),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x51),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x61),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x71),
        },
        [DIGIT_3] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x00),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x10),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x20),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x30),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x40),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x50),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x60),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x70),
        },
    },
    [CURRENT] =
    {
        [DIGIT_0] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x0A),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x1A),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x2A),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x3A),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x4A),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x5A),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x6A),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x7A),
        },
        [DIGIT_1] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x0B),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x1B),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x2B),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x3B),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x4B),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x5B),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x6B),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x7B),
        },
        [DIGIT_2] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x0C),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x1C),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x2C),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x3C),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x4C),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x5C),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x6C),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x7C),
        },
        [DIGIT_3] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x09),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x19),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x29),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x39),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x49),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x59),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x69),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x79),
        },
    },
    [POWER] =
    {
        [DIGIT_0] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x06),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x16),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x26),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x36),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x46),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x56),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x66),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x76),
        },
        [DIGIT_1] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x07),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x17),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x27),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x37),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x47),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x57),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x67),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x77),
        },
        [DIGIT_2] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x08),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x18),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x28),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x38),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x48),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x58),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x68),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x78),
        },
        [DIGIT_3] =
        {
            [SEGMENT_E] = NUM_TO_BIT_POS_T(0x05),
            [SEGMENT_A] = NUM_TO_BIT_POS_T(0x15),
            [SEGMENT_F] = NUM_TO_BIT_POS_T(0x25),
            [SEGMENT_B] = NUM_TO_BIT_POS_T(0x35),
            [SEGMENT_G] = NUM_TO_BIT_POS_T(0x45),
            [SEGMENT_C] = NUM_TO_BIT_POS_T(0x55),
            [SEGMENT_DP] = NUM_TO_BIT_POS_T(0x65),
            [SEGMENT_D] = NUM_TO_BIT_POS_T(0x75),
        },
    },
};

const uint8_t segment_mapping[] = 
{
    [0] = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
    [1] = SEG_B | SEG_C,
    [2] = SEG_A | SEG_B | SEG_G | SEG_E | SEG_D,
    [3] = SEG_A | SEG_B | SEG_G | SEG_C | SEG_D,
    [4] = SEG_F | SEG_G | SEG_B | SEG_C,
    [5] = SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
    [6] = SEG_A | SEG_F | SEG_G | SEG_E | SEG_C | SEG_D,
    [7] = SEG_A | SEG_B | SEG_C,
    [8] = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    [9] = SEG_A | SEG_F | SEG_G | SEG_B | SEG_C | SEG_D,
};

screendata_t screendata;


uint8_t get_digit_of_segment(uint8_t *data, row_t row, digit_t digit )
{
    // get all the bits of this segment to a byte
    uint8_t segment_bits = 0;
    for ( segment_t seg = SEGMENT_A; seg < SEGMENT_LEN; seg++ )
    {
        bit_position_t bit_pos = data_mapping[row][digit][seg];
        uint8_t bit_value = (data[bit_pos.byte] >> bit_pos.bit) & 0x01;
        if ( bit_value )
        {
            segment_bits |= (1u << seg);
        }
    }

    for ( uint8_t i = 0; i < sizeof(segment_mapping); i++ )
    {
        if ( (segment_bits & (uint8_t)(~SEG_DP)) == segment_mapping[i] )
        {
            return i;
        }
    }
    return 0xFF; // Return an invalid digit if no match is found
}


uint8_t get_value(uint8_t*data, value_t* value_ptr, row_t row)
{
    uint16_t voltage_fac = 0;
    int8_t decimal_point_pos = 0;


    for (uint8_t digit = 0; digit < DIGIT_LEN; digit++) 
    {
        uint8_t digit_value = get_digit_of_segment(data, row, (digit_t)digit);
        
        if (digit_value != 0xFF)
        {
            voltage_fac += digit_value * pow((uint16_t)10, (uint16_t)((DIGIT_LEN-1)-digit) );
        }

        // get decimal seperator 
        bit_position_t bit_pos = data_mapping[row][digit][SEGMENT_DP];
        uint8_t bit_value = (data[bit_pos.byte] >> bit_pos.bit) & 0x01;

        if ( bit_value )
        {
            decimal_point_pos = digit;
        }

    }

    screendata.rows[row].fac = voltage_fac;

    screendata.rows[row].exp = decimal_point_pos+(1-DIGIT_LEN);
    
}


