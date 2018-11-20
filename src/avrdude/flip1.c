/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2014 Joerg Wunsch
 *
 * This implementation has been cloned from FLIPv2 implementation
 * written by Kirill Levchenko.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* $Id$ */

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#if HAVE_STDINT_H
#include <stdint.h>
#elif HAVE_INTTYPES_H
#include <inttypes.h>
#endif


#include "avrdude.h"
#include "libavrdude.h"

#include "flip1.h"
#include "dfu.h"
#include "usbdevs.h" /* for USB_VENDOR_ATMEL */

/* There are three versions of the FLIP protocol:
 *
 * Version 0: C51 parts
 * Version 1: megaAVR parts ("USB DFU Bootloader Datasheet" [doc7618])
 * Version 2: XMEGA parts (AVR4023 [doc8457])
 *
 * This implementation handles protocol version 1.
 *
 * Protocol version 1 has some, erm, "interesting" features:
 *
 * When contacting the fresh bootloader, the only allowed actions are
 * requesting the configuration/manufacturer information (which is
 * used to read the signature on AVRs), and to issue a "chip erase".
 * All operations on flash and EEPROM are restricted before a chip
 * erase has been seen (security protection).
 *
 * However, after the chip erase, the configuration/manufacturer
 * information can no longer be obtained ... they all respond with
 * 0xff.  Essentially, the device needs a power cycle then, after
 * which the only actual command to access is a chip erase.
 *
 * Quite cumbersome to the user.
 */

/* EXPORTED CONSTANT STRINGS */

const char flip1_desc[] = "FLIP USB DFU protocol version 1 (doc7618)";

/* PRIVATE DATA STRUCTURES */

struct flip1
{
  struct dfu_dev *dfu;
  unsigned char part_sig[3];
  unsigned char part_rev;
  unsigned char boot_ver;
  unsigned char security_mode_flag; /* indicates the user has already
                                     * been hinted about security
                                     * mode */
};

#define FLIP1(pgm) ((struct flip1 *)(pgm->cookie))

/* FLIP1 data structures and constants. */

struct flip1_cmd
{
  unsigned char cmd;
  unsigned char args[5];
};

struct flip1_cmd_header         /* for memory read/write */
{
  unsigned char cmd;
  unsigned char memtype;
  unsigned char start_addr[2];
  unsigned char end_addr[2];
  unsigned char padding[26];
};

struct flip1_prog_footer
{
  unsigned char crc[4];         /* not really used */
  unsigned char ftr_length;     /* 0x10 */
  unsigned char signature[3];   /* "DFU" */
  unsigned char bcdversion[2];  /* 0x01, 0x10 */
  unsigned char vendor[2];      /* or 0xff, 0xff */
  unsigned char product[2];     /* or 0xff, 0xff */
  unsigned char device[2];      /* or 0xff, 0xff */
};

#define FLIP1_CMD_PROG_START 0x01
#define FLIP1_CMD_DISPLAY_DATA 0x03
#define FLIP1_CMD_WRITE_COMMAND 0x04
#define FLIP1_CMD_READ_COMMAND 0x05
#define FLIP1_CMD_CHANGE_BASE_ADDRESS 0x06

/* args[1:0] for FLIP1_CMD_READ_COMMAND */
#define FLIP1_READ_BOOTLOADER_VERSION { 0x00, 0x00 }
#define FLIP1_READ_DEVICE_BOOT_ID1    { 0x00, 0x01 }
#define FLIP1_READ_DEVICE_BOOT_ID2    { 0x00, 0x02 }
#define FLIP1_READ_MANUFACTURER_CODE  { 0x01, 0x30 }
#define FLIP1_READ_FAMILY_CODE        { 0x01, 0x31 }
#define FLIP1_READ_PRODUCT_NAME       { 0x01, 0x60 }
#define FLIP1_READ_PRODUCT_REVISION   { 0x01, 0x61 }

enum flip1_mem_unit {
  FLIP1_MEM_UNIT_FLASH = 0x00,
  FLIP1_MEM_UNIT_EEPROM = 0x01,
  FLIP1_MEM_UNIT_UNKNOWN = -1
};

#define STATE_dfuERROR 10       /* bState; requires a DFU_CLRSTATUS */

