#include "calibration_job.h"
#include "controller.h"
#include "pico/stdlib.h"
#include <stdlib.h>
#include <string.h>

#define RESET_US 22000000u
#define PROBE_PHASE_US 10000u
#define PROBE_PRESS_US 200000u
#define PROBE_RELEASE_US 100000u

static calibration_job_status_t status = {CAL_JOB_IDLE, "IDLE", "", UNKNOWN_ROW, 0, 0, 0};
static calibration_config_t previous;
static bool aborted;
static int emergency_output_pin = -1;
static int probing_button_pin = -1;
static void (*service_callback)(void);

static bool fail(const char *error)
{
    if (!status.error[0])
        status.error = error;
    return false;
}

static int trial_error(const char *error)
{
    (void)fail(error);
    return -1;
}

bool calibration_job_busy(void)
{
    return status.state == CAL_JOB_PENDING || status.state == CAL_JOB_RUNNING;
}

void calibration_job_set_service(void (*service)(void)) { service_callback = service; }
void calibration_job_status(calibration_job_status_t *result) { *result = status; }
void calibration_job_abort(void) { if (calibration_job_busy()) aborted = true; }

static bool alive(bool allow_output)
{
    if (service_callback)
        service_callback();
    if (aborted)
        return fail("ABORTED_TARGETS_MAY_HAVE_CHANGED");
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!snapshot.sequence || time_us_32() - snapshot.updated_us >= DISPLAY_STALE_US)
        return fail("DISPLAY_FEEDBACK_LOST");
    if (snapshot.screen.output_on && !allow_output)
    {
        if (emergency_output_pin >= 0)
            controller_raw_press((unsigned)emergency_output_pin, PROBE_PRESS_US, PROBE_RELEASE_US);
        return fail("UNEXPECTED_OUTPUT_ON_CHECK_PANEL");
    }
    return true;
}

static bool wait_us(uint32_t duration, bool allow_output)
{
    uint32_t started = time_us_32();
    while (time_us_32() - started < duration)
    {
        if (!alive(allow_output))
            return false;
        sleep_ms(1);
    }
    return alive(allow_output);
}

static bool wait_since(uint32_t started, uint32_t duration)
{
    uint32_t elapsed = time_us_32() - started;
    return elapsed >= duration ? alive(false) : wait_us(duration - elapsed, false);
}

static bool learn_bus(void)
{
    status.phase = "SCAN_AND_DISPLAY_CADENCE";
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    uint32_t before = snapshot.sequence;
    spi_capture_reset_timing();
    if (!wait_us(1000000, false))
        return false;
    spi_capture_snapshot(&snapshot);
    display_timing_t *timing = &snapshot.timing;
    if (snapshot.sequence - before < 3 || timing->scan_frames < 3 ||
        !timing->scan_min_us || !timing->display_min_us)
        return fail("NO_0X42_SCAN_OR_FULL_DISPLAY_PERIOD");
    if (timing->scan_max_us > timing->scan_min_us * 3 / 2 ||
        timing->display_max_us > timing->display_min_us * 3 / 2)
        return fail("BUS_CADENCE_TOO_IRREGULAR");
    return calibration_set(CAL_SCAN_US, timing->scan_max_us) &&
           calibration_set(CAL_REFRESH_US, timing->display_max_us) &&
           calibration_set(CAL_RESOLUTION_US, timing->scan_max_us > 10000 ? timing->scan_max_us : 10000);
}

static bool observe(row_t row, uint32_t after, bool editing, digit_t expected,
                    display_snapshot_t *snapshot)
{
    uint32_t started = time_us_32();
    if (!wait_us(calibration_get(CAL_REFRESH_US) * 2, false))
        return false;
    do
    {
        if (!alive(false))
            return false;
        spi_capture_snapshot(snapshot);
        if (editing)
        {
            if ((int32_t)(snapshot->editing_sequence[row] - after) > 0 &&
                (expected == UNKNOWN_DIGIT || snapshot->editing_digit[row] == expected))
                return true;
        }
        else if (snapshot->row_valid[row] &&
                 (int32_t)(snapshot->row_sequence[row] - after) > 0)
            return true;
        sleep_ms(1);
    } while (time_us_32() - started < calibration_get(CAL_FEEDBACK_US));
    return fail("NO_FRESH_ROW_OR_EDIT_DIGIT");
}

