// usb_descriptors.cpp — TinyUSB MSC descriptors (two-tab pattern).
//
// The Arduino .ino prototype generator mangles extern "C" callbacks,
// so descriptors go in a separate .cpp tab.
//
// RAM disk: 16 KB (32 blocks x 512 bytes), SCSI inquiry identifies
// the device as an Arduino-TinyUSB RAM disk.

#include <ArduinoTinyUSB.h>

// ─── RAM Disk Geometry ───
#define BLOCK_SIZE  512
#define BLOCK_COUNT 32

enum { ITF_NUM_MSC = 0, ITF_NUM_TOTAL };

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static uint8_t const desc_fs_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
  // MSC interface: 0x01 OUT, 0x82 IN, 64-byte endpoints
  // NOTE: EP1 and EP2 are separate physical endpoints on SAM3X (phys=logical).
  // Using the same EP number with different directions requires runtime EPDIR
  // flipping which SAM3X DEVEPTCFG silently ignores.  Two physical endpoints
  // avoids the problem entirely.
  TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 4, 0x01, 0x82, 64),
};

static char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },  // 0: English
  "Arduino-TinyUSB",              // 1: Manufacturer
  "TinyUSB MSC",                  // 2: Product
  "123456",                       // 3: Serial
  "TinyUSB MSC",                  // 4: MSC interface
};

static uint16_t _desc_str[32];

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x2341,
    .idProduct = 0x8004,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

extern "C" uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_fs_configuration;
}

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  uint8_t chr_count;
  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
    const char *str = string_desc_arr[index];
    chr_count = (uint8_t) strlen(str);
    if (chr_count > 31) chr_count = 31;
    for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
  }
  _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
  return _desc_str;
}

// ─── MSC Callbacks ───

// External RAM disk buffer (defined in the .ino sketch)
extern uint8_t disk[];
#define DISK_SIZE (BLOCK_SIZE * BLOCK_COUNT)

// SCSI inquiry: identify the disk to the host
extern "C" void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                                   uint8_t product_id[16], uint8_t product_rev[4]) {
  (void)lun;
  const char vid[] = "Arduino";
  const char pid[] = "TinyUSB RAM Disk";
  const char rev[] = "1.0";
  memset(vendor_id, ' ', 8);
  memset(product_id, ' ', 16);
  memset(product_rev, ' ', 4);
  memcpy(vendor_id, vid, strlen(vid));
  memcpy(product_id, pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

// Test Unit Ready: always report disk is ready
extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  (void)lun;
  return true;
}

// Report disk capacity
extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                                    uint16_t *block_size) {
  (void)lun;
  *block_count = BLOCK_COUNT;
  *block_size  = BLOCK_SIZE;
}

// Read blocks from RAM disk into host buffer
extern "C" int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                     void *buffer, uint32_t bufsize) {
  (void)lun;
  uint32_t addr = lba * BLOCK_SIZE + offset;
  if (addr + bufsize > DISK_SIZE) return -1;
  memcpy(buffer, disk + addr, bufsize);
  return (int32_t) bufsize;
}

// Write blocks from host buffer into RAM disk
extern "C" int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                      uint8_t *buffer, uint32_t bufsize) {
  (void)lun;
  uint32_t addr = lba * BLOCK_SIZE + offset;
  if (addr + bufsize > DISK_SIZE) return -1;
  memcpy(disk + addr, buffer, bufsize);
  return (int32_t) bufsize;
}

// SCSI command handler for unrecognized commands
extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                                   void *buffer, uint16_t bufsize) {
  (void)lun; (void)scsi_cmd; (void)buffer; (void)bufsize;
  return -1;
}
