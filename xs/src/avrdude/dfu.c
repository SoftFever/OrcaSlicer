/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2012 Kirill Levchenko
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
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "dfu.h"

#include "usbdevs.h" /* for USB_VENDOR_ATMEL */

/* If we don't have LibUSB, define dummy functions that report an error. */

#ifndef HAVE_LIBUSB

struct dfu_dev *dfu_open(char *port_name) {
  avrdude_message(MSG_INFO, "%s: Error: No USB support in this compile of avrdude\n",
    progname);
  return NULL;
}

int dfu_init(struct dfu_dev *dfu, unsigned short usb_vid, unsigned short usb_pid) {
  return -1;
}

void dfu_close(struct dfu_dev *dfu) {
  /* nothing */
}

int dfu_getstatus(struct dfu_dev *dfu, struct dfu_status *status)
{
  return -1;
}

int dfu_clrstatus(struct dfu_dev *dfu) {
  return -1;
}

int dfu_download(struct dfu_dev *dfu, void * ptr, int size) {
  return -1;
}

int dfu_upload(struct dfu_dev *dfu, void * ptr, int size) {
  return -1;
}

#else

/* If we DO have LibUSB, we can define the real functions. */

/* DFU data structures and constants.
 */

#define DFU_TIMEOUT 200 /* ms */

#define DFU_DNLOAD 1
#define DFU_UPLOAD 2
#define DFU_GETSTATUS 3
#define DFU_CLRSTATUS 4
#define DFU_GETSTATE 5          /* FLIPv1 only; not used */
#define DFU_ABORT 6             /* FLIPv1 only */

/* Block counter global variable. Incremented each time a DFU_DNLOAD command
 * is sent to the device.
 */

static uint16_t wIndex = 0;

/* INTERNAL FUNCTION PROTOTYPES
 */

static char * get_usb_string(usb_dev_handle * dev_handle, int index);

/* EXPORTED FUNCTION DEFINITIONS
 */

struct dfu_dev * dfu_open(char *port_spec)
{
  struct dfu_dev *dfu;
  char *bus_name = NULL;
  char *dev_name = NULL;

  /* The following USB device spec parsing code was copied from usbtiny.c. The
   * expected format is "usb:BUS:DEV" where BUS and DEV are the bus and device
   * names. We stash these away in the dfu_dev structure for the dfu_init()
   * function, where we actually open the device.
   */

  if (strncmp(port_spec, "usb", 3) != 0) {
    avrdude_message(MSG_INFO, "%s: Error: "
      "Invalid port specification \"%s\" for USB device\n",
      progname, port_spec);
    return NULL;
  }

  if(':' == port_spec[3]) {
      bus_name = strdup(port_spec + 3 + 1);
      if (bus_name == NULL) {
        avrdude_message(MSG_INFO, "%s: Out of memory in strdup\n", progname);
        return NULL;
      }

      dev_name = strchr(bus_name, ':');
      if(NULL != dev_name)
        *dev_name++ = '\0';
  }

  /* Allocate the dfu_dev structure and save the bus_name and dev_name
   * strings for use in dfu_initialize().
   */

  dfu = calloc(1, sizeof(struct dfu_dev));

  if (dfu == NULL)
  {
    avrdude_message(MSG_INFO, "%s: out of memory\n", progname);
    free(bus_name);
    return NULL;
  }

  dfu->bus_name = bus_name;
  dfu->dev_name = dev_name;
  dfu->timeout = DFU_TIMEOUT;

  /* LibUSB initialization. */

  usb_init();
  usb_find_busses();
  usb_find_devices();

  return dfu;
}