#define LONG_DFU_TIMEOUT  10000 /* 10 s for program and erase */

/* EXPORTED PROGRAMMER FUNCTION PROTOTYPES */

static int flip1_open(PROGRAMMER *pgm, char *port_spec);
static int flip1_initialize(PROGRAMMER* pgm, AVRPART *part);
static void flip1_close(PROGRAMMER* pgm);
static void flip1_enable(PROGRAMMER* pgm);
static void flip1_disable(PROGRAMMER* pgm);
static void flip1_display(PROGRAMMER* pgm, const char *prefix);
static int flip1_program_enable(PROGRAMMER* pgm, AVRPART *part);
static int flip1_chip_erase(PROGRAMMER* pgm, AVRPART *part);
static int flip1_read_byte(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned long addr, unsigned char *value);
static int flip1_write_byte(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned long addr, unsigned char value);
static int flip1_paged_load(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned int page_size, unsigned int addr, unsigned int n_bytes);
static int flip1_paged_write(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned int page_size, unsigned int addr, unsigned int n_bytes);
static int flip1_read_sig_bytes(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem);
static void flip1_setup(PROGRAMMER * pgm);
static void flip1_teardown(PROGRAMMER * pgm);

/* INTERNAL PROGRAMMER FUNCTION PROTOTYPES */
#ifdef HAVE_LIBUSB
// The internal ones are made conditional, as they're not defined further down #ifndef HAVE_LIBUSB

static void flip1_show_info(struct flip1 *flip1);

static int flip1_read_memory(PROGRAMMER * pgm,
  enum flip1_mem_unit mem_unit, uint32_t addr, void *ptr, int size);
static int flip1_write_memory(struct dfu_dev *dfu,
  enum flip1_mem_unit mem_unit, uint32_t addr, const void *ptr, int size);

static const char * flip1_status_str(const struct dfu_status *status);
static const char * flip1_mem_unit_str(enum flip1_mem_unit mem_unit);
static int flip1_set_mem_page(struct dfu_dev *dfu, unsigned short page_addr);
static enum flip1_mem_unit flip1_mem_unit(const char *name);

#endif /* HAVE_LIBUSB */

/* THE INITPGM FUNCTION DEFINITIONS */

void flip1_initpgm(PROGRAMMER *pgm)
{
  strcpy(pgm->type, "flip1");

  /* Mandatory Functions */
  pgm->initialize       = flip1_initialize;
  pgm->enable           = flip1_enable;
  pgm->disable          = flip1_disable;
  pgm->display          = flip1_display;
  pgm->program_enable   = flip1_program_enable;
  pgm->chip_erase       = flip1_chip_erase;
  pgm->open             = flip1_open;
  pgm->close            = flip1_close;
  pgm->paged_load       = flip1_paged_load;
  pgm->paged_write      = flip1_paged_write;
  pgm->read_byte        = flip1_read_byte;
  pgm->write_byte       = flip1_write_byte;
  pgm->read_sig_bytes   = flip1_read_sig_bytes;
  pgm->setup            = flip1_setup;
  pgm->teardown         = flip1_teardown;
}

#ifdef HAVE_LIBUSB
/* EXPORTED PROGRAMMER FUNCTION DEFINITIONS */

int flip1_open(PROGRAMMER *pgm, char *port_spec)
{
  FLIP1(pgm)->dfu = dfu_open(port_spec);
  return (FLIP1(pgm)->dfu != NULL) ? 0 : -1;
}