static bool prepare(row_t row, digit_t digit, display_snapshot_t *snapshot)
{
    controller_press(row);
    spi_capture_snapshot(snapshot);
    if (!observe(row, snapshot->sequence, true, UNKNOWN_DIGIT, snapshot))
        return false;
    for (unsigned moves = 0; moves < DIGIT_LEN; moves++)
    {
        digit_t current = snapshot->editing_digit[row];
        if (current == digit)
            return observe(row, snapshot->sequence, false, UNKNOWN_DIGIT, snapshot);
        controller_press(row);
        spi_capture_snapshot(snapshot);
        if (!observe(row, snapshot->sequence, true,
                     (digit_t)((current + DIGIT_LEN - 1) % DIGIT_LEN), snapshot))
            return false;
    }
    return fail("DIGIT_SELECTION_NOT_CYCLIC");
}

static void assign_pin(calibration_config_t *config, calibration_field_t field, unsigned pin)
{
    for (unsigned other = CAL_V_A; other <= CAL_OUTPUT_BUTTON; other++)
        if (config->values[other] == pin)
            config->values[other] = config->values[field];
    config->values[field] = pin;
}

static bool discover_buttons(void)
{
    status.phase = "BUTTON_PINS";
    int found[3] = {-1, -1, -1};
    for (unsigned index = 0; index < CALIBRATION_PIN_COUNT; index++)
    {
        if (!wait_us(RESET_US, false))
            return false;
        display_snapshot_t before, after;
        spi_capture_snapshot(&before);
        unsigned pin = calibration_candidate_pins[index];
        probing_button_pin = (int)pin;
        controller_raw_press(pin, PROBE_PRESS_US, PROBE_RELEASE_US);
        if (!wait_us(calibration_get(CAL_FEEDBACK_US), true))
            return false;
        spi_capture_snapshot(&after);
        int role = -1;
        if (after.screen.output_on)
        {
            role = 2;
            emergency_output_pin = (int)pin;
            controller_raw_press(pin, PROBE_PRESS_US, PROBE_RELEASE_US);
            if (!wait_us(calibration_get(CAL_REFRESH_US) * 4, true))
                return false;
            spi_capture_snapshot(&after);
            if (after.screen.output_on)
                return fail("CANNOT_TURN_OUTPUT_BACK_OFF");
        }
        probing_button_pin = -1;
        for (row_t row = VOLTAGE; row <= CURRENT; row++)
            if ((int32_t)(after.editing_sequence[row] - before.sequence) > 0)
            {
                if (role >= 0)
                    return fail("AMBIGUOUS_BUTTON_ROLE");
                role = (int)row;
            }
        if (role >= 0)
        {
            if (found[role] >= 0)
                return fail("DUPLICATE_BUTTON_ROLE");
            found[role] = (int)pin;
        }
    }
    if (found[0] < 0 || found[1] < 0 || found[2] < 0)
        return fail("BUTTON_MAPPING_INCOMPLETE");
    calibration_config_t config;
    calibration_get_config(&config);
    assign_pin(&config, CAL_V_BUTTON, (unsigned)found[0]);
    assign_pin(&config, CAL_I_BUTTON, (unsigned)found[1]);
    assign_pin(&config, CAL_OUTPUT_BUTTON, (unsigned)found[2]);
    config.values[CAL_V_PRESS_US] = config.values[CAL_I_PRESS_US] = PROBE_PRESS_US;
    config.values[CAL_OUTPUT_PRESS_US] = PROBE_PRESS_US;
    config.values[CAL_V_RELEASE_US] = config.values[CAL_I_RELEASE_US] = PROBE_RELEASE_US;
    config.values[CAL_OUTPUT_RELEASE_US] = PROBE_RELEASE_US;
    return calibration_apply(&config);
}

