/* diskio.c — FatFs disk I/O glue for ArduinoTinyUSB Host_MSC_FatFS.
 * Maps FatFs disk_* calls to TinyUSB MSC host transfers (512 B sectors).
 * Volume -> (dev_addr, lun) mapping lives in the sketch (r4msc_volDev /
 * r4msc_volLun) so multi-LUN readers mount each ready LUN as its own
 * volume. msc_poll() is provided by the sketch and pumps the host stack
 * (tuh_arduino_poll) while a transfer completes. */

#include <stdint.h>
#include <stdbool.h>
#include "ff.h"
#include "diskio.h"
// NOTE: ArduinoTinyUSB.h is C++-only (bare extern "C" debug decls), so this
// C tab includes the C-safe tusb.h directly, mirroring the header's own
// board selection.
#include "tusb.h"

void msc_poll(void);

extern uint8_t r4msc_volDev[];
extern uint8_t r4msc_volLun[];

static volatile bool diskBusy[FF_VOLUMES];

static bool diskIoComplete(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data)
{
  (void)dev_addr;
  (void)cb_data;
  for (uint8_t i = 0; i < FF_VOLUMES; i++)
  {
    diskBusy[i] = false;
  }
  return true;
}

static bool waitBusy(BYTE pdrv)
{
  uint32_t start = tusb_time_millis_api();
  while (diskBusy[pdrv])
  {
    msc_poll();
    if (tusb_time_millis_api() - start > 5000)
    {
      diskBusy[pdrv] = false;
      return false;
    }
  }
  return true;
}

DSTATUS disk_status(BYTE pdrv)
{
  if (pdrv >= FF_VOLUMES || r4msc_volDev[pdrv] == 0)
  {
    return STA_NODISK;
  }
  return tuh_msc_mounted(r4msc_volDev[pdrv]) ? 0 : STA_NODISK;
}

DSTATUS disk_initialize(BYTE pdrv)
{
  (void)pdrv;
  return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
  if (pdrv >= FF_VOLUMES || r4msc_volDev[pdrv] == 0)
  {
    return RES_PARERR;
  }
  diskBusy[pdrv] = true;
  if (!tuh_msc_read10(r4msc_volDev[pdrv], r4msc_volLun[pdrv], buff, (uint32_t)sector, (uint16_t)count, diskIoComplete, 0))
  {
    diskBusy[pdrv] = false;
    return RES_ERROR;
  }
  return waitBusy(pdrv) ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv, BYTE const *buff, LBA_t sector, UINT count)
{
  if (pdrv >= FF_VOLUMES || r4msc_volDev[pdrv] == 0)
  {
    return RES_PARERR;
  }
  diskBusy[pdrv] = true;
  if (!tuh_msc_write10(r4msc_volDev[pdrv], r4msc_volLun[pdrv], (void *)buff, (uint32_t)sector, (uint16_t)count, diskIoComplete, 0))
  {
    diskBusy[pdrv] = false;
    return RES_ERROR;
  }
  return waitBusy(pdrv) ? RES_OK : RES_ERROR;
}

#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  if (pdrv >= FF_VOLUMES || r4msc_volDev[pdrv] == 0)
  {
    return RES_PARERR;
  }
  uint8_t dev_addr = r4msc_volDev[pdrv];
  uint8_t lun = r4msc_volLun[pdrv];
  switch (cmd)
  {
    case CTRL_SYNC:
      return RES_OK;
    case GET_SECTOR_COUNT:
      *((DWORD *)buff) = (DWORD)tuh_msc_get_block_count(dev_addr, lun);
      return RES_OK;
    case GET_SECTOR_SIZE:
      *((WORD *)buff) = (WORD)tuh_msc_get_block_size(dev_addr, lun);
      return RES_OK;
    case GET_BLOCK_SIZE:
      *((DWORD *)buff) = 1;
      return RES_OK;
    default:
      return RES_PARERR;
  }
}