int flip1_initialize(PROGRAMMER* pgm, AVRPART *part)
{
  unsigned short vid, pid;
  int result;
  struct dfu_dev *dfu = FLIP1(pgm)->dfu;

  /* A note about return values. Negative return values from this function are
   * interpreted as failure by main(), from where this function is called.
   * However such failures are interpreted as a device signature check failure
   * and the user is adviced to use the -F option to override this check. In
   * our case, this is misleading, so we defer reporting an error until another
   * function is called. Thus, we always return 0 (success) from initialize().
   * I don't like this, but I don't want to mess with main().
   */

  /* The dfu_init() function will try to find the target part either based on
   * a USB address provided by the user with the -P option or by matching the
   * VID and PID of the device. The VID may be specified in the programmer
   * definition; if not specified, it defaults to USB_VENDOR_ATMEL (defined
   * in usbdevs.h). The PID may be specified either in the programmer
   * definition or the part definition; the programmer definition takes
   * priority. The default PID value is 0, which causes dfu_init() to ignore
   * the PID when matching a target device.
   */

  vid = (pgm->usbvid != 0) ? pgm->usbvid : USB_VENDOR_ATMEL;
  LNODEID usbpid = lfirst(pgm->usbpid);
  if (usbpid) {
    pid = *(int *)(ldata(usbpid));
    if (lnext(usbpid))
      avrdude_message(MSG_INFO, "%s: Warning: using PID 0x%04x, ignoring remaining PIDs in list\n",
                      progname, pid);
  } else {
    pid = part->usbpid;
  }
  if (!ovsigck && (part->flags & AVRPART_HAS_PDI)) {
    avrdude_message(MSG_INFO, "%s: \"flip1\" (FLIP protocol version 1) is for AT90USB* and ATmega*U* devices.\n"
                    "%s For Xmega devices, use \"flip2\".\n"
                    "%s (Use -F to bypass this check.)\n",
                    progname, progbuf, progbuf);
    return -1;
  }

  result = dfu_init(FLIP1(pgm)->dfu, vid, pid);

  if (result != 0)
    goto flip1_initialize_fail;

  /* Check if descriptor values are what we expect. */

  if (dfu->dev_desc.idVendor != vid)
    avrdude_message(MSG_INFO, "%s: Warning: USB idVendor = 0x%04X (expected 0x%04X)\n",
      progname, dfu->dev_desc.idVendor, vid);

  if (pid != 0 && dfu->dev_desc.idProduct != pid)
    avrdude_message(MSG_INFO, "%s: Warning: USB idProduct = 0x%04X (expected 0x%04X)\n",
      progname, dfu->dev_desc.idProduct, pid);

  if (dfu->dev_desc.bNumConfigurations != 1)
    avrdude_message(MSG_INFO, "%s: Warning: USB bNumConfigurations = %d (expected 1)\n",
      progname, (int) dfu->dev_desc.bNumConfigurations);

  if (dfu->conf_desc.bNumInterfaces != 1)
    avrdude_message(MSG_INFO, "%s: Warning: USB bNumInterfaces = %d (expected 1)\n",
      progname, (int) dfu->conf_desc.bNumInterfaces);

  if (dfu->dev_desc.bDeviceClass != 254)
    avrdude_message(MSG_INFO, "%s: Warning: USB bDeviceClass = %d (expected 254)\n",
      progname, (int) dfu->dev_desc.bDeviceClass);

  if (dfu->dev_desc.bDeviceSubClass != 1)
    avrdude_message(MSG_INFO, "%s: Warning: USB bDeviceSubClass = %d (expected 1)\n",
      progname, (int) dfu->dev_desc.bDeviceSubClass);

  if (dfu->dev_desc.bDeviceProtocol != 0)
    avrdude_message(MSG_INFO, "%s: Warning: USB bDeviceProtocol = %d (expected 0)\n",
      progname, (int) dfu->dev_desc.bDeviceProtocol);

  /*
   * doc7618 claims an interface class of FEh and a subclas 01h.
   * However, as of today (2014-01-16), all values in the interface
   * descriptor (except of bLength and bDescriptorType) are actually
   * 0.  So rather don't check these.
   */
  if (0) {
  if (dfu->intf_desc.bInterfaceClass != 254)
    avrdude_message(MSG_INFO, "%s: Warning: USB bInterfaceClass = %d (expected 254)\n",
      progname, (int) dfu->intf_desc.bInterfaceClass);

  if (dfu->intf_desc.bInterfaceSubClass != 1)
    avrdude_message(MSG_INFO, "%s: Warning: USB bInterfaceSubClass = %d (expected 1)\n",
      progname, (int) dfu->intf_desc.bInterfaceSubClass);

  if (dfu->intf_desc.bInterfaceProtocol != 0)
    avrdude_message(MSG_INFO, "%s: Warning: USB bInterfaceSubClass = %d (expected 0)\n",
      progname, (int) dfu->intf_desc.bInterfaceProtocol);
  }

  if (dfu->dev_desc.bMaxPacketSize0 != 32)
    avrdude_message(MSG_INFO, "%s: Warning: bMaxPacketSize0 (%d) != 32, things might go wrong\n",
      progname, dfu->dev_desc.bMaxPacketSize0);

  if (verbose)
    flip1_show_info(FLIP1(pgm));

  dfu_abort(dfu);

  return 0;

flip1_initialize_fail:
  dfu_close(FLIP1(pgm)->dfu);
  FLIP1(pgm)->dfu = NULL;
  return 0;
}

