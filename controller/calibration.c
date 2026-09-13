#include "calibration.h"
#include <ctype.h>
#include <string.h>

#define CALIBRATION_MAGIC 0x5043414Cu
#define CALIBRATION_VERSION 1u

const unsigned calibration_candidate_pins[CALIBRATION_PIN_COUNT] = {16, 17, 18, 19, 20, 26, 27};

static const struct
{
    const char *name;
    uint32_t initial;
    uint32_t minimum;
    uint32_t maximum;
} fields[] = {
    {"V_A", 19, 0, 29}, {"V_B", 20, 0, 29}, {"V_BUTTON", 17, 0, 29},
    {"I_A", 27, 0, 29}, {"I_B", 26, 0, 29}, {"I_BUTTON", 18, 0, 29},
    {"OUTPUT_BUTTON", 16, 0, 29},
    {"V_PHASE_US", 1250, 100, 100000}, {"I_PHASE_US", 1250, 100, 100000},
    {"V_GAP_US", 10000, 0, 500000}, {"I_GAP_US", 10000, 0, 500000},
    {"V_PRESS_US", 61000, 1000, 500000}, {"I_PRESS_US", 61000, 1000, 500000},
    {"OUTPUT_PRESS_US", 61000, 1000, 500000},
    {"V_RELEASE_US", 11000, 1000, 500000}, {"I_RELEASE_US", 11000, 1000, 500000},
    {"OUTPUT_RELEASE_US", 11000, 1000, 500000},
    {"V_IDLE_US", 2000000, 100000, 5000000}, {"I_IDLE_US", 2000000, 100000, 5000000},
    {"V_EXIT_US", 9000000, 1000000, 20000000}, {"I_EXIT_US", 9000000, 1000000, 20000000},
    {"V_BLINK_US", 1000000, 100000, 3000000}, {"I_BLINK_US", 1000000, 100000, 3000000},
    {"V_DEFAULT_DIGIT", 2, 0, 3}, {"I_DEFAULT_DIGIT", 2, 0, 3},
    {"SCAN_US", 10000, 100, 500000}, {"REFRESH_US", 50000, 1000, 1000000},
    {"FEEDBACK_US", 2500000, 100000, 10000000},
    {"V_TO_I_COUPLING", 0, 0, 4}, {"I_TO_V_COUPLING", 0, 0, 4},
    {"RESOLUTION_US", 10000, 1000, 50000}, {"TESTED_MASK", 0, 0, UINT32_MAX},
    {"V_IDLE_LOW_US", 0, 0, 21000000}, {"V_IDLE_HIGH_US", 0, 0, 21000000},
    {"I_IDLE_LOW_US", 0, 0, 21000000}, {"I_IDLE_HIGH_US", 0, 0, 21000000},
    {"V_EXIT_LOW_US", 0, 0, 21000000}, {"V_EXIT_HIGH_US", 0, 0, 21000000},
    {"I_EXIT_LOW_US", 0, 0, 21000000}, {"I_EXIT_HIGH_US", 0, 0, 21000000}
};
_Static_assert(sizeof(fields) / sizeof(fields[0]) == CAL_FIELD_COUNT, "Calibration fields do not match schema");

static calibration_config_t active;
static calibration_config_t saved;
static bool initialized;
static bool loaded;
static bool dirty;
static uint32_t generation;
static int saved_slot = -1;

void calibration_defaults(calibration_config_t *config)
{
    for (unsigned field = 0; field < CAL_FIELD_COUNT; field++)
        config->values[field] = fields[field].initial;
}

bool calibration_valid(const calibration_config_t *config)
{
    for (unsigned field = 0; field < CAL_FIELD_COUNT; field++)
        if (config->values[field] < fields[field].minimum ||
            config->values[field] > fields[field].maximum)
            return false;
    for (unsigned field = CAL_V_A; field <= CAL_OUTPUT_BUTTON; field++)
    {
        uint32_t pin = config->values[field];
        bool allowed = false;
        for (unsigned index = 0; index < CALIBRATION_PIN_COUNT; index++)
            allowed |= pin == calibration_candidate_pins[index];
        if (!allowed)
            return false;
        for (unsigned other = CAL_V_A; other < field; other++)
            if (config->values[other] == pin)
                return false;
    }
    for (unsigned field = CAL_V_IDLE_LOW_US; field < CAL_FIELD_COUNT; field += 2)
        if (config->values[field] > config->values[field + 1])
            return false;
    if (config->values[CAL_V_PHASE_US] * 4 + config->values[CAL_V_GAP_US] >= config->values[CAL_V_IDLE_US] ||
        config->values[CAL_I_PHASE_US] * 4 + config->values[CAL_I_GAP_US] >= config->values[CAL_I_IDLE_US])
        return false;
    return config->values[CAL_V_IDLE_US] < config->values[CAL_V_EXIT_US] &&
           config->values[CAL_I_IDLE_US] < config->values[CAL_I_EXIT_US] &&
           config->values[CAL_FEEDBACK_US] >= config->values[CAL_V_BLINK_US] * 2 &&
           config->values[CAL_FEEDBACK_US] >= config->values[CAL_I_BLINK_US] * 2;
}

