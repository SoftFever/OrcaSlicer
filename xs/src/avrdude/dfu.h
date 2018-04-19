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

#ifndef dfu_h
#define dfu_h

#include "ac_cfg.h"

#ifdef HAVE_LIBUSB
#if defined(HAVE_USB_H)
#  include <usb.h>
#elif defined(HAVE_LUSB0_USB_H)
#  include <lusb0_usb.h>
#else
#  error "libusb needs either <usb.h> or <lusb0_usb.h>"
#endif
#endif

#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/* If we have LIBUSB, define the dfu_dev struct normally. Otherwise, declare
 * it as an empty struct so that code compiles, but we generate an error at
 * run time.
 */

#ifdef HAVE_LIBUSB

struct dfu_dev
{
  char *bus_name, *dev_name;
  usb_dev_handle *dev_handle;
  struct usb_device_descriptor dev_desc;
  struct usb_config_descriptor conf_desc;
  struct usb_interface_descriptor intf_desc;
  struct usb_endpoint_descriptor endp_desc;
  char *manf_str, *prod_str, *serno_str;
  unsigned int timeout;
};

#else

struct dfu_dev {
  // empty
};

#endif

/* We assume unsigned char is 1 byte. */

#if UCHAR_MAX != 255
#error UCHAR_MAX != 255
#endif

struct dfu_status {
  unsigned char bStatus;
  unsigned char bwPollTimeout[3];
  unsigned char bState;
  unsigned char iString;
};

// Values of bStatus field.

#define DFU_STATUS_OK 0x0
#define DFU_STATUS_ERR_TARGET 0x1
#define DFU_STATUS_ERR_FILE 0x2
#define DFU_STATUS_ERR_WRITE 0x3
#define DFU_STATUS_ERR_ERASE 0x4
#define DFU_STATUS_ERR_CHECK_ERASED 0x5
#define DFU_STATUS_ERR_PROG 0x6
#define DFU_STATUS_ERR_VERIFY 0x7
#define DFU_STATUS_ERR_ADDRESS 0x8
#define DFU_STATUS_ERR_NOTDONE 0x9
#define DFU_STATUS_ERR_FIRMWARE 0xA
#define DFU_STATUS_ERR_VENDOR 0xB
#define DFU_STATUS_ERR_USBR 0xC
#define DFU_STATUS_ERR_POR 0xD
#define DFU_STATUS_ERR_UNKNOWN 0xE
#define DFU_STATUS_ERR_STALLEDPKT 0xF

// Values of bState field.

#define DFU_STATE_APP_IDLE 0
#define DFU_STATE_APP_DETACH 1
#define DFU_STATE_DFU_IDLE 2
#define DFU_STATE_DFU_DLOAD_SYNC 3
#define DFU_STATE_DFU_DNBUSY 4
#define DFU_STATE_DFU_DNLOAD_IDLE 5
#define DFU_STATE_DFU_MANIFEST_SYNC 6
#define DFU_STATE_DFU_MANIFEST 7
#define DFU_STATE_DFU_MANIFEST_WAIT_RESET 8
#define DFU_STATE_DFU_UPLOAD_IDLE 9
#define DFU_STATE_DFU_ERROR 10

// FUNCTIONS

extern struct dfu_dev * dfu_open(char *port_spec);
extern int dfu_init(struct dfu_dev *dfu,
  unsigned short vid, unsigned short pid);
extern void dfu_close(struct dfu_dev *dfu);

extern int dfu_getstatus(struct dfu_dev *dfu, struct dfu_status *status);
extern int dfu_clrstatus(struct dfu_dev *dfu);
extern int dfu_dnload(struct dfu_dev *dfu, void *ptr, int size);
extern int dfu_upload(struct dfu_dev *dfu, void *ptr, int size);
extern int dfu_abort(struct dfu_dev *dfu);

extern void dfu_show_info(struct dfu_dev *dfu);

extern const char * dfu_status_str(int bStatus);
extern const char * dfu_state_str(int bState);

#ifdef __cplusplus
}
#endif

#endif /* dfu_h */