void flip1_close(PROGRAMMER* pgm)
{
  if (FLIP1(pgm)->dfu != NULL) {
    dfu_close(FLIP1(pgm)->dfu);
    FLIP1(pgm)->dfu = NULL;
  }
}

void flip1_enable(PROGRAMMER* pgm)
{
  /* Nothing to do. */
}

void flip1_disable(PROGRAMMER* pgm)
{
  /* Nothing to do. */
}

void flip1_display(PROGRAMMER* pgm, const char *prefix)
{
  /* Nothing to do. */
}

int flip1_program_enable(PROGRAMMER* pgm, AVRPART *part)
{
  /* I couldn't find anything that uses this function, although it is marked
   * as "mandatory" in pgm.c. In case anyone does use it, we'll report an
   * error if we failed to initialize.
   */

  return (FLIP1(pgm)->dfu != NULL) ? 0 : -1;
}

int flip1_chip_erase(PROGRAMMER* pgm, AVRPART *part)
{
  struct dfu_status status;
  int cmd_result = 0;
  int aux_result;
  unsigned int default_timeout = FLIP1(pgm)->dfu->timeout;

  avrdude_message(MSG_NOTICE2, "%s: flip_chip_erase()\n", progname);

  struct flip1_cmd cmd = {
    FLIP1_CMD_WRITE_COMMAND, { 0, 0xff }
  };

  FLIP1(pgm)->dfu->timeout = LONG_DFU_TIMEOUT;
  cmd_result = dfu_dnload(FLIP1(pgm)->dfu, &cmd, 3);
  aux_result = dfu_getstatus(FLIP1(pgm)->dfu, &status);
  FLIP1(pgm)->dfu->timeout = default_timeout;

  if (cmd_result < 0 || aux_result < 0)
    return -1;

  if (status.bStatus != DFU_STATUS_OK) {
    avrdude_message(MSG_INFO, "%s: failed to send chip erase command: %s\n",
            progname, flip1_status_str(&status));
    if (status.bState == STATE_dfuERROR)
      dfu_clrstatus(FLIP1(pgm)->dfu);
    return -1;
  }

  return 0;
}

int flip1_read_byte(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned long addr, unsigned char *value)
{
  enum flip1_mem_unit mem_unit;

  if (FLIP1(pgm)->dfu == NULL)
    return -1;

  if (strcasecmp(mem->desc, "signature") == 0) {
    if (flip1_read_sig_bytes(pgm, part, mem) < 0)
      return -1;
    if (addr > mem->size) {
      avrdude_message(MSG_INFO, "%s: flip1_read_byte(signature): address %lu out of range\n",
              progname, addr);
      return -1;
    }
    *value = mem->buf[addr];
    return 0;
  }

  mem_unit = flip1_mem_unit(mem->desc);

  if (mem_unit == FLIP1_MEM_UNIT_UNKNOWN) {
    avrdude_message(MSG_INFO, "%s: Error: "
      "\"%s\" memory not accessible using FLIP",
      progname, mem->desc);
    avrdude_message(MSG_INFO, "\n");
    return -1;
  }

  if (mem_unit == FLIP1_MEM_UNIT_EEPROM)
    /* 0x01 is used for blank check when reading, 0x02 is EEPROM */
    mem_unit = 2;

  return flip1_read_memory(pgm, mem_unit, addr, value, 1);
}

int flip1_write_byte(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned long addr, unsigned char value)
{
  enum flip1_mem_unit mem_unit;

  if (FLIP1(pgm)->dfu == NULL)
    return -1;

  mem_unit = flip1_mem_unit(mem->desc);

  if (mem_unit == FLIP1_MEM_UNIT_UNKNOWN) {
    avrdude_message(MSG_INFO, "%s: Error: "
      "\"%s\" memory not accessible using FLIP",
      progname, mem->desc);
    avrdude_message(MSG_INFO, "\n");
    return -1;
  }

  return flip1_write_memory(FLIP1(pgm)->dfu, mem_unit, addr, &value, 1);
}