static bool discover_encoders(void)
{
    status.phase = "ENCODER_PAIRS";
    unsigned pins[4];
    unsigned count = 0;
    for (unsigned index = 0; index < CALIBRATION_PIN_COUNT; index++)
    {
        unsigned pin = calibration_candidate_pins[index];
        if (pin != calibration_get(CAL_V_BUTTON) && pin != calibration_get(CAL_I_BUTTON) &&
            pin != calibration_get(CAL_OUTPUT_BUTTON))
            pins[count++] = pin;
    }
    if (count != 4)
        return fail("INVALID_ENCODER_CANDIDATES");
    unsigned mapped[2][2] = {{0}};
    for (row_t row = VOLTAGE; row <= CURRENT; row++)
    {
        status.row = row;
        unsigned matches = 0;
        for (unsigned first = 0; first < count; first++)
            for (unsigned second = first + 1; second < count; second++)
            {
                display_snapshot_t before, after, restored;
                if (!prepare(row, DIGIT_3, &before))
                    return false;
                row_t other = row == VOLTAGE ? CURRENT : VOLTAGE;
                controller_press(other);
                spi_capture_snapshot(&before);
                if (before.screen.rows[row].fac < 4 || before.screen.rows[row].fac > 9995)
                    return fail("MOVE_TARGET_AWAY_FROM_DISPLAY_BOUNDARY");
                for (unsigned pulse = 0; pulse < 2; pulse++)
                    controller_raw_rotate(pins[first], pins[second], true, PROBE_PHASE_US, 20000);
                spi_capture_snapshot(&after);
                if (!observe(row, after.sequence, false, UNKNOWN_DIGIT, &after))
                    return false;
                int change = (int)after.screen.rows[row].fac - before.screen.rows[row].fac;
                for (unsigned pulse = 0; pulse < 2; pulse++)
                    controller_raw_rotate(pins[first], pins[second], false, PROBE_PHASE_US, 20000);
                spi_capture_snapshot(&restored);
                if (!observe(row, restored.sequence, false, UNKNOWN_DIGIT, &restored))
                    return false;
                if (restored.screen.rows[row].fac != before.screen.rows[row].fac ||
                    restored.screen.rows[row].exp != before.screen.rows[row].exp ||
                    restored.screen.rows[other].fac != before.screen.rows[other].fac ||
                    restored.screen.rows[other].exp != before.screen.rows[other].exp)
                    return fail("ENCODER_PROBE_NOT_REVERSIBLE");
                if (abs(change) == 2 && after.screen.rows[other].fac == before.screen.rows[other].fac)
                {
                    mapped[row][0] = change > 0 ? pins[first] : pins[second];
                    mapped[row][1] = change > 0 ? pins[second] : pins[first];
                    matches++;
                }
                else if (change != 0)
                    return fail("AMBIGUOUS_ENCODER_STEP");
            }
        if (matches != 1)
            return fail("ENCODER_MAPPING_NOT_UNIQUE");
    }
    calibration_config_t config;
    calibration_get_config(&config);
    assign_pin(&config, CAL_V_A, mapped[VOLTAGE][0]);
    assign_pin(&config, CAL_V_B, mapped[VOLTAGE][1]);
    assign_pin(&config, CAL_I_A, mapped[CURRENT][0]);
    assign_pin(&config, CAL_I_B, mapped[CURRENT][1]);
    if (config.values[CAL_V_A] != mapped[VOLTAGE][0] ||
        config.values[CAL_V_B] != mapped[VOLTAGE][1])
        return fail("ENCODER_PAIRS_OVERLAP");
    config.values[CAL_V_PHASE_US] = config.values[CAL_I_PHASE_US] = PROBE_PHASE_US;
    config.values[CAL_V_GAP_US] = config.values[CAL_I_GAP_US] = 20000;
    return calibration_apply(&config);
}

bool calibration_bisect(uint32_t *lower, uint32_t *upper, uint32_t resolution,
                        bool increasing, int (*trial)(uint32_t, void *), void *context)
{
    if (!resolution || *lower >= *upper || !trial)
        return false;
    int low_result = trial(*lower, context);
    int high_result = trial(*upper, context);
    if (low_result < 0 || high_result < 0 || low_result != (increasing ? 0 : 1) ||
        high_result != (increasing ? 1 : 0))
        return false;
    while (*upper - *lower > resolution)
    {
        uint32_t middle = *lower + (*upper - *lower) / 2;
        int result = trial(middle, context);
        if (result < 0)
            return false;
        if ((result != 0) == increasing)
            *upper = middle;
        else
            *lower = middle;
    }
    return true;
}

