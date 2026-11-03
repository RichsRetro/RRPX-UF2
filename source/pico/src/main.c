#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "puff.h"
#include "card_index.h"

extern const uint8_t card_blob[];
#define SECTOR_SIZE 512u
#define OVERLAY_SECTORS 192u
static uint8_t decoded[CARD_BLOCK_SIZE];
static uint32_t cached_block = UINT32_MAX;
// Accept metadata writes without wearing flash or changing the original image.
// All writes disappear when unplugged. This is an exploit carrier, not a save card.
static uint8_t overlay[OVERLAY_SECTORS][SECTOR_SIZE];
static uint32_t overlay_lba[OVERLAY_SECTORS];
static uint32_t overlay_used;
static bool fault;

static int find_overlay(uint32_t lba) {
    for (uint32_t i = 0; i < overlay_used; ++i)
        if (overlay_lba[i] == lba) return (int)i;
    return -1;
}

static bool base_read(uint32_t address, uint8_t *out, uint32_t length) {
    while (length) {
        uint32_t block = address / CARD_BLOCK_SIZE;
        uint32_t offset = address % CARD_BLOCK_SIZE;
        uint32_t count = CARD_BLOCK_SIZE - offset;
        if (count > length) count = length;
        if (cached_block != block) {
            unsigned long dst_len = CARD_BLOCK_SIZE;
            unsigned long src_len = card_offsets[block+1] - card_offsets[block];
            unsigned long expected = src_len;
            cached_block = UINT32_MAX;
            int result = puff(decoded, &dst_len, card_blob + card_offsets[block], &src_len);
            if (result || dst_len != CARD_BLOCK_SIZE || src_len != expected) {
                fault = true; return false;
            }
            cached_block = block;
        }
        memcpy(out, decoded + offset, count);
        address += count; out += count; length -= count;
    }
    return true;
}

static bool get_range(uint8_t lun, uint32_t lba, uint32_t offset,
                      uint32_t length, uint32_t *address) {
    uint64_t start = (uint64_t)lba * SECTOR_SIZE + offset;
    if (lun || start > CARD_SIZE || length > CARD_SIZE - start) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0);
        return false;
    }
    *address = (uint32_t)start;
    return true;
}

uint8_t tud_msc_get_maxlun_cb(void) { return 1; } // TinyUSB expects LUN count.
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) {
    (void)lun;
    memcpy(vendor, "PICO    ", 8);
    memcpy(product, "ENDGAME CARD    ", 16);
    memcpy(revision, "0.1 ", 4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) { return lun == 0 && !fault; }
void tud_msc_capacity_cb(uint8_t lun, uint32_t *blocks, uint16_t *size) {
    *blocks = lun == 0 ? CARD_SIZE / SECTOR_SIZE : 0;
    *size = SECTOR_SIZE;
}
bool tud_msc_is_writable_cb(uint8_t lun) { return lun == 0; }
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power, bool start, bool eject) {
    (void)power; (void)start; (void)eject; return lun == 0;
}
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                         void *buffer, uint32_t length) {
    uint32_t address;
    if (!get_range(lun, lba, offset, length, &address)) return -1;
    uint8_t *out = buffer;
    uint32_t remaining = length;
    while (remaining) {
        uint32_t sector = address / SECTOR_SIZE, within = address % SECTOR_SIZE;
        uint32_t count = SECTOR_SIZE - within;
        if (count > remaining) count = remaining;
        int slot = find_overlay(sector);
        if (slot >= 0) memcpy(out, overlay[slot] + within, count);
        else if (!base_read(address, out, count)) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0); return -1;
        }
        address += count; out += count; remaining -= count;
    }
    return (int32_t)length;
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          uint8_t *buffer, uint32_t length) {
    uint32_t address;
    if (!get_range(lun, lba, offset, length, &address)) return -1;
    // Preflight capacity so a callback never partially writes on exhaustion.
    uint32_t needed = 0;
    if (length) {
        uint32_t last = (address + length - 1) / SECTOR_SIZE;
        for (uint32_t s = address / SECTOR_SIZE; s <= last; ++s)
            if (find_overlay(s) < 0) ++needed;
    }
    if (needed > OVERLAY_SECTORS - overlay_used) {
        tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0); return -1;
    }
    uint32_t remaining = length;
    while (remaining) {
        uint32_t sector = address / SECTOR_SIZE, within = address % SECTOR_SIZE;
        uint32_t count = SECTOR_SIZE - within;
        if (count > remaining) count = remaining;
        int slot = find_overlay(sector);
        if (slot < 0) {
            slot = (int)overlay_used;
            if (!base_read(sector * SECTOR_SIZE, overlay[slot], SECTOR_SIZE)) return -1;
            overlay_lba[overlay_used++] = sector;
        }
        memcpy(overlay[slot] + within, buffer, count);
        address += count; buffer += count; remaining -= count;
    }
    return (int32_t)length;
}
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const command[16], void *buffer, uint16_t size) {
    (void)buffer; (void)size;
    if (command[0] == 0x1e || command[0] == 0x35) return 0;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0); return -1;
}

int main(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    tusb_init();
    while (true) {
        tud_task();
        uint32_t ms = to_ms_since_boot(get_absolute_time());
        gpio_put(PICO_DEFAULT_LED_PIN, fault ? ((ms / 100) & 1) :
                 tud_mounted() ? 1 : ((ms / 500) & 1));
    }
}