int flip1_paged_load(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned int page_size, unsigned int addr, unsigned int n_bytes)
{
  enum flip1_mem_unit mem_unit;

  if (FLIP1(pgm)->dfu == NULL)
    return -1;

  mem_unit = flip1_mem_unit(mem->desc);

  if (mem_unit == FLIP1_MEM_UNIT_UNKNOWN) {
    avrdude_message(MSG_INFO, "%s: Error: "
      "\"%s\" memory not accessible using FLIP",
      progname, mem->desc);
    avrdude_message(MSG_INFO, "\n");
    return -1;
  }

  if (mem_unit == FLIP1_MEM_UNIT_EEPROM)
    /* 0x01 is used for blank check when reading, 0x02 is EEPROM */
    mem_unit = 2;

  return flip1_read_memory(pgm, mem_unit, addr, mem->buf + addr, n_bytes);
}

int flip1_paged_write(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned int page_size, unsigned int addr, unsigned int n_bytes)
{
  enum flip1_mem_unit mem_unit;
  int result;

  if (FLIP1(pgm)->dfu == NULL)
    return -1;

  mem_unit = flip1_mem_unit(mem->desc);

  if (mem_unit == FLIP1_MEM_UNIT_UNKNOWN) {
    avrdude_message(MSG_INFO, "%s: Error: "
      "\"%s\" memory not accessible using FLIP",
      progname, mem->desc);
    avrdude_message(MSG_INFO, "\n");
    return -1;
  }

  if (n_bytes > INT_MAX) {
    /* This should never happen, unless the int type is only 16 bits. */
    avrdude_message(MSG_INFO, "%s: Error: Attempting to read more than %d bytes\n",
      progname, INT_MAX);
    exit(1);
  }

  result = flip1_write_memory(FLIP1(pgm)->dfu, mem_unit, addr,
    mem->buf + addr, n_bytes);

  return (result == 0) ? n_bytes : -1;
}

int flip1_read_sig_bytes(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem)
{
  avrdude_message(MSG_NOTICE2, "%s: flip1_read_sig_bytes(): ", progname);

  if (FLIP1(pgm)->dfu == NULL)
    return -1;

  if (mem->size < sizeof(FLIP1(pgm)->part_sig)) {
    avrdude_message(MSG_INFO, "%s: Error: Signature read must be at least %u bytes\n",
      progname, (unsigned int) sizeof(FLIP1(pgm)->part_sig));
    return -1;
  }

  if (FLIP1(pgm)->part_sig[0] == 0 &&
      FLIP1(pgm)->part_sig[1] == 0 &&
      FLIP1(pgm)->part_sig[2] == 0)
  {
    /* signature not yet cached */
    struct dfu_status status;
    int cmd_result = 0;
    int aux_result;
    int i;
    struct flip1_cmd cmd = {
      FLIP1_CMD_READ_COMMAND, FLIP1_READ_FAMILY_CODE
    };

    avrdude_message(MSG_NOTICE2, "from device\n");

    for (i = 0; i < 3; i++)
    {
      if (i == 1)
        cmd.args[1] = 0x60;     /* product name */
      else if (i == 2)
        cmd.args[1] = 0x61;     /* product revision */

      cmd_result = dfu_dnload(FLIP1(pgm)->dfu, &cmd, 3);
      aux_result = dfu_getstatus(FLIP1(pgm)->dfu, &status);

      if (cmd_result < 0 || aux_result < 0)
        return -1;

      if (status.bStatus != DFU_STATUS_OK)
      {
        avrdude_message(MSG_INFO, "%s: failed to send cmd for signature byte %d: %s\n",
                progname, i, flip1_status_str(&status));
        if (status.bState == STATE_dfuERROR)
          dfu_clrstatus(FLIP1(pgm)->dfu);
        return -1;
      }

      cmd_result = dfu_upload(FLIP1(pgm)->dfu, &(FLIP1(pgm)->part_sig[i]), 1);
      aux_result = dfu_getstatus(FLIP1(pgm)->dfu, &status);

      if (cmd_result < 0 || aux_result < 0)
        return -1;

      if (status.bStatus != DFU_STATUS_OK)
      {
        avrdude_message(MSG_INFO, "%s: failed to read signature byte %d: %s\n",
                progname, i, flip1_status_str(&status));
        if (status.bState == STATE_dfuERROR)
          dfu_clrstatus(FLIP1(pgm)->dfu);
        return -1;
      }
    }
  }
  else
  {
    avrdude_message(MSG_NOTICE2, "cached\n");
  }

  memcpy(mem->buf, FLIP1(pgm)->part_sig, sizeof(FLIP1(pgm)->part_sig));

  return 0;
}