int dfu_init(struct dfu_dev *dfu, unsigned short vid, unsigned short pid)
{
  struct usb_device *found = NULL;
  struct usb_device *dev;
  struct usb_bus *bus;

  /* At last, we reach out through the USB bus to the part. There are three
   * ways to specify the part: by USB address, by USB vendor and product id,
   * and by part name. To specify the part by USB address, the user specifies
   * a port parameter in the form "usb:BUS:DEV" (see dfu_open()). To specify
   * the part by vendor and product, the user must specify a usbvid and usbpid
   * in the configuration file. Finally, if the user specifies the part only,
   * we use the default vendor and product id.
   */

  if (pid == 0 && dfu->dev_name == NULL) {
    avrdude_message(MSG_INFO, "%s: Error: No DFU support for part; "
      "specify PID in config or USB address (via -P) to override.\n",
      progname);
    return -1;
  }

  /* Scan through all the devices for the part. The matching rules are:
   *
   *   1. If the user specified a USB bus name, it must match.
   *   2. If the user specified a USB device name, it must match.
   *   3. If the user didn't specify a USB device name and specified a vendor
   *      id, the vendor id must match.
   *   4. If the user didn't specify a USB device name and specified a product
   *      id, the product id must match.
   */

  for (bus = usb_busses; !found && bus != NULL; bus = bus->next) {
    for (dev = bus->devices; !found && dev != NULL; dev = dev->next) {
      if (dfu->bus_name != NULL && strcmp(bus->dirname, dfu->bus_name))
         continue;
      if (dfu->dev_name != NULL) {
        if (strcmp(dev->filename, dfu->dev_name))
          continue;
      } else if (vid != dev->descriptor.idVendor)
        continue;
      else if (pid != 0 && pid != dev->descriptor.idProduct)
        continue;

      found = dev;
    }
  }

  if (found == NULL) {
    /* We could try to be more informative here. For example, we could report
     * why the match failed, and if we came across another DFU-capable part.
     */

    avrdude_message(MSG_INFO, "%s: Error: No matching USB device found\n", progname);
    return -1;
  }

  if(verbose)
    avrdude_message(MSG_INFO, "%s: Found VID=0x%04x PID=0x%04x at %s:%s\n",
                    progname, found->descriptor.idVendor, found->descriptor.idProduct,
                    found->bus->dirname, found->filename);

  dfu->dev_handle = usb_open(found);

  if (dfu->dev_handle == NULL) {
    avrdude_message(MSG_INFO, "%s: Error: USB device at %s:%s: %s\n",
      progname, found->bus->dirname, found->filename, usb_strerror());
    return -1;
  }

  /* Save device, configuration, interface and endpoint descriptors. */

  memcpy(&dfu->dev_desc, &found->descriptor, sizeof(dfu->dev_desc));
  memcpy(&dfu->conf_desc, found->config, sizeof(dfu->conf_desc));
  dfu->conf_desc.interface = NULL;

  memcpy(&dfu->intf_desc, found->config->interface->altsetting,
    sizeof(dfu->intf_desc));
  dfu->intf_desc.endpoint = &dfu->endp_desc;

  if (found->config->interface->altsetting->endpoint != 0)
      memcpy(&dfu->endp_desc, found->config->interface->altsetting->endpoint,
             sizeof(dfu->endp_desc));

  /* Get strings. */

  dfu->manf_str = get_usb_string(dfu->dev_handle,
    dfu->dev_desc.iManufacturer);

  dfu->prod_str = get_usb_string(dfu->dev_handle,
    dfu->dev_desc.iProduct);

  dfu->serno_str = get_usb_string(dfu->dev_handle,
    dfu->dev_desc.iSerialNumber);

  return 0;
}

void dfu_close(struct dfu_dev *dfu)
{
  if (dfu->dev_handle != NULL)
    usb_close(dfu->dev_handle);
  if (dfu->bus_name != NULL)
    free(dfu->bus_name);
  if (dfu->manf_str != NULL)
    free(dfu->manf_str);
  if (dfu->prod_str != NULL)
    free(dfu->prod_str);
  if (dfu->serno_str != NULL)
    free(dfu->serno_str);
}

int dfu_getstatus(struct dfu_dev *dfu, struct dfu_status *status)
{
  int result;

  avrdude_message(MSG_TRACE, "%s: dfu_getstatus(): issuing control IN message\n",
            progname);

  result = usb_control_msg(dfu->dev_handle,
    0x80 | USB_TYPE_CLASS | USB_RECIP_INTERFACE, DFU_GETSTATUS, 0, 0,
    (char*) status, sizeof(struct dfu_status), dfu->timeout);

  if (result < 0) {
    avrdude_message(MSG_INFO, "%s: Error: Failed to get DFU status: %s\n",
      progname, usb_strerror());
    return -1;
  }

  if (result < sizeof(struct dfu_status)) {
    avrdude_message(MSG_INFO, "%s: Error: Failed to get DFU status: %s\n",
      progname, "short read");
    return -1;
  }

  if (result > sizeof(struct dfu_status)) {
    avrdude_message(MSG_INFO, "%s: Error: Oversize read (should not happen); "
      "exiting\n", progname);
    exit(1);
  }

  avrdude_message(MSG_TRACE, "%s: dfu_getstatus(): bStatus 0x%02x, bwPollTimeout %d, bState 0x%02x, iString %d\n",
                  progname,
                  status->bStatus,
                  status->bwPollTimeout[0] | (status->bwPollTimeout[1] << 8) | (status->bwPollTimeout[2] << 16),
                  status->bState,
                  status->iString);

  return 0;
}