typedef enum
{
    TEST_PHASE, TEST_GAP, TEST_PRESS, TEST_RELEASE,
    TEST_OUTPUT_PRESS, TEST_OUTPUT_RELEASE, TEST_IDLE, TEST_EXIT
} test_kind_t;
typedef struct { row_t row; test_kind_t kind; } trial_context_t;

static bool restore_small_change(row_t row, value_t original)
{
    display_snapshot_t snapshot;
    if (!prepare(row, DIGIT_3, &snapshot))
        return false;
    int difference = (int)snapshot.screen.rows[row].fac - original.fac;
    if (snapshot.screen.rows[row].exp != original.exp || abs(difference) > 4)
        return fail("TRIAL_CHANGED_UNEXPECTED_DIGITS");
    for (int step = 0; step < abs(difference); step++)
        controller_rotate(row, difference < 0);
    spi_capture_snapshot(&snapshot);
    if (!observe(row, snapshot.sequence, false, UNKNOWN_DIGIT, &snapshot))
        return false;
    if (snapshot.screen.rows[row].fac != original.fac)
        return fail("TRIAL_COULD_NOT_RESTORE_TARGET");
    return true;
}

static int single_trial(uint32_t delay, trial_context_t *context)
{
    row_t row = context->row;
    test_kind_t kind = context->kind;
    if (kind == TEST_OUTPUT_PRESS || kind == TEST_OUTPUT_RELEASE)
    {
        if (!wait_us(PROBE_RELEASE_US, false))
            return -1;
        unsigned pin = calibration_get(CAL_OUTPUT_BUTTON);
        unsigned pulses = kind == TEST_OUTPUT_RELEASE ? 2 : 1;
        for (unsigned pulse = 0; pulse < pulses; pulse++)
            controller_raw_press(pin, kind == TEST_OUTPUT_PRESS ? delay : PROBE_PRESS_US,
                                 kind == TEST_OUTPUT_RELEASE ? delay : PROBE_RELEASE_US);
        if (!wait_us(calibration_get(CAL_REFRESH_US) * 4, true))
            return -1;
        display_snapshot_t result;
        spi_capture_snapshot(&result);
        bool passed = kind == TEST_OUTPUT_PRESS ? result.screen.output_on : !result.screen.output_on;
        if (result.screen.output_on)
        {
            controller_raw_press(pin, PROBE_PRESS_US, PROBE_RELEASE_US);
            if (!wait_us(calibration_get(CAL_REFRESH_US) * 4, true))
                return -1;
            spi_capture_snapshot(&result);
            if (result.screen.output_on)
                return trial_error("OUTPUT_TIMING_TEST_COULD_NOT_DISABLE");
        }
        return passed;
    }
    display_snapshot_t before, after;
    if ((kind == TEST_IDLE || kind == TEST_EXIT) && !wait_us(RESET_US, false))
        return -1;
    digit_t initial = kind == TEST_IDLE ? DIGIT_0 :
                      kind == TEST_EXIT ? (digit_t)calibration_get(row == VOLTAGE ?
                          CAL_V_DEFAULT_DIGIT : CAL_I_DEFAULT_DIGIT) : DIGIT_3;
    if (!prepare(row, initial, &before))
        return -1;
    uint32_t phase = calibration_get(row == VOLTAGE ? CAL_V_PHASE_US : CAL_I_PHASE_US);
    uint32_t gap = calibration_get(row == VOLTAGE ? CAL_V_GAP_US : CAL_I_GAP_US);
    uint32_t press = calibration_get(row == VOLTAGE ? CAL_V_PRESS_US : CAL_I_PRESS_US);
    uint32_t release = calibration_get(row == VOLTAGE ? CAL_V_RELEASE_US : CAL_I_RELEASE_US);
    unsigned button = calibration_get(row == VOLTAGE ? CAL_V_BUTTON : CAL_I_BUTTON);
    uint32_t marker = before.sequence;
    if (kind == TEST_EXIT)
    {
        controller_press(row);
        uint32_t anchor = time_us_32() - release;
        if (!wait_since(anchor, delay))
            return -1;
        controller_press(row);
        spi_capture_snapshot(&after);
        if (!observe(row, after.sequence, true, UNKNOWN_DIGIT, &after))
            return -1;
        digit_t retained = (digit_t)((initial + DIGIT_LEN - 2) % DIGIT_LEN);
        if (after.editing_digit[row] == retained)
            return 1;
        return after.editing_digit[row] == initial ? 0 : -1;
    }
    if (kind == TEST_PRESS || kind == TEST_RELEASE)
    {
        for (unsigned pulse = 0; pulse < 2; pulse++)
            controller_raw_press(button, kind == TEST_PRESS ? delay : press,
                                 kind == TEST_RELEASE ? delay : release);
        if (!wait_us(calibration_get(CAL_FEEDBACK_US), false))
            return -1;
        spi_capture_snapshot(&after);
        return (int32_t)(after.editing_sequence[row] - marker) > 0 &&
               after.editing_digit[row] == DIGIT_1;
    }
    if (before.screen.rows[row].fac < 4 || before.screen.rows[row].fac > 9995)
        return trial_error("MOVE_TARGET_AWAY_FROM_DISPLAY_BOUNDARY");
    if (kind == TEST_IDLE)
    {
        controller_press(row);
        uint32_t anchor = time_us_32() - release;
        if (!wait_since(anchor, delay))
            return -1;
        controller_rotate(row, true);
    }
    else
        for (unsigned pulse = 0; pulse < 2; pulse++)
            controller_raw_rotate(calibration_get(row == VOLTAGE ? CAL_V_A : CAL_I_A),
                                  calibration_get(row == VOLTAGE ? CAL_V_B : CAL_I_B), true,
                                  kind == TEST_PHASE ? delay : phase,
                                  kind == TEST_GAP ? delay : gap);
    spi_capture_snapshot(&after);
    if (!observe(row, after.sequence, false, UNKNOWN_DIGIT, &after))
        return -1;
    int change = (int)after.screen.rows[row].fac - before.screen.rows[row].fac;
    bool passed = change == (kind == TEST_IDLE ? 1 : 2);
    if (change < 0 || change > 2 || after.screen.rows[row].exp != before.screen.rows[row].exp)
        return trial_error("NON_MONOTONIC_ROTATION_RESULT");
    if (!restore_small_change(row, before.screen.rows[row]))
        return -1;
    return passed;
}