void flip1_setup(PROGRAMMER * pgm)
{
  pgm->cookie = calloc(1, sizeof(struct flip1));

  if (pgm->cookie == NULL) {
    avrdude_message(MSG_INFO, "%s: Out of memory allocating private data structure\n",
            progname);
    exit(1);
  }
}

void flip1_teardown(PROGRAMMER * pgm)
{
  free(pgm->cookie);
  pgm->cookie = NULL;
}

/* INTERNAL FUNCTION DEFINITIONS
 */

void flip1_show_info(struct flip1 *flip1)
{
  dfu_show_info(flip1->dfu);
  avrdude_message(MSG_INFO, "    USB max packet size : %hu\n",
    (unsigned short) flip1->dfu->dev_desc.bMaxPacketSize0);
}

int flip1_read_memory(PROGRAMMER * pgm,
  enum flip1_mem_unit mem_unit, uint32_t addr, void *ptr, int size)
{
  struct dfu_dev *dfu = FLIP1(pgm)->dfu;
  unsigned short page_addr;
  struct dfu_status status;
  int cmd_result = 0;
  int aux_result;
  struct flip1_cmd cmd = {
    FLIP1_CMD_DISPLAY_DATA, { mem_unit }
  };
  unsigned int default_timeout = dfu->timeout;


  avrdude_message(MSG_NOTICE2, "%s: flip_read_memory(%s, 0x%04x, %d)\n",
                  progname, flip1_mem_unit_str(mem_unit), addr, size);

  /*
   * As this function is called once per page, no need to handle 64
   * KiB border crossing below.
   *
   * Also, on AVRs, no page size is larger than 1 KiB, so no need to
   * split the request into multiple 1 KiB chunks.
   */
  if (mem_unit == FLIP1_MEM_UNIT_FLASH) {
    page_addr = addr >> 16;
    if (flip1_set_mem_page(dfu, page_addr) < 0)
      return -1;
  }

  cmd.args[1] = (addr >> 8) & 0xFF;
  cmd.args[2] = addr & 0xFF;
  cmd.args[3] = ((addr + size - 1) >> 8) & 0xFF;
  cmd.args[4] = (addr + size - 1) & 0xFF;

  dfu->timeout = LONG_DFU_TIMEOUT;
  cmd_result = dfu_dnload(dfu, &cmd, 6);
  dfu->timeout = default_timeout;
  aux_result = dfu_getstatus(dfu, &status);

  if (cmd_result < 0 || aux_result < 0)
    return -1;

  if (status.bStatus != DFU_STATUS_OK)
  {
    avrdude_message(MSG_INFO, "%s: failed to read %u bytes of %s memory @%u: %s\n",
            progname, size, flip1_mem_unit_str(mem_unit), addr,
            flip1_status_str(&status));
    if (status.bState == STATE_dfuERROR)
      dfu_clrstatus(dfu);
    return -1;
  }

  cmd_result = dfu_upload(dfu, (char*) ptr, size);
  aux_result = dfu_getstatus(dfu, &status);

  if (cmd_result < 0 && aux_result == 0 &&
      status.bStatus == DFU_STATUS_ERR_WRITE) {
    if (FLIP1(pgm)->security_mode_flag == 0)
      avrdude_message(MSG_INFO, "\n%s:\n"
              "%s***********************************************************************\n"
              "%sMaybe the device is in ``security mode´´, and needs a chip erase first?\n"
              "%s***********************************************************************\n"
              "\n",
              progname, progbuf, progbuf, progbuf);
    FLIP1(pgm)->security_mode_flag = 1;
  }

