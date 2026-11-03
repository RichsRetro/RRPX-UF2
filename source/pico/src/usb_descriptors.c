#include "tusb.h"

// Standard SCSI/Bulk-Only storage interface, as supported by compatible USB sticks.
// This is not a complete emulation of the proprietary Microsoft XMU subclass.
static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0110, .bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xcafe, .idProduct = 0x4010, .bcdDevice = 0x0100,
    .iManufacturer = 0, .iProduct = 0, .iSerialNumber = 0,
    .bNumConfigurations = 1
};
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(0, 0, 0x02, 0x81, 64)
};
uint8_t const *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index; return configuration;
}
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)index; (void)langid; return NULL;
}