int dfu_clrstatus(struct dfu_dev *dfu)
{
  int result;

  avrdude_message(MSG_TRACE, "%s: dfu_clrstatus(): issuing control OUT message\n",
                  progname);

  result = usb_control_msg(dfu->dev_handle,
    USB_TYPE_CLASS | USB_RECIP_INTERFACE, DFU_CLRSTATUS, 0, 0,
    NULL, 0, dfu->timeout);

  if (result < 0) {
    avrdude_message(MSG_INFO, "%s: Error: Failed to clear DFU status: %s\n",
      progname, usb_strerror());
    return -1;
  }

  return 0;
}

int dfu_abort(struct dfu_dev *dfu)
{
  int result;

  avrdude_message(MSG_TRACE, "%s: dfu_abort(): issuing control OUT message\n",
                  progname);

  result = usb_control_msg(dfu->dev_handle,
    USB_TYPE_CLASS | USB_RECIP_INTERFACE, DFU_ABORT, 0, 0,
    NULL, 0, dfu->timeout);

  if (result < 0) {
    avrdude_message(MSG_INFO, "%s: Error: Failed to reset DFU state: %s\n",
      progname, usb_strerror());
    return -1;
  }

  return 0;
}


int dfu_dnload(struct dfu_dev *dfu, void *ptr, int size)
{
  int result;

  avrdude_message(MSG_TRACE, "%s: dfu_dnload(): issuing control OUT message, wIndex = %d, ptr = %p, size = %d\n",
                  progname, wIndex, ptr, size);

  result = usb_control_msg(dfu->dev_handle,
    USB_TYPE_CLASS | USB_RECIP_INTERFACE, DFU_DNLOAD, wIndex++, 0,
    ptr, size, dfu->timeout);

  if (result < 0) {
    avrdude_message(MSG_INFO, "%s: Error: DFU_DNLOAD failed: %s\n",
      progname, usb_strerror());
    return -1;
  }

  if (result < size) {
    avrdude_message(MSG_INFO, "%s: Error: DFU_DNLOAD failed: %s\n",
      progname, "short write");
    return -1;
  }

  if (result > size) {
    avrdude_message(MSG_INFO, "%s: Error: Oversize write (should not happen); " \
      "exiting\n", progname);
    exit(1);
  }

  return 0;
}

int dfu_upload(struct dfu_dev *dfu, void *ptr, int size)
{
  int result;

  avrdude_message(MSG_TRACE, "%s: dfu_upload(): issuing control IN message, wIndex = %d, ptr = %p, size = %d\n",
                  progname, wIndex, ptr, size);

  result = usb_control_msg(dfu->dev_handle,
    0x80 | USB_TYPE_CLASS | USB_RECIP_INTERFACE, DFU_UPLOAD, wIndex++, 0,
    ptr, size, dfu->timeout);

  if (result < 0) {
    avrdude_message(MSG_INFO, "%s: Error: DFU_UPLOAD failed: %s\n",
      progname, usb_strerror());
    return -1;
  }

  if (result < size) {
    avrdude_message(MSG_INFO, "%s: Error: DFU_UPLOAD failed: %s\n",
      progname, "short read");
    return -1;
  }

  if (result > size) {
    avrdude_message(MSG_INFO, "%s: Error: Oversize read (should not happen); "
      "exiting\n", progname);
    exit(1);
  }

  return 0;
}

