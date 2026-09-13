#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CALIBRATION_PIN_COUNT 7
extern const unsigned calibration_candidate_pins[CALIBRATION_PIN_COUNT];

typedef enum
{
    CAL_V_A, CAL_V_B, CAL_V_BUTTON,
    CAL_I_A, CAL_I_B, CAL_I_BUTTON, CAL_OUTPUT_BUTTON,
    CAL_V_PHASE_US, CAL_I_PHASE_US,
    CAL_V_GAP_US, CAL_I_GAP_US,
    CAL_V_PRESS_US, CAL_I_PRESS_US, CAL_OUTPUT_PRESS_US,
    CAL_V_RELEASE_US, CAL_I_RELEASE_US,
    CAL_OUTPUT_RELEASE_US,
    CAL_V_IDLE_US, CAL_I_IDLE_US,
    CAL_V_EXIT_US, CAL_I_EXIT_US,
    CAL_V_BLINK_US, CAL_I_BLINK_US,
    CAL_V_DEFAULT_DIGIT, CAL_I_DEFAULT_DIGIT,
    CAL_SCAN_US, CAL_REFRESH_US, CAL_FEEDBACK_US,
    CAL_V_TO_I_COUPLING, CAL_I_TO_V_COUPLING,
    CAL_RESOLUTION_US, CAL_TESTED_MASK,
    CAL_V_IDLE_LOW_US, CAL_V_IDLE_HIGH_US, CAL_I_IDLE_LOW_US, CAL_I_IDLE_HIGH_US,
    CAL_V_EXIT_LOW_US, CAL_V_EXIT_HIGH_US, CAL_I_EXIT_LOW_US, CAL_I_EXIT_HIGH_US,
    CAL_FIELD_COUNT
} calibration_field_t;

typedef struct
{
    uint32_t values[CAL_FIELD_COUNT];
} calibration_config_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    calibration_config_t config;
    uint32_t crc;
} calibration_record_t;

void calibration_defaults(calibration_config_t *config);
bool calibration_valid(const calibration_config_t *config);
const char *calibration_field_name(calibration_field_t field);
int calibration_find_field(const char *name, size_t length);
uint32_t calibration_get(calibration_field_t field);
void calibration_get_config(calibration_config_t *config);
bool calibration_apply(const calibration_config_t *config);
bool calibration_set(calibration_field_t field, uint32_t value);
void calibration_record_encode(calibration_record_t *record,
                               const calibration_config_t *config, uint32_t generation);
bool calibration_record_valid(const calibration_record_t *record);
int calibration_record_select(const calibration_record_t records[2]);
bool calibration_load(void);
bool calibration_save(void);
bool calibration_loaded(void);
bool calibration_dirty(void);
void calibration_init(void);

bool calibration_storage_read(unsigned slot, calibration_record_t *record);
bool calibration_storage_write(unsigned slot, const calibration_record_t *record);