  if (cmd_result < 0 || aux_result < 0)
    return -1;

  if (status.bStatus != DFU_STATUS_OK)
  {
    avrdude_message(MSG_INFO, "%s: failed to read %u bytes of %s memory @%u: %s\n",
            progname, size, flip1_mem_unit_str(mem_unit), addr,
            flip1_status_str(&status));
    if (status.bState == STATE_dfuERROR)
      dfu_clrstatus(dfu);
    return -1;
  }

  return 0;
}

int flip1_write_memory(struct dfu_dev *dfu,
  enum flip1_mem_unit mem_unit, uint32_t addr, const void *ptr, int size)
{
  unsigned short page_addr;
  int write_size;
  struct dfu_status status;
  int cmd_result = 0;
  int aux_result;
  struct flip1_cmd_header cmd_header = {
    FLIP1_CMD_PROG_START, mem_unit
  };
  struct flip1_prog_footer cmd_footer = {
    { 0, 0, 0, 0 },             /* CRC */
    0x10,                       /* footer length */
    { 'D', 'F', 'U' },          /* signature */
    { 0x01, 0x10 },             /* BCD version */
    { 0xff, 0xff },             /* vendor */
    { 0xff, 0xff },             /* product */
    { 0xff, 0xff }              /* device */
  };
  unsigned int default_timeout = dfu->timeout;
  unsigned char *buf;

  avrdude_message(MSG_NOTICE2, "%s: flip_write_memory(%s, 0x%04x, %d)\n",
                  progname, flip1_mem_unit_str(mem_unit), addr, size);

  if (size < 32) {
    /* presumably single-byte updates; must be padded to USB endpoint size */
    if ((addr + size - 1) / 32 != addr / 32) {
      avrdude_message(MSG_INFO, "%s: flip_write_memory(): begin (0x%x) and end (0x%x) not within same 32-byte block\n",
                      progname, addr, addr + size - 1);
      return -1;
    }
    write_size = 32;
  } else {
    write_size = size;
  }

  if ((buf = malloc(sizeof(struct flip1_cmd_header) +
                    write_size +
                    sizeof(struct flip1_prog_footer))) == 0) {
    avrdude_message(MSG_INFO, "%s: Out of memory\n", progname);
    return -1;
  }

  /*
   * As this function is called once per page, no need to handle 64
   * KiB border crossing below.
   *
   * Also, on AVRs, no page size is larger than 1 KiB, so no need to
   * split the request into multiple 1 KiB chunks.
   */
  if (mem_unit == FLIP1_MEM_UNIT_FLASH) {
    page_addr = addr >> 16;
    if (flip1_set_mem_page(dfu, page_addr) < 0) {
      free(buf);
      return -1;
    }
  }

  cmd_header.start_addr[0] = (addr >> 8) & 0xFF;
  cmd_header.start_addr[1] = addr & 0xFF;
  cmd_header.end_addr[0] = ((addr + size - 1) >> 8) & 0xFF;
  cmd_header.end_addr[1] = (addr + size - 1) & 0xFF;

  memcpy(buf, &cmd_header, sizeof(struct flip1_cmd_header));
  if (size < 32) {
    memset(buf + sizeof(struct flip1_cmd_header), 0xff, 32);
    memcpy(buf + sizeof(struct flip1_cmd_header) + (addr % 32), ptr, size);
  } else {
    memcpy(buf + sizeof(struct flip1_cmd_header), ptr, size);
  }
  memcpy(buf + sizeof(struct flip1_cmd_header) + write_size,
         &cmd_footer, sizeof(struct flip1_prog_footer));

  dfu->timeout = LONG_DFU_TIMEOUT;
  cmd_result = dfu_dnload(dfu, buf,
                          sizeof(struct flip1_cmd_header) +
                          write_size +
                          sizeof(struct flip1_prog_footer));
  aux_result = dfu_getstatus(dfu, &status);
  dfu->timeout = default_timeout;

  free(buf);

  if (aux_result < 0 || cmd_result < 0)
    return -1;

  if (status.bStatus != DFU_STATUS_OK)
  {
    avrdude_message(MSG_INFO, "%s: failed to write %u bytes of %s memory @%u: %s\n",
            progname, size, flip1_mem_unit_str(mem_unit), addr,
            flip1_status_str(&status));
    if (status.bState == STATE_dfuERROR)
      dfu_clrstatus(dfu);
    return -1;
  }