const char *calibration_field_name(calibration_field_t field)
{
    return field < CAL_FIELD_COUNT ? fields[field].name : "";
}

int calibration_find_field(const char *name, size_t length)
{
    for (unsigned field = 0; field < CAL_FIELD_COUNT; field++)
    {
        if (strlen(fields[field].name) != length)
            continue;
        size_t index = 0;
        while (index < length && toupper((unsigned char)name[index]) == fields[field].name[index])
            index++;
        if (index == length)
            return (int)field;
    }
    return -1;
}

static void ensure_initialized(void)
{
    if (!initialized)
    {
        calibration_defaults(&active);
        saved = active;
        initialized = true;
    }
}

uint32_t calibration_get(calibration_field_t field)
{
    ensure_initialized();
    return field < CAL_FIELD_COUNT ? active.values[field] : 0;
}

void calibration_get_config(calibration_config_t *config)
{
    ensure_initialized();
    *config = active;
}

bool calibration_apply(const calibration_config_t *config)
{
    if (!calibration_valid(config))
        return false;
    ensure_initialized();
    if (memcmp(&active, config, sizeof(active)))
    {
        active = *config;
        dirty = memcmp(&active, &saved, sizeof(active)) != 0;
    }
    return true;
}

bool calibration_set(calibration_field_t field, uint32_t value)
{
    if (field >= CAL_FIELD_COUNT)
        return false;
    calibration_config_t candidate;
    calibration_get_config(&candidate);
    if (field <= CAL_OUTPUT_BUTTON)
    {
        for (unsigned other = CAL_V_A; other <= CAL_OUTPUT_BUTTON; other++)
            if (candidate.values[other] == value)
                candidate.values[other] = candidate.values[field];
    }
    candidate.values[field] = value;
    return calibration_apply(&candidate);
}

static uint32_t record_crc(const calibration_record_t *record)
{
    const unsigned char *bytes = (const unsigned char *)record;
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < offsetof(calibration_record_t, crc); index++)
    {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void calibration_record_encode(calibration_record_t *record,
                               const calibration_config_t *config, uint32_t next_generation)
{
    record->magic = CALIBRATION_MAGIC;
    record->version = CALIBRATION_VERSION;
    record->generation = next_generation;
    record->config = *config;
    record->crc = record_crc(record);
}

bool calibration_record_valid(const calibration_record_t *record)
{
    return record->magic == CALIBRATION_MAGIC && record->version == CALIBRATION_VERSION &&
           record->crc == record_crc(record) && calibration_valid(&record->config);
}

int calibration_record_select(const calibration_record_t records[2])
{
    bool first = calibration_record_valid(&records[0]);
    bool second = calibration_record_valid(&records[1]);
    if (!first)
        return second ? 1 : -1;
    if (!second)
        return 0;
    return (int32_t)(records[1].generation - records[0].generation) > 0 ? 1 : 0;
}

bool calibration_load(void)
{
    calibration_record_t records[2] = {0};
    for (unsigned slot = 0; slot < 2; slot++)
        if (!calibration_storage_read(slot, &records[slot]))
            memset(&records[slot], 0, sizeof(records[slot]));
    int selected = calibration_record_select(records);
    if (selected < 0)
        return false;
    active = records[selected].config;
    saved = active;
    generation = records[selected].generation;
    saved_slot = selected;
    initialized = loaded = true;
    dirty = false;
    return true;
}

bool calibration_save(void)
{
    ensure_initialized();
    if (loaded && !dirty)
        return true;
    unsigned slot = saved_slot == 0 ? 1 : 0;
    calibration_record_t record;
    calibration_record_encode(&record, &active, generation + 1);
    if (!calibration_storage_write(slot, &record))
        return false;
    calibration_record_t verification;
    if (!calibration_storage_read(slot, &verification) ||
        memcmp(&verification, &record, sizeof(record)))
        return false;
    generation = record.generation;
    saved = active;
    saved_slot = (int)slot;
    loaded = true;
    dirty = false;
    return true;
}

bool calibration_loaded(void) { return loaded; }
bool calibration_dirty(void) { return dirty; }

void calibration_init(void)
{
    calibration_defaults(&active);
    saved = active;
    initialized = true;
    loaded = dirty = false;
    generation = 0;
    saved_slot = -1;
    (void)calibration_load();
}