/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
// Host_MSC_FatFS.ino — USB MSC file explorer with self-contained FatFS
// (ArduinoTinyUSB multi-board).
// Upstream: TinyUSB examples/host/msc_file_explorer + ChaN FatFS R0.15
// (ff.c/ff.h/ffconf.h/diskio.* live in this directory, no external library).
// Wiring: Due/Zero use the native port as host. Use a self-powered drive (or a
// powered hub); bus-powered gear will not enumerate.
// ARDUINO_TINYUSB_CONSOLE is the CLI: type help + ENTER for commands.
//
// Boards: Due/Zero. NOT Giga: the Mbed core already
// defines the ChaN disk_* symbols (FATFileSystem.cpp), which collide with
// the self-contained diskio.c here.
#include "ArduinoTinyUSB.h"
#include "ff.h"
#include "diskio.h"

static FATFS fatfs[FF_VOLUMES];
static FIL file1;
static FIL file2;
static uint8_t rwBuf[1024];  // 1 KB chunks (small-SRAM budget; dd speed only)
static bool driveMounted[FF_VOLUMES];
static scsi_inquiry_resp_t inquiryResp;
static scsi_read_capacity10_resp_t capResp;
static char cmdLine[128];
static uint8_t cmdLen = 0;
static uint32_t lastTick = 0;
static void probeNextLun(uint8_t dev_addr, uint8_t lun);
// Deferred initial scan: the card takes seconds to init after bus reset;
// probing immediately sees "no medium" on every LUN. `rescan` re-probes.
static uint32_t scanAt = 0;
static uint8_t scanDev = 0;
static uint8_t autoRescans = 0;

// Volume -> (dev_addr, lun) mapping. Multi-LUN readers (e.g. empty LUN0
// slot + media on LUN1) mount each ready LUN as its own volume.
uint8_t r4msc_volDev[FF_VOLUMES];
uint8_t r4msc_volLun[FF_VOLUMES];

static void printPrompt()
{
  char cwd[64];
  if (f_getcwd(cwd, sizeof(cwd)) == FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.print(cwd);
  }
  ARDUINO_TINYUSB_CONSOLE.print("> ");
}

static void cmdLs(char *args)
{
  char const *dpath = ".";
  if (args[0])
  {
    dpath = args;
  }
  DIR dir;
  if (f_opendir(&dir, dpath) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("ls: no such directory");
    return;
  }
  FILINFO fno;
  while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0])
  {
    if (fno.fname[0] == '.')
    {
      continue;
    }
    char line[96];
    if (fno.fattrib & AM_DIR)
    {
      snprintf(line, sizeof(line), "/%s", fno.fname);
    }
    else if (fno.fsize < 1024)
    {
      snprintf(line, sizeof(line), "%-40s%lu B", fno.fname, (unsigned long)fno.fsize);
    }
    else
    {
      snprintf(line, sizeof(line), "%-40s%lu KB", fno.fname, (unsigned long)(fno.fsize / 1024));
    }
    ARDUINO_TINYUSB_CONSOLE.println(line);
  }
  f_closedir(&dir);
}

static void cmdCat(char *args)
{
  if (!args[0])
  {
    ARDUINO_TINYUSB_CONSOLE.println("usage: cat FILE");
    return;
  }
  if (f_open(&file1, args, FA_READ) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("cat: no such file");
    return;
  }
  UINT count = 0;
  while (f_read(&file1, rwBuf, sizeof(rwBuf), &count) == FR_OK && count > 0)
  {
    for (UINT i = 0; i < count; i++)
    {
      uint8_t ch = rwBuf[i];
      ARDUINO_TINYUSB_CONSOLE.write(isprint(ch) || ch == '\n' || ch == '\r' || ch == '\t' ? ch : '.');
    }
  }
  f_close(&file1);
  ARDUINO_TINYUSB_CONSOLE.println();
}

static void cmdCd(char *args)
{
  if (!args[0] || f_chdir(args) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("cd: no such directory");
    return;
  }
}

static void cmdPwd()
{
  char path[128];
  if (f_getcwd(path, sizeof(path)) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("pwd: failed");
    return;
  }
  ARDUINO_TINYUSB_CONSOLE.println(path);
}

static void cmdMkdir(char *args)
{
  if (!args[0] || f_mkdir(args) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("mkdir: failed");
    return;
  }
}

static void cmdRm(char *args)
{
  if (!args[0] || f_unlink(args) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("rm: no such file");
    return;
  }
}

static void cmdMv(char *src, char *dst)
{
  if (!src || !dst || f_rename(src, dst) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("usage: mv SOURCE DEST");
    return;
  }
}

