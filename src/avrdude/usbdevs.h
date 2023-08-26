/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2006 Joerg Wunsch
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

/*
 * defines for the USB interface
 */

#ifndef usbdevs_h
#define usbdevs_h

#define USB_VENDOR_ATMEL 1003
#define USB_DEVICE_JTAGICEMKII 0x2103
#define USB_DEVICE_AVRISPMKII  0x2104
#define USB_DEVICE_STK600      0x2106
#define USB_DEVICE_AVRDRAGON   0x2107
#define USB_DEVICE_JTAGICE3    0x2110
#define USB_DEVICE_XPLAINEDPRO 0x2111
#define USB_DEVICE_JTAG3_EDBG  0x2140
#define USB_DEVICE_ATMEL_ICE   0x2141

#define USB_VENDOR_FTDI        0x0403
#define USB_DEVICE_FT2232      0x6010
#define USB_DEVICE_FT245       0x6001

#define	USBASP_SHARED_VID   0x16C0  /* VOTI */
#define	USBASP_SHARED_PID   0x05DC  /* Obdev's free shared PID */

#define	USBASP_OLD_VID      0x03EB  /* ATMEL */
#define	USBASP_OLD_PID	    0xC7B4  /* (unoffical) USBasp */

#define	USBASP_NIBOBEE_VID  0x16C0  /* VOTI */
#define	USBASP_NIBOBEE_PID  0x092F  /* NIBObee PID */

// these are specifically assigned to USBtiny,
// if you need your own VID and PIDs you can get them for cheap from
// www.mecanique.co.uk so please don't reuse these. Thanks!
#define USBTINY_VENDOR_DEFAULT  0x1781
#define USBTINY_PRODUCT_DEFAULT 0x0C9F



/* JTAGICEmkII, AVRISPmkII */
#define USBDEV_BULK_EP_WRITE_MKII 0x02
#define USBDEV_BULK_EP_READ_MKII  0x82
#define USBDEV_MAX_XFER_MKII 64

/* STK600 */
#define USBDEV_BULK_EP_WRITE_STK600 0x02
#define USBDEV_BULK_EP_READ_STK600 0x83

/* JTAGICE3 */
#define USBDEV_BULK_EP_WRITE_3    0x01
#define USBDEV_BULK_EP_READ_3     0x82
#define USBDEV_EVT_EP_READ_3      0x83
#define USBDEV_MAX_XFER_3    512

/*
 * When operating on the JTAGICE3, usbdev_recv_frame() returns an
 * indication in the upper bits of the return value whether the
 * message has been received from the event endpoint rather than the
 * normal conversation endpoint.
 */
#define USB_RECV_LENGTH_MASK   0x0fff /* up to 4 KiB */
#define USB_RECV_FLAG_EVENT    0x1000

#endif  /* usbdevs_h */