void dfu_show_info(struct dfu_dev *dfu)
{
  if (dfu->manf_str != NULL)
    avrdude_message(MSG_INFO, "    USB Vendor          : %s (0x%04hX)\n",
      dfu->manf_str, (unsigned short) dfu->dev_desc.idVendor);
  else
    avrdude_message(MSG_INFO, "    USB Vendor          : 0x%04hX\n",
      (unsigned short) dfu->dev_desc.idVendor);

  if (dfu->prod_str != NULL)
    avrdude_message(MSG_INFO, "    USB Product         : %s (0x%04hX)\n",
      dfu->prod_str, (unsigned short) dfu->dev_desc.idProduct);
  else
    avrdude_message(MSG_INFO, "    USB Product         : 0x%04hX\n",
      (unsigned short) dfu->dev_desc.idProduct);

  avrdude_message(MSG_INFO, "    USB Release         : %hu.%hu.%hu\n",
    ((unsigned short) dfu->dev_desc.bcdDevice >> 8) & 0xFF,
    ((unsigned short) dfu->dev_desc.bcdDevice >> 4) & 0xF,
    ((unsigned short) dfu->dev_desc.bcdDevice >> 0) & 0xF);

  if (dfu->serno_str != NULL)
    avrdude_message(MSG_INFO, "    USB Serial No       : %s\n", dfu->serno_str);
}

/* INTERNAL FUNCTION DEFINITIONS
 */

char * get_usb_string(usb_dev_handle * dev_handle, int index) {
  char buffer[256];
  char * str;
  int result;

  if (index == 0)
    return NULL;

  result = usb_get_string_simple(dev_handle, index, buffer, sizeof(buffer)-1);

  if (result < 0) {
    avrdude_message(MSG_INFO, "%s: Warning: Failed to read USB device string %d: %s\n",
      progname, index, usb_strerror());
    return NULL;
  }

  str = malloc(result+1);

  if (str == NULL) {
    avrdude_message(MSG_INFO, "%s: Out of memory allocating a string\n", progname);
    return 0;
  }

  memcpy(str, buffer, result);
  str[result] = '\0';
  return str;
}

#endif /* defined(HAVE_LIBUSB) */

/* EXPORTED FUNCTIONS THAT DO NO REQUIRE LIBUSB
 */

const char * dfu_status_str(int bStatus)
{
  switch (bStatus) {
    case DFU_STATUS_OK: return "OK";
    case DFU_STATUS_ERR_TARGET: return "ERR_TARGET";
    case DFU_STATUS_ERR_FILE: return "ERR_FILE";
    case DFU_STATUS_ERR_WRITE: return "ERR_WRITE";
    case DFU_STATUS_ERR_ERASE: return "ERR_ERASE";
    case DFU_STATUS_ERR_CHECK_ERASED: return "ERR_CHECK_ERASED";
    case DFU_STATUS_ERR_PROG: return "ERR_PROG";
    case DFU_STATUS_ERR_VERIFY: return "ERR_VERIFY";
    case DFU_STATUS_ERR_ADDRESS: return "ERR_ADDRESS";
    case DFU_STATUS_ERR_NOTDONE: return "ERR_NOTDONE";
    case DFU_STATUS_ERR_FIRMWARE: return "ERR_FIRMWARE";
    case DFU_STATUS_ERR_VENDOR: return "ERR_VENDOR";
    case DFU_STATUS_ERR_USBR: return "ERR_USBR";
    case DFU_STATUS_ERR_POR: return "ERR_POR";
    case DFU_STATUS_ERR_UNKNOWN: return "ERR_UNKNOWN";
    case DFU_STATUS_ERR_STALLEDPKT: return "ERR_STALLEDPKT";
    default: return "Unknown";
  }
}

const char * dfu_state_str(int bState)
{
  switch (bState) {
    case DFU_STATE_APP_IDLE: return "APP_IDLE";
    case DFU_STATE_APP_DETACH: return "APP_DETACH";
    case DFU_STATE_DFU_IDLE: return "DFU_IDLE";
    case DFU_STATE_DFU_DLOAD_SYNC: return "DFU_DLOAD_SYNC";
    case DFU_STATE_DFU_DNBUSY: return "DFU_DNBUSY";
    case DFU_STATE_DFU_DNLOAD_IDLE: return "DFU_DNLOAD_IDLE";
    case DFU_STATE_DFU_MANIFEST_SYNC: return "DFU_MANIFEST_SYNC";
    case DFU_STATE_DFU_MANIFEST: return "DFU_MANIFEST";
    case DFU_STATE_DFU_MANIFEST_WAIT_RESET: return "DFU_MANIFEST_WAIT_RESET";
    case DFU_STATE_DFU_UPLOAD_IDLE: return "DFU_UPLOAD_IDLE";
    case DFU_STATE_DFU_ERROR: return "DFU_ERROR";
    default: return "Unknown";
  }
}