static int repeated_trial(uint32_t delay, void *context)
{
    status.lower_us = delay;
    int result = -1;
    for (unsigned repeat = 0; repeat < 3; repeat++)
    {
        status.trials++;
        int current = single_trial(delay, context);
        if (current < 0 || (result >= 0 && result != current))
            return trial_error("INCONSISTENT_TRIAL_OR_FEEDBACK");
        result = current;
    }
    return result;
}

static bool search_field(row_t row, test_kind_t kind, calibration_field_t field,
                         uint32_t low, uint32_t high)
{
    status.phase = calibration_field_name(field);
    status.row = row;
    trial_context_t context = {row, kind};
    uint32_t resolution = calibration_get(CAL_RESOLUTION_US);
    if (kind == TEST_PHASE)
        resolution = 100;
    status.lower_us = low;
    status.upper_us = high;
    int minimum_result = kind < TEST_IDLE ? repeated_trial(low, &context) : 0;
    if (minimum_result < 0)
        return false;
    if (minimum_result == 1)
        high = low;
    else if (!calibration_bisect(&low, &high, resolution, kind < TEST_IDLE, repeated_trial, &context))
        return fail("SEARCH_NOT_BRACKETED_OR_UNSTABLE");
    status.lower_us = low;
    status.upper_us = high;
    uint32_t value = kind == TEST_IDLE ? low : high;
    if (kind < TEST_IDLE)
        value += resolution;
    calibration_config_t config;
    calibration_get_config(&config);
    if (kind == TEST_IDLE || kind == TEST_EXIT)
    {
        calibration_field_t bounds = row == VOLTAGE ?
            (kind == TEST_IDLE ? CAL_V_IDLE_LOW_US : CAL_V_EXIT_LOW_US) :
            (kind == TEST_IDLE ? CAL_I_IDLE_LOW_US : CAL_I_EXIT_LOW_US);
        uint32_t probe_us = kind == TEST_IDLE ?
            calibration_get(row == VOLTAGE ? CAL_V_PHASE_US : CAL_I_PHASE_US) * 4 :
            calibration_get(row == VOLTAGE ? CAL_V_PRESS_US : CAL_I_PRESS_US);
        config.values[bounds] = low;
        config.values[bounds + 1] = high + probe_us + calibration_get(CAL_SCAN_US);
        if (kind == TEST_EXIT)
            value = config.values[bounds + 1];
    }
    config.values[field] = value;
    int verified = repeated_trial(value, &context);
    if (verified != (kind == TEST_EXIT ? 0 : 1))
        return fail("OPERATING_TIMING_VERIFICATION_FAILED");
    return calibration_apply(&config) || fail("MEASURED_CONFIGURATION_INVALID");
}