static void cmdCp(char *src, char *dst)
{
  if (!src || !dst)
  {
    ARDUINO_TINYUSB_CONSOLE.println("usage: cp SOURCE DEST");
    return;
  }
  if (f_open(&file1, src, FA_READ) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("cp: no such source");
    return;
  }
  if (f_open(&file2, dst, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("cp: cannot create dest");
    f_close(&file1);
    return;
  }
  UINT rd = 0;
  while (f_read(&file1, rwBuf, sizeof(rwBuf), &rd) == FR_OK && rd > 0)
  {
    UINT wr = 0;
    if (f_write(&file2, rwBuf, rd, &wr) != FR_OK || wr != rd)
    {
      ARDUINO_TINYUSB_CONSOLE.println("cp: write failed");
      break;
    }
  }
  f_close(&file1);
  f_close(&file2);
}

static void cmdDd(char *args)
{
  uint8_t pdrv = 0xFF;
  for (uint8_t i = 0; i < FF_VOLUMES; i++)
  {
    if (driveMounted[i])
    {
      pdrv = i;
      break;
    }
  }
  if (pdrv == 0xFF)
  {
    ARDUINO_TINYUSB_CONSOLE.println("dd: no drive mounted");
    return;
  }
  uint32_t count = args[0] ? (uint32_t)atoi(args) : 1024;
  if (!count)
  {
    count = 1024;
  }
  DWORD sectors = 0;
  if (disk_ioctl(pdrv, GET_SECTOR_COUNT, &sectors) != RES_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("dd: ioctl failed");
    return;
  }
  if (count > sectors)
  {
    count = sectors;
  }
  uint16_t perXfer = sizeof(rwBuf) / 512;
  char line[96];
  snprintf(line, sizeof(line), "dd: reading %lu sectors ...", (unsigned long)count);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  uint32_t start = millis();
  for (uint32_t i = 0; i < count; i += perXfer)
  {
    uint16_t n = (count - i < perXfer) ? (uint16_t)(count - i) : perXfer;
    if (disk_read(pdrv, rwBuf, i, n) != RES_OK)
    {
      ARDUINO_TINYUSB_CONSOLE.println("dd: read failed");
      return;
    }
  }
  uint32_t ms = millis() - start;
  if (!ms)
  {
    ms = 1;
  }
  snprintf(line, sizeof(line), "dd: %lu KB in %lu ms = %lu KB/s",
    (unsigned long)(count / 2), (unsigned long)ms, (unsigned long)((count * 1000) / (2 * ms)));
  ARDUINO_TINYUSB_CONSOLE.println(line);
}

static void cmdInfo()
{
  for (uint8_t i = 0; i < FF_VOLUMES; i++)
  {
    if (!driveMounted[i])
    {
      continue;
    }
    uint8_t dev = r4msc_volDev[i];
    uint8_t lun = r4msc_volLun[i];
    uint32_t bc = tuh_msc_get_block_count(dev, lun);
    uint32_t bs = tuh_msc_get_block_size(dev, lun);
    char line[96];
    snprintf(line, sizeof(line), "drv %u: dev=%u lun=%u %lu x %lu B = %lu MB", i, dev, lun,
      (unsigned long)bc, (unsigned long)bs, (unsigned long)(bc / ((1024 * 1024) / bs)));
    ARDUINO_TINYUSB_CONSOLE.println(line);
  }
}

static void runCmd(char *line)
{
  while (*line == ' ')
  {
    line++;
  }
  char *sp = line;
  while (*sp && *sp != ' ')
  {
    sp++;
  }
  char *args = (char *)"";
  if (*sp)
  {
    *sp = 0;
    args = sp + 1;
    while (*args == ' ')
    {
      args++;
    }
  }
  if (!strcmp(line, "help"))
  {
    ARDUINO_TINYUSB_CONSOLE.println("ls [DIR] | cat FILE | cd DIR | pwd | mkdir DIR | rm FILE");
    ARDUINO_TINYUSB_CONSOLE.println("mv SRC DST | cp SRC DST | dd [COUNT] | info | rescan");
  }
  else if (!strcmp(line, "ls"))
  {
    cmdLs(args);
  }
  else if (!strcmp(line, "cat"))
  {
    cmdCat(args);
  }
  else if (!strcmp(line, "cd"))
  {
    cmdCd(args);
  }
  else if (!strcmp(line, "pwd"))
  {
    cmdPwd();
  }
  else if (!strcmp(line, "mkdir"))
  {
    cmdMkdir(args);
  }
  else if (!strcmp(line, "rm"))
  {
    cmdRm(args);
  }
  else if (!strcmp(line, "mv"))
  {
    char *dst = args;
    while (*dst && *dst != ' ')
    {
      dst++;
    }
    if (*dst)
    {
      *dst = 0;
      cmdMv(args, dst + 1);
    }
    else
    {
      ARDUINO_TINYUSB_CONSOLE.println("usage: mv SOURCE DEST");
    }
  }
  else if (!strcmp(line, "cp"))
  {
    char *dst = args;
    while (*dst && *dst != ' ')
    {
      dst++;
    }
    if (*dst)
    {
      *dst = 0;
      cmdCp(args, dst + 1);
    }
    else
    {
      ARDUINO_TINYUSB_CONSOLE.println("usage: cp SOURCE DEST");
    }
  }
  else if (!strcmp(line, "dd"))
  {
    cmdDd(args);
  }
  else if (!strcmp(line, "info"))
  {
    cmdInfo();
  }
  else if (!strcmp(line, "rescan"))
  {
    for (uint8_t dev = 1; dev <= 4; dev++)
    {
      if (tuh_msc_mounted(dev))
      {
        probeNextLun(dev, 0);
      }
    }
  }
  else if (line[0])
  {
    ARDUINO_TINYUSB_CONSOLE.println("unknown cmd, type help");
  }
  printPrompt();
}

static void pollConsole()
{
  while (ARDUINO_TINYUSB_CONSOLE.available())
  {
    char c = (char)ARDUINO_TINYUSB_CONSOLE.read();
    if (c == '\r' || c == '\n')
    {
      ARDUINO_TINYUSB_CONSOLE.println();
      cmdLine[cmdLen] = 0;
      cmdLen = 0;
      runCmd(cmdLine);
    }
    else if (c == 8 || c == 127)
    {
      if (cmdLen)
      {
        cmdLen--;
      }
    }
    else if (cmdLen < sizeof(cmdLine) - 1 && c >= 32)
    {
      cmdLine[cmdLen++] = c;
      ARDUINO_TINYUSB_CONSOLE.write(c);
    }
  }
}

static void probeNextLun(uint8_t dev_addr, uint8_t lun);

static bool capCb(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data)
{
  uint8_t lun = cb_data->cbw->lun;
  if (cb_data->csw->status != 0)
  {
    char line[64];
    snprintf(line, sizeof(line), "[MSC] dev=%u lun=%u no capacity", dev_addr, lun);
    ARDUINO_TINYUSB_CONSOLE.println(line);
    probeNextLun(dev_addr, (uint8_t)(lun + 1));
    return true;
  }
  // Capacity is cached driver-side by the config flow; readability proven.
  if (!tuh_msc_inquiry(dev_addr, lun, &inquiryResp, inquiryCb, 0))
  {
    probeNextLun(dev_addr, (uint8_t)(lun + 1));
  }
  return true;
}

static bool probeTurCb(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data)
{
  uint8_t lun = cb_data->cbw->lun;
  if (cb_data->csw->status == 0)
  {
    if (!tuh_msc_read_capacity(dev_addr, lun, &capResp, capCb, 0))
    {
      probeNextLun(dev_addr, (uint8_t)(lun + 1));
    }
  }
  else
  {
    char line[64];
    snprintf(line, sizeof(line), "[MSC] dev=%u lun=%u no medium", dev_addr, lun);
    ARDUINO_TINYUSB_CONSOLE.println(line);
    probeNextLun(dev_addr, (uint8_t)(lun + 1));
  }
  return true;
}

static void probeNextLun(uint8_t dev_addr, uint8_t lun)
{
  if (lun >= tuh_msc_get_maxlun(dev_addr))
  {
    uint8_t vols = 0;
    for (uint8_t i = 0; i < FF_VOLUMES; i++)
    {
      if (driveMounted[i] && r4msc_volDev[i] == dev_addr)
      {
        vols++;
      }
    }
    if (!vols && autoRescans < 2)
    {
      autoRescans++;
      scanDev = dev_addr;
      scanAt = millis() + 8000;
      ARDUINO_TINYUSB_CONSOLE.println("[MSC] no volumes, auto re-scan in 8 s");
    }
    else
    {
      ARDUINO_TINYUSB_CONSOLE.println("[MSC] LUN scan done");
    }
    printPrompt();
    return;
  }
  if (!tuh_msc_test_unit_ready(dev_addr, lun, probeTurCb, 0))
  {
    probeNextLun(dev_addr, (uint8_t)(lun + 1));
  }
}

static bool inquiryCb(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data)
{
  uint8_t lun = cb_data->cbw->lun;
  if (cb_data->csw->status != 0)
  {
    ARDUINO_TINYUSB_CONSOLE.println("[MSC] inquiry failed");
    probeNextLun(dev_addr, (uint8_t)(lun + 1));
    return true;
  }
  for (uint8_t i = 0; i < FF_VOLUMES; i++)
  {
    if (driveMounted[i] && r4msc_volDev[i] == dev_addr && r4msc_volLun[i] == lun)
    {
      probeNextLun(dev_addr, (uint8_t)(lun + 1));
      return true;
    }
  }
  int8_t vol = -1;
  for (uint8_t i = 0; i < FF_VOLUMES; i++)
  {
    if (!driveMounted[i])
    {
      vol = (int8_t)i;
      break;
    }
  }
  if (vol < 0)
  {
    ARDUINO_TINYUSB_CONSOLE.println("[MSC] no free volume");
    return true;
  }
  char line[128];
  snprintf(line, sizeof(line), "[MSC] %.8s %.16s %.4s (dev=%u lun=%u %lu x %lu B)", inquiryResp.vendor_id,
    inquiryResp.product_id, inquiryResp.product_rev, dev_addr, lun,
    (unsigned long)tuh_msc_get_block_count(dev_addr, lun),
    (unsigned long)tuh_msc_get_block_size(dev_addr, lun));
  ARDUINO_TINYUSB_CONSOLE.println(line);
  char drivePath[3] = "0:";
  drivePath[0] += (char)vol;
  r4msc_volDev[vol] = dev_addr;
  r4msc_volLun[vol] = lun;
  if (f_mount(&fatfs[vol], drivePath, 1) != FR_OK)
  {
    ARDUINO_TINYUSB_CONSOLE.println("[MSC] mount failed");
    r4msc_volDev[vol] = 0;
    probeNextLun(dev_addr, (uint8_t)(lun + 1));
    return true;
  }
  driveMounted[vol] = true;
  if (vol == 0)
  {
    f_chdrive(drivePath);
    if (f_chdir("/") != FR_OK)
    {
      ARDUINO_TINYUSB_CONSOLE.println("[MSC] chdir failed");
    }
  }
  snprintf(line, sizeof(line), "[MSC] drive %s mounted (dev=%u lun=%u)", drivePath, dev_addr, lun);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  probeNextLun(dev_addr, (uint8_t)(lun + 1));
  return true;
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);
  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { /* wait for console */ }
  ARDUINO_TINYUSB_CONSOLE.println("\n### ArduinoTinyUSB MSC file explorer ###");
  for (uint8_t i = 0; i < FF_VOLUMES; i++)
  {
    driveMounted[i] = false;
    r4msc_volDev[i] = 0;
    r4msc_volLun[i] = 0;
  }
  tuh_arduino_init();
  printPrompt();
}

