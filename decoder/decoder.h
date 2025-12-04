
#include <inttypes.h>

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
    DIGIT_0 = 0,
    DIGIT_1,
    DIGIT_2,
    DIGIT_3,
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