static bool calibrate_row(row_t row)
{
    status.row = row;
    status.phase = "DEFAULT_DIGIT_AND_BLINK";
    if (!wait_us(RESET_US, false))
        return false;
    controller_press(row);
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!observe(row, snapshot.sequence, true, UNKNOWN_DIGIT, &snapshot))
        return false;
    if (!calibration_set(row == VOLTAGE ? CAL_V_DEFAULT_DIGIT : CAL_I_DEFAULT_DIGIT,
                         snapshot.editing_digit[row]))
        return false;
    controller_press(row);
    if (!wait_us(calibration_get(row == VOLTAGE ? CAL_V_IDLE_US : CAL_I_IDLE_US) * 9 / 10, false))
        return false;
    spi_capture_snapshot(&snapshot);
    uint32_t blink = snapshot.timing.blink_period_us[row];
    if (blink < 100000 || blink > 3000000)
        return fail("BLINK_PERIOD_NOT_OBSERVED");
    uint32_t feedback = blink * 2 + calibration_get(CAL_REFRESH_US) * 2;
    if (feedback > calibration_get(CAL_FEEDBACK_US) && !calibration_set(CAL_FEEDBACK_US, feedback))
        return false;
    if (!calibration_set(row == VOLTAGE ? CAL_V_BLINK_US : CAL_I_BLINK_US, blink))
        return false;
    return search_field(row, TEST_PHASE, row == VOLTAGE ? CAL_V_PHASE_US : CAL_I_PHASE_US, 100, 10000) &&
           search_field(row, TEST_GAP, row == VOLTAGE ? CAL_V_GAP_US : CAL_I_GAP_US, 0, 100000) &&
           search_field(row, TEST_PRESS, row == VOLTAGE ? CAL_V_PRESS_US : CAL_I_PRESS_US, 1000, 200000) &&
           search_field(row, TEST_RELEASE, row == VOLTAGE ? CAL_V_RELEASE_US : CAL_I_RELEASE_US, 1000, 100000) &&
           search_field(row, TEST_IDLE, row == VOLTAGE ? CAL_V_IDLE_US : CAL_I_IDLE_US, 500000, 5000000) &&
           search_field(row, TEST_EXIT, row == VOLTAGE ? CAL_V_EXIT_US : CAL_I_EXIT_US, 5000000, 20000000);
}

static int coupling_trial(row_t target, bool exit_test)
{
    row_t other = target == VOLTAGE ? CURRENT : VOLTAGE;
    uint32_t timeout = calibration_get(target == VOLTAGE ?
        (exit_test ? CAL_V_EXIT_US : CAL_V_IDLE_US) :
        (exit_test ? CAL_I_EXIT_US : CAL_I_IDLE_US));
    digit_t initial = exit_test ? (digit_t)calibration_get(target == VOLTAGE ?
        CAL_V_DEFAULT_DIGIT : CAL_I_DEFAULT_DIGIT) : DIGIT_0;
    display_snapshot_t before, after;
    if (!wait_us(RESET_US, false) || !prepare(target, initial, &before))
        return -1;
    controller_press(target);
    if (!wait_us(timeout * 3 / 4, false))
        return -1;
    controller_press(other);
    if (!wait_us(timeout * 3 / 4, false))
        return -1;
    if (exit_test)
    {
        controller_press(target);
        spi_capture_snapshot(&after);
        if (!observe(target, after.sequence, true, UNKNOWN_DIGIT, &after))
            return -1;
        if (after.editing_digit[target] == (digit_t)((initial + DIGIT_LEN - 2) % DIGIT_LEN))
            return 1;
        return after.editing_digit[target] == initial ? 0 : -1;
    }
    controller_rotate(target, true);
    spi_capture_snapshot(&after);
    if (!observe(target, after.sequence, false, UNKNOWN_DIGIT, &after))
        return -1;
    int difference = (int)after.screen.rows[target].fac - before.screen.rows[target].fac;
    if ((difference != 0 && difference != 1) ||
        !restore_small_change(target, before.screen.rows[target]))
        return -1;
    return difference;
}