void loop()
{
  tuh_arduino_poll();
  pollConsole();
  if (scanAt && (int32_t)(millis() - scanAt) >= 0)
  {
    scanAt = 0;
    probeNextLun(scanDev, 0);
  }
  if (millis() - lastTick > 2000)
  {
    lastTick = millis();
    bool any = false;
    for (uint8_t i = 0; i < FF_VOLUMES; i++)
    {
      any = any || driveMounted[i];
    }
    digitalWrite(LED_BUILTIN, any ? HIGH : (millis() / 500 & 1));
  }
}

extern "C" {
void msc_poll(void)
{
  tuh_arduino_poll();
}
void tuh_mount_cb(uint8_t daddr)
{
  char line[64];
  snprintf(line, sizeof(line), "[MSC] device %u mounted", daddr);
  ARDUINO_TINYUSB_CONSOLE.println(line);
}
void tuh_umount_cb(uint8_t daddr)
{
  char line[64];
  snprintf(line, sizeof(line), "[MSC] device %u unmounted", daddr);
  ARDUINO_TINYUSB_CONSOLE.println(line);
}
void tuh_msc_mount_cb(uint8_t dev_addr)
{
  char line[64];
  snprintf(line, sizeof(line), "[MSC] mass-storage dev=%u mounted, maxlun=%u", dev_addr,
    tuh_msc_get_maxlun(dev_addr));
  ARDUINO_TINYUSB_CONSOLE.println(line);
  scanDev = dev_addr;
  scanAt = millis() + 3000;
  autoRescans = 0;
  ARDUINO_TINYUSB_CONSOLE.println("[MSC] LUN scan in 3 s (card init); `rescan` re-probes");
}
void tuh_msc_umount_cb(uint8_t dev_addr)
{
  ARDUINO_TINYUSB_CONSOLE.println("[MSC] mass-storage unmounted");
  scanAt = 0;
  for (uint8_t i = 0; i < FF_VOLUMES; i++)
  {
    if (driveMounted[i] && r4msc_volDev[i] == dev_addr)
    {
      char drivePath[3] = "0:";
      drivePath[0] += (char)i;
      f_unmount(drivePath);
      driveMounted[i] = false;
      r4msc_volDev[i] = 0;
    }
  }
}
}
