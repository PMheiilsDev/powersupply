#pragma once
#include "calibration.h"
#include "decoder_types.h"

typedef enum
{
    CAL_JOB_IDLE, CAL_JOB_PENDING, CAL_JOB_RUNNING,
    CAL_JOB_DONE, CAL_JOB_FAILED, CAL_JOB_ABORTED
} calibration_job_state_t;

typedef struct
{
    calibration_job_state_t state;
    const char *phase;
    const char *error;
    row_t row;
    uint32_t lower_us;
    uint32_t upper_us;
    unsigned trials;
} calibration_job_status_t;

bool calibration_job_start(bool loads_disconnected);
bool calibration_job_busy(void);
void calibration_job_abort(void);
void calibration_job_process(void);
void calibration_job_set_service(void (*service)(void));
void calibration_job_status(calibration_job_status_t *status);
bool calibration_bisect(uint32_t *lower, uint32_t *upper, uint32_t resolution,
                        bool increasing, int (*trial)(uint32_t, void *), void *context);