  return 0;
}

int flip1_set_mem_page(struct dfu_dev *dfu,
  unsigned short page_addr)
{
  struct dfu_status status;
  int cmd_result = 0;
  int aux_result;

  struct flip1_cmd cmd = {
    FLIP1_CMD_CHANGE_BASE_ADDRESS, { 0, page_addr }
  };

  cmd_result = dfu_dnload(dfu, &cmd, 3);

  aux_result = dfu_getstatus(dfu, &status);

  if (cmd_result < 0 || aux_result < 0)
    return -1;

  if (status.bStatus != DFU_STATUS_OK)
  {
    avrdude_message(MSG_INFO, "%s: failed to set memory page: %s\n",
            progname, flip1_status_str(&status));
    if (status.bState == STATE_dfuERROR)
      dfu_clrstatus(dfu);
    return -1;
  }

  return 0;
}

const char * flip1_status_str(const struct dfu_status *status)
{
  static const char *msg[] = {
    "No error condition is present",
    "File is not targeted for use by this device",
    "File is for this device but fails some vendor-specific verification test",
    "Device id unable to write memory",
    "Memory erase function failed",
    "Memory erase check failed",
    "Program memory function failed",
    "Programmed memory failed verification",
    "Cannot program memory due to received address that is out of range",
    "Received DFU_DNLOAD with wLength = 0, but device does not think it has all the data yet.",
    "Device's firmware is corrupted. It cannot return to run-time operations",
    "iString indicates a vendor-specific error",
    "Device detected unexpected USB reset signaling",
    "Device detected unexpected power on reset",
    "Something went wrong, but the device does not know what it was",
    "Device stalled an unexpected request",
  };
  if (status->bStatus < sizeof msg / sizeof msg[0])
    return msg[status->bStatus];

  return "Unknown status code";
}

const char * flip1_mem_unit_str(enum flip1_mem_unit mem_unit)
{
  switch (mem_unit) {
  case FLIP1_MEM_UNIT_FLASH: return "Flash";
  case FLIP1_MEM_UNIT_EEPROM: return "EEPROM";
  default: return "unknown";
  }
}

enum flip1_mem_unit flip1_mem_unit(const char *name) {
  if (strcasecmp(name, "flash") == 0)
    return FLIP1_MEM_UNIT_FLASH;
  if (strcasecmp(name, "eeprom") == 0)
    return FLIP1_MEM_UNIT_EEPROM;
  return FLIP1_MEM_UNIT_UNKNOWN;
}
#else /* HAVE_LIBUSB */
// Dummy functions
int flip1_open(PROGRAMMER *pgm, char *port_spec)
{
  fprintf(stderr, "%s: Error: No USB support in this compile of avrdude\n",
    progname);
  return -1;
}

int flip1_initialize(PROGRAMMER* pgm, AVRPART *part)
{
  return -1;
}

void flip1_close(PROGRAMMER* pgm)
{
}

void flip1_enable(PROGRAMMER* pgm)
{
}

void flip1_disable(PROGRAMMER* pgm)
{
}

void flip1_display(PROGRAMMER* pgm, const char *prefix)
{
}

int flip1_program_enable(PROGRAMMER* pgm, AVRPART *part)
{
  return -1;
}

int flip1_chip_erase(PROGRAMMER* pgm, AVRPART *part)
{
  return -1;
}

int flip1_read_byte(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned long addr, unsigned char *value)
{
  return -1;
}

int flip1_write_byte(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned long addr, unsigned char value)
{
  return -1;
}

int flip1_paged_load(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned int page_size, unsigned int addr, unsigned int n_bytes)
{
  return -1;
}

int flip1_paged_write(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem,
  unsigned int page_size, unsigned int addr, unsigned int n_bytes)
{
  return -1;
}

int flip1_read_sig_bytes(PROGRAMMER* pgm, AVRPART *part, AVRMEM *mem)
{
  return -1;
}

void flip1_setup(PROGRAMMER * pgm)
{
}

void flip1_teardown(PROGRAMMER * pgm)
{
}


#endif /* HAVE_LIBUSB */
