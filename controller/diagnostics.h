#pragma once
#include "controller.h"

#define DIAGNOSTIC_QUEUE_SIZE 16u

typedef struct
{
    uint32_t id;
    uint32_t delay_us;
    uint32_t pulse_us;
    uint32_t gap_us;
    uint8_t kind;
    uint8_t row;
    bool up;
} diagnostic_action_t;

typedef struct
{
    unsigned arm_mode;
    unsigned queued;
    uint32_t completed_id;
    uint32_t ended_us;
    unsigned error;
    bool running;
} diagnostic_status_t;

bool diagnostics_arm(unsigned mode);
bool diagnostics_armed(void);
bool diagnostics_busy(void);
bool diagnostics_enqueue(const diagnostic_action_t *action);
bool diagnostics_run(void);
void diagnostics_contact(void);
void diagnostics_process(void);
void diagnostics_status(diagnostic_status_t *status);