static bool coupling(void)
{
    status.phase = "CROSS_ROW_TIMERS";
    for (row_t target = VOLTAGE; target <= CURRENT; target++)
    {
        status.row = target;
        int idle_result = -1, exit_result = -1;
        for (unsigned repeat = 0; repeat < 3; repeat++)
        {
            int idle = coupling_trial(target, false);
            int exit = coupling_trial(target, true);
            if (idle < 0 || exit < 0 || (repeat && (idle != idle_result || exit != exit_result)))
                return fail("CROSS_ROW_TIMERS_INCONCLUSIVE");
            idle_result = idle;
            exit_result = exit;
        }
        uint32_t result = !idle_result && !exit_result ? 1 : idle_result && exit_result ? 2 :
                          idle_result ? 3 : 4;
        if (!calibration_set(target == VOLTAGE ? CAL_I_TO_V_COUPLING : CAL_V_TO_I_COUPLING, result))
            return false;
    }
    return true;
}

bool calibration_job_start(bool loads_disconnected)
{
    if (!loads_disconnected || calibration_job_busy())
        return false;
    display_snapshot_t snapshot;
    spi_capture_snapshot(&snapshot);
    if (!snapshot.sequence || snapshot.screen.output_on ||
        time_us_32() - snapshot.updated_us >= DISPLAY_STALE_US ||
        !snapshot.row_valid[VOLTAGE] || !snapshot.row_valid[CURRENT] ||
        time_us_32() - snapshot.row_updated_us[VOLTAGE] >= DISPLAY_STALE_US ||
        time_us_32() - snapshot.row_updated_us[CURRENT] >= DISPLAY_STALE_US)
        return false;
    calibration_get_config(&previous);
    calibration_config_t probe;
    calibration_defaults(&probe);
    if (!calibration_apply(&probe))
        return false;
    status = (calibration_job_status_t){CAL_JOB_PENDING, "PENDING", "", UNKNOWN_ROW, 0, 0, 0};
    aborted = false;
    emergency_output_pin = -1;
    probing_button_pin = -1;
    return true;
}

void calibration_job_process(void)
{
    if (status.state != CAL_JOB_PENDING)
        return;
    status.state = CAL_JOB_RUNNING;
    bool success = alive(false) && learn_bus() && discover_buttons() && discover_encoders() &&
                   calibrate_row(VOLTAGE) && calibrate_row(CURRENT) && coupling() &&
                   search_field(VOLTAGE, TEST_OUTPUT_PRESS, CAL_OUTPUT_PRESS_US, 1000, 200000) &&
                   search_field(VOLTAGE, TEST_OUTPUT_RELEASE, CAL_OUTPUT_RELEASE_US, 1000, 100000);
    if (success)
    {
        status.phase = "SAVE";
        success = alive(false) && calibration_set(CAL_TESTED_MASK, 0x7F) && calibration_save();
        if (!success)
            fail("FLASH_SAVE_FAILED");
    }
    display_snapshot_t final;
    spi_capture_snapshot(&final);
    if (final.screen.output_on)
    {
        int pin = emergency_output_pin >= 0 ? emergency_output_pin : probing_button_pin;
        if (pin >= 0)
        {
            controller_raw_press((unsigned)pin, PROBE_PRESS_US, PROBE_RELEASE_US);
            sleep_ms(250);
            spi_capture_snapshot(&final);
        }
        if (final.screen.output_on || time_us_32() - final.updated_us >= DISPLAY_STALE_US)
        {
            status.error = "OUTPUT_STATE_UNCONFIRMED_CHECK_PANEL";
            success = false;
        }
    }
    if (!success)
        (void)calibration_apply(&previous);
    controller_init();
    status.state = aborted ? CAL_JOB_ABORTED : success ? CAL_JOB_DONE : CAL_JOB_FAILED;
    status.phase = success ? "DONE" : "STOPPED";
    if (!success && !status.error[0])
        status.error = "CALIBRATION_FAILED_CHECK_PANEL";
}