#include "calibration.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include <string.h>

extern char __flash_binary_end;
_Static_assert(sizeof(calibration_record_t) <= FLASH_PAGE_SIZE, "Calibration record exceeds one page");
_Static_assert(PICO_FLASH_SIZE_BYTES >= 2 * FLASH_SECTOR_SIZE, "Flash is too small");

static bool storage_offset(unsigned slot, uint32_t *offset)
{
    uint32_t start = PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE;
    if (slot > 1 || (uintptr_t)&__flash_binary_end > XIP_BASE + start)
        return false;
    *offset = start + slot * FLASH_SECTOR_SIZE;
    return true;
}

bool calibration_storage_read(unsigned slot, calibration_record_t *record)
{
    uint32_t offset;
    if (!storage_offset(slot, &offset))
        return false;
    memcpy(record, (const void *)(XIP_BASE + offset), sizeof(*record));
    return true;
}

typedef struct
{
    uint32_t offset;
    uint8_t page[FLASH_PAGE_SIZE];
} flash_write_t;

static void write_record(void *context)
{
    flash_write_t *write = context;
    flash_range_erase(write->offset, FLASH_SECTOR_SIZE);
    flash_range_program(write->offset, write->page, FLASH_PAGE_SIZE);
}

bool calibration_storage_write(unsigned slot, const calibration_record_t *record)
{
    flash_write_t write;
    if (!storage_offset(slot, &write.offset))
        return false;
    memset(write.page, 0xFF, sizeof(write.page));
    memcpy(write.page, record, sizeof(*record));
    return flash_safe_execute(write_record, &write, 1000) == PICO_OK;
}