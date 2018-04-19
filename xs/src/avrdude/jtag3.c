/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2012 Joerg Wunsch <j@uriah.heep.sax.de>
 *
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
 * avrdude interface for Atmel JTAGICE3 programmer
 */

#include "ac_cfg.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "crc16.h"
#include "jtag3.h"
#include "jtag3_private.h"
#include "usbdevs.h"

/*
 * Private data for this programmer.
 */
struct pdata
{
  unsigned short command_sequence; /* Next cmd seqno to issue. */

  /*
   * See jtag3_read_byte() for an explanation of the flash and
   * EEPROM page caches.
   */
  unsigned char *flash_pagecache;
  unsigned long flash_pageaddr;
  unsigned int flash_pagesize;

  unsigned char *eeprom_pagecache;
  unsigned long eeprom_pageaddr;
  unsigned int eeprom_pagesize;

  int prog_enabled;	     /* Cached value of PROGRAMMING status. */

  /* JTAG chain stuff */
  unsigned char jtagchain[4];

  /* Start address of Xmega boot area */
  unsigned long boot_start;

  /* Function to set the appropriate clock parameter */
  int (*set_sck)(PROGRAMMER *, unsigned char *);
};

#define PDATA(pgm) ((struct pdata *)(pgm->cookie))

/*
 * pgm->flag is marked as "for private use of the programmer".
 * The following defines this programmer's use of that field.
 */
#define PGM_FL_IS_DW		(0x0001)
#define PGM_FL_IS_PDI           (0x0002)
#define PGM_FL_IS_JTAG          (0x0004)
#define PGM_FL_IS_EDBG          (0x0008)

static int jtag3_open(PROGRAMMER * pgm, char * port);
static int jtag3_edbg_prepare(PROGRAMMER * pgm);
static int jtag3_edbg_signoff(PROGRAMMER * pgm);
static int jtag3_edbg_send(PROGRAMMER * pgm, unsigned char * data, size_t len);
static int jtag3_edbg_recv_frame(PROGRAMMER * pgm, unsigned char **msg);

static int jtag3_initialize(PROGRAMMER * pgm, AVRPART * p);
static int jtag3_chip_erase(PROGRAMMER * pgm, AVRPART * p);
static int jtag3_read_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
                                unsigned long addr, unsigned char * value);
static int jtag3_write_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
                                unsigned long addr, unsigned char data);
static int jtag3_set_sck_period(PROGRAMMER * pgm, double v);
static void jtag3_print_parms1(PROGRAMMER * pgm, const char * p);
static int jtag3_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                unsigned int page_size,
                                unsigned int addr, unsigned int n_bytes);
static unsigned char jtag3_memtype(PROGRAMMER * pgm, AVRPART * p, unsigned long addr);
static unsigned int jtag3_memaddr(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, unsigned long addr);


void jtag3_setup(PROGRAMMER * pgm)
{
  if ((pgm->cookie = malloc(sizeof(struct pdata))) == 0) {
    avrdude_message(MSG_INFO, "%s: jtag3_setup(): Out of memory allocating private data\n",
                    progname);
    exit(1);
  }
  memset(pgm->cookie, 0, sizeof(struct pdata));
}

void jtag3_teardown(PROGRAMMER * pgm)
{
  free(pgm->cookie);
}


static unsigned long
b4_to_u32(unsigned char *b)
{
  unsigned long l;
  l = b[0];
  l += (unsigned)b[1] << 8;
  l += (unsigned)b[2] << 16;
  l += (unsigned)b[3] << 24;

  return l;
}

static void
u32_to_b4(unsigned char *b, unsigned long l)
{
  b[0] = l & 0xff;
  b[1] = (l >> 8) & 0xff;
  b[2] = (l >> 16) & 0xff;
  b[3] = (l >> 24) & 0xff;
}

static unsigned short
b2_to_u16(unsigned char *b)
{
  unsigned short l;
  l = b[0];
  l += (unsigned)b[1] << 8;

  return l;
}

static void
u16_to_b2(unsigned char *b, unsigned short l)
{
  b[0] = l & 0xff;
  b[1] = (l >> 8) & 0xff;
}

static void jtag3_print_data(unsigned char *b, size_t s)
{
  int i;

  if (s < 2)
    return;

  for (i = 0; i < s; i++) {
    avrdude_message(MSG_INFO, "0x%02x", b[i]);
    if (i % 16 == 15)
      putc('\n', stderr);
    else
      putc(' ', stderr);
  }
  if (i % 16 != 0)
    putc('\n', stderr);
}

static void jtag3_prmsg(PROGRAMMER * pgm, unsigned char * data, size_t len)
{
  int i;

  if (verbose >= 4) {
    avrdude_message(MSG_TRACE, "Raw message:\n");

    for (i = 0; i < len; i++) {
      avrdude_message(MSG_TRACE, "%02x ", data[i]);
      if (i % 16 == 15)
	putc('\n', stderr);
      else
	putc(' ', stderr);
    }
    if (i % 16 != 0)
      putc('\n', stderr);
  }

  switch (data[0]) {
    case SCOPE_INFO:
      avrdude_message(MSG_INFO, "[info] ");
      break;

    case SCOPE_GENERAL:
      avrdude_message(MSG_INFO, "[general] ");
      break;

    case SCOPE_AVR_ISP:
      avrdude_message(MSG_INFO, "[AVRISP] ");
      jtag3_print_data(data + 1, len - 1);
      return;

    case SCOPE_AVR:
      avrdude_message(MSG_INFO, "[AVR] ");
      break;

    default:
      avrdude_message(MSG_INFO, "[scope 0x%02x] ", data[0]);
      break;
  }

  switch (data[1]) {
    case RSP3_OK:
      avrdude_message(MSG_INFO, "OK\n");
      break;

    case RSP3_FAILED:
      avrdude_message(MSG_INFO, "FAILED");
      if (len > 3)
      {
	char reason[50];
	sprintf(reason, "0x%02x", data[3]);
	switch (data[3])
	{
	  case RSP3_FAIL_NO_ANSWER:
	    strcpy(reason, "target does not answer");
	    break;

	  case RSP3_FAIL_NO_TARGET_POWER:
	    strcpy(reason, "no target power");
	    break;

	  case RSP3_FAIL_NOT_UNDERSTOOD:
	    strcpy(reason, "command not understood");
	    break;

	  case RSP3_FAIL_WRONG_MODE:
	    strcpy(reason, "wrong (programming) mode");
	    break;

	  case RSP3_FAIL_PDI:
	    strcpy(reason, "PDI failure");
	    break;

	  case RSP3_FAIL_UNSUPP_MEMORY:
	    strcpy(reason, "unsupported memory type");
	    break;

	  case RSP3_FAIL_WRONG_LENGTH:
	    strcpy(reason, "wrong length in memory access");
	    break;

	  case RSP3_FAIL_DEBUGWIRE:
	    strcpy(reason, "debugWIRE communication failed");
	    break;
	}
	avrdude_message(MSG_INFO, ", reason: %s\n", reason);
      }
      else
      {
	avrdude_message(MSG_INFO, ", unspecified reason\n");
      }
      break;

    case RSP3_DATA:
      avrdude_message(MSG_INFO, "Data returned:\n");
      jtag3_print_data(data + 2, len - 2);
      break;

    case RSP3_INFO:
      avrdude_message(MSG_INFO, "Info returned:\n");
      for (i = 2; i < len; i++) {
	if (isprint(data[i]))
	  putc(data[i], stderr);
	else
	  avrdude_message(MSG_INFO, "\\%03o", data[i]);
      }
      putc('\n', stderr);
      break;

    case RSP3_PC:
      if (len < 7)
      {
	avrdude_message(MSG_INFO, "PC reply too short\n");
      }
      else
      {
	unsigned long pc = (data[6] << 24) | (data[5] << 16)
	  | (data[4] << 8) | data[3];
	avrdude_message(MSG_INFO, "PC 0x%0lx\n", pc);
      }
      break;

  default:
    avrdude_message(MSG_INFO, "unknown message 0x%02x\n", data[1]);
  }
}

static void jtag3_prevent(PROGRAMMER * pgm, unsigned char * data, size_t len)
{
  int i;

  if (verbose >= 4) {
    avrdude_message(MSG_TRACE, "Raw event:\n");

    for (i = 0; i < len; i++) {
      avrdude_message(MSG_TRACE, "%02x ", data[i]);
      if (i % 16 == 15)
	putc('\n', stderr);
      else
	putc(' ', stderr);
    }
    if (i % 16 != 0)
      putc('\n', stderr);
  }

  avrdude_message(MSG_INFO, "Event serial 0x%04x, ",
	  (data[3] << 8) | data[2]);

  switch (data[4]) {
    case SCOPE_INFO:
      avrdude_message(MSG_INFO, "[info] ");
      break;

    case SCOPE_GENERAL:
      avrdude_message(MSG_INFO, "[general] ");
      break;

    case SCOPE_AVR:
      avrdude_message(MSG_INFO, "[AVR] ");
      break;

    default:
      avrdude_message(MSG_INFO, "[scope 0x%02x] ", data[0]);
      break;
  }

  switch (data[5]) {
  case EVT3_BREAK:
    avrdude_message(MSG_INFO, "BREAK");
    if (len >= 11) {
      avrdude_message(MSG_INFO, ", PC = 0x%lx, reason ", b4_to_u32(data + 6));
      switch (data[10]) {
      case 0x00:
	avrdude_message(MSG_INFO, "unspecified");
	break;
      case 0x01:
	avrdude_message(MSG_INFO, "program break");
	break;
      case 0x02:
	avrdude_message(MSG_INFO, "data break PDSB");
	break;
      case 0x03:
	avrdude_message(MSG_INFO, "data break PDMSB");
	break;
      default:
	avrdude_message(MSG_INFO, "unknown: 0x%02x", data[10]);
      }
      /* There are two more bytes of data which always appear to be
       * 0x01, 0x00.  Purpose unknown. */
    }
    break;

  case EVT3_SLEEP:
    if (len >= 8 && data[7] == 0)
      avrdude_message(MSG_INFO, "sleeping");
    else if (len >= 8 && data[7] == 1)
      avrdude_message(MSG_INFO, "wakeup");
    else
      avrdude_message(MSG_INFO, "unknown SLEEP event");
    break;

  case EVT3_POWER:
    if (len >= 8 && data[7] == 0)
      avrdude_message(MSG_INFO, "power-down");
    else if (len >= 8 && data[7] == 1)
      avrdude_message(MSG_INFO, "power-up");
    else
      avrdude_message(MSG_INFO, "unknown POWER event");
    break;

  default:
    avrdude_message(MSG_INFO, "UNKNOWN 0x%02x", data[5]);
    break;
  }
  putc('\n', stderr);
}



int jtag3_send(PROGRAMMER * pgm, unsigned char * data, size_t len)
{
  unsigned char *buf;

  if (pgm->flag & PGM_FL_IS_EDBG)
    return jtag3_edbg_send(pgm, data, len);

  avrdude_message(MSG_DEBUG, "\n%s: jtag3_send(): sending %lu bytes\n",
	    progname, (unsigned long)len);

  if ((buf = malloc(len + 4)) == NULL)
    {
      avrdude_message(MSG_INFO, "%s: jtag3_send(): out of memory",
	      progname);
      return -1;
    }

  buf[0] = TOKEN;
  buf[1] = 0;                   /* dummy */
  u16_to_b2(buf + 2, PDATA(pgm)->command_sequence);
  memcpy(buf + 4, data, len);

  if (serial_send(&pgm->fd, buf, len + 4) != 0) {
    avrdude_message(MSG_INFO, "%s: jtag3_send(): failed to send command to serial port\n",
                    progname);
    return -1;
  }

  free(buf);

  return 0;
}

static int jtag3_edbg_send(PROGRAMMER * pgm, unsigned char * data, size_t len)
{
  unsigned char buf[USBDEV_MAX_XFER_3];
  unsigned char status[USBDEV_MAX_XFER_3];
  int rv;

  if (verbose >= 4)
    {
      memset(buf, 0, USBDEV_MAX_XFER_3);
      memset(status, 0, USBDEV_MAX_XFER_3);
    }

  avrdude_message(MSG_DEBUG, "\n%s: jtag3_edbg_send(): sending %lu bytes\n",
	    progname, (unsigned long)len);

  /* 4 bytes overhead for CMD, fragment #, and length info */
  int max_xfer = pgm->fd.usb.max_xfer;
  int nfragments = (len + max_xfer - 1) / max_xfer;
  if (nfragments > 1)
    {
      avrdude_message(MSG_DEBUG, "%s: jtag3_edbg_send(): fragmenting into %d packets\n",
                      progname, nfragments);
    }
  int frag;
  for (frag = 0; frag < nfragments; frag++)
    {
      int this_len;

      /* All fragments have the (CMSIS-DAP layer) CMD, the fragment
       * identifier, and the length field. */
      buf[0] = EDBG_VENDOR_AVR_CMD;
      buf[1] = ((frag + 1) << 4) | nfragments;

      if (frag == 0)
        {
          /* Only first fragment has TOKEN and seq#, thus four bytes
           * less payload than subsequent fragments. */
          this_len = len < max_xfer - 8? len: max_xfer - 8;
          buf[2] = (this_len + 4) >> 8;
          buf[3] = (this_len + 4) & 0xff;
          buf[4] = TOKEN;
          buf[5] = 0;                   /* dummy */
          u16_to_b2(buf + 6, PDATA(pgm)->command_sequence);
          memcpy(buf + 8, data, this_len);
        }
      else
        {
          this_len = len < max_xfer - 4? len: max_xfer - 4;
          buf[2] = (this_len) >> 8;
          buf[3] = (this_len) & 0xff;
          memcpy(buf + 4, data, this_len);
        }

      if (serial_send(&pgm->fd, buf, max_xfer) != 0) {
        avrdude_message(MSG_INFO, "%s: jtag3_edbg_send(): failed to send command to serial port\n",
                        progname);
        return -1;
      }
      rv = serial_recv(&pgm->fd, status, max_xfer);

      if (rv < 0) {
        /* timeout in receive */
        avrdude_message(MSG_NOTICE2, "%s: jtag3_edbg_send(): Timeout receiving packet\n",
                        progname);
        return -1;
      }
      if (status[0] != EDBG_VENDOR_AVR_CMD ||
          (frag == nfragments - 1 && status[1] != 0x01))
        {
          /* what to do in this case? */
          avrdude_message(MSG_INFO, "%s: jtag3_edbg_send(): Unexpected response 0x%02x, 0x%02x\n",
                          progname, status[0], status[1]);
        }
      data += this_len;
      len -= this_len;
    }

  return 0;
}

/*
 * Send out all the CMSIS-DAP stuff needed to prepare the ICE.
 */
static int jtag3_edbg_prepare(PROGRAMMER * pgm)
{
  unsigned char buf[USBDEV_MAX_XFER_3];
  unsigned char status[USBDEV_MAX_XFER_3];
  int rv;

  avrdude_message(MSG_DEBUG, "\n%s: jtag3_edbg_prepare()\n",
	    progname);

  if (verbose >= 4)
    memset(buf, 0, USBDEV_MAX_XFER_3);

  buf[0] = CMSISDAP_CMD_CONNECT;
  buf[1] = CMSISDAP_CONN_SWD;
  if (serial_send(&pgm->fd, buf, pgm->fd.usb.max_xfer) != 0) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_prepare(): failed to send command to serial port\n",
                    progname);
    return -1;
  }
  rv = serial_recv(&pgm->fd, status, pgm->fd.usb.max_xfer);
  if (rv != pgm->fd.usb.max_xfer) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_prepare(): failed to read from serial port (%d)\n",
                    progname, rv);
    return -1;
  }
  if (status[0] != CMSISDAP_CMD_CONNECT ||
      status[1] == 0)
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_prepare(): unexpected response 0x%02x, 0x%02x\n",
                    progname, status[0], status[1]);
  avrdude_message(MSG_NOTICE2, "%s: jtag3_edbg_prepare(): connection status 0x%02x\n",
                    progname, status[1]);

  buf[0] = CMSISDAP_CMD_LED;
  buf[1] = CMSISDAP_LED_CONNECT;
  buf[2] = 1;
  if (serial_send(&pgm->fd, buf, pgm->fd.usb.max_xfer) != 0) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_prepare(): failed to send command to serial port\n",
                    progname);
    return -1;
  }
  rv = serial_recv(&pgm->fd, status, pgm->fd.usb.max_xfer);
  if (rv != pgm->fd.usb.max_xfer) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_prepare(): failed to read from serial port (%d)\n",
                    progname, rv);
    return -1;
  }
  if (status[0] != CMSISDAP_CMD_LED ||
      status[1] != 0)
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_prepare(): unexpected response 0x%02x, 0x%02x\n",
                    progname, status[0], status[1]);

  return 0;
}


/*
 * Send out all the CMSIS-DAP stuff when signing off.
 */
static int jtag3_edbg_signoff(PROGRAMMER * pgm)
{
  unsigned char buf[USBDEV_MAX_XFER_3];
  unsigned char status[USBDEV_MAX_XFER_3];
  int rv;

  avrdude_message(MSG_DEBUG, "\n%s: jtag3_edbg_signoff()\n",
	    progname);

  if (verbose >= 4)
    memset(buf, 0, USBDEV_MAX_XFER_3);

  buf[0] = CMSISDAP_CMD_LED;
  buf[1] = CMSISDAP_LED_CONNECT;
  buf[2] = 0;
  if (serial_send(&pgm->fd, buf, pgm->fd.usb.max_xfer) != 0) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_signoff(): failed to send command to serial port\n",
                    progname);
    return -1;
  }
  rv = serial_recv(&pgm->fd, status, pgm->fd.usb.max_xfer);
  if (rv != pgm->fd.usb.max_xfer) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_signoff(): failed to read from serial port (%d)\n",
                    progname, rv);
    return -1;
  }
  if (status[0] != CMSISDAP_CMD_LED ||
      status[1] != 0)
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_signoff(): unexpected response 0x%02x, 0x%02x\n",
                    progname, status[0], status[1]);

  buf[0] = CMSISDAP_CMD_DISCONNECT;
  if (serial_send(&pgm->fd, buf, pgm->fd.usb.max_xfer) != 0) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_signoff(): failed to send command to serial port\n",
                    progname);
    return -1;
  }
  rv = serial_recv(&pgm->fd, status, pgm->fd.usb.max_xfer);
  if (rv != pgm->fd.usb.max_xfer) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_signoff(): failed to read from serial port (%d)\n",
                    progname, rv);
    return -1;
  }
  if (status[0] != CMSISDAP_CMD_DISCONNECT ||
      status[1] != 0)
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_signoff(): unexpected response 0x%02x, 0x%02x\n",
                    progname, status[0], status[1]);

  return 0;
}


static int jtag3_drain(PROGRAMMER * pgm, int display)
{
  return serial_drain(&pgm->fd, display);
}


/*
 * Receive one frame, return it in *msg.  Received sequence number is
 * returned in seqno.  Any valid frame will be returned, regardless
 * whether it matches the expected sequence number, including event
 * notification frames (seqno == 0xffff).
 *
 * Caller must eventually free the buffer.
 */
static int jtag3_recv_frame(PROGRAMMER * pgm, unsigned char **msg) {
  int rv;
  unsigned char *buf = NULL;

  if (pgm->flag & PGM_FL_IS_EDBG)
    return jtag3_edbg_recv_frame(pgm, msg);

  avrdude_message(MSG_TRACE, "%s: jtag3_recv():\n", progname);

  if ((buf = malloc(pgm->fd.usb.max_xfer)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtag3_recv(): out of memory\n",
	    progname);
    return -1;
  }
  if (verbose >= 4)
    memset(buf, 0, pgm->fd.usb.max_xfer);

  rv = serial_recv(&pgm->fd, buf, pgm->fd.usb.max_xfer);

  if (rv < 0) {
    /* timeout in receive */
    avrdude_message(MSG_NOTICE2, "%s: jtag3_recv(): Timeout receiving packet\n",
                      progname);
    free(buf);
    return -1;
  }

  *msg = buf;

  return rv;
}

static int jtag3_edbg_recv_frame(PROGRAMMER * pgm, unsigned char **msg) {
  int rv, len = 0;
  unsigned char *buf = NULL;
  unsigned char *request;

  avrdude_message(MSG_TRACE, "%s: jtag3_edbg_recv():\n", progname);

  if ((buf = malloc(USBDEV_MAX_XFER_3)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_recv(): out of memory\n",
	    progname);
    return -1;
  }
  if ((request = malloc(pgm->fd.usb.max_xfer)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtag3_edbg_recv(): out of memory\n",
	    progname);
    free(buf);
    return -1;
  }

  *msg = buf;

  int nfrags = 0;
  int thisfrag = 0;

  do {
    request[0] = EDBG_VENDOR_AVR_RSP;

    if (serial_send(&pgm->fd, request, pgm->fd.usb.max_xfer) != 0) {
      avrdude_message(MSG_INFO, "%s: jtag3_edbg_recv(): error sending CMSIS-DAP vendor command\n",
                      progname);
      free(request);
      free(*msg);
      return -1;
    }

    rv = serial_recv(&pgm->fd, buf, pgm->fd.usb.max_xfer);

    if (rv < 0) {
      /* timeout in receive */
      avrdude_message(MSG_NOTICE2, "%s: jtag3_edbg_recv(): Timeout receiving packet\n",
                      progname);
      free(*msg);
      free(request);
      return -1;
    }

    if (buf[0] != EDBG_VENDOR_AVR_RSP) {
      avrdude_message(MSG_INFO, "%s: jtag3_edbg_recv(): Unexpected response 0x%02x\n",
                      progname, buf[0]);
      free(*msg);
      free(request);
      return -1;
    }

    /* calculate fragment information */
    if (thisfrag == 0) {
      /* first fragment */
      nfrags = buf[1] & 0x0F;
      thisfrag = 1;
    } else {
      if (nfrags != (buf[1] & 0x0F)) {
        avrdude_message(MSG_INFO,
                        "%s: jtag3_edbg_recv(): "
                        "Inconsistent # of fragments; had %d, now %d\n",
                        progname, nfrags, (buf[1] & 0x0F));
        free(*msg);
        free(request);
        return -1;
      }
    }
    if (thisfrag != ((buf[1] >> 4) & 0x0F)) {
      avrdude_message(MSG_INFO,
                      "%s: jtag3_edbg_recv(): "
                      "Inconsistent fragment number; expect %d, got %d\n",
                      progname, thisfrag, ((buf[1] >> 4) & 0x0F));
      free(*msg);
      free(request);
      return -1;
    }

    int thislen = (buf[2] << 8) | buf[3];
    if (thislen > rv + 4) {
      avrdude_message(MSG_INFO, "%s: jtag3_edbg_recv(): Unexpected length value (%d > %d)\n",
                      progname, thislen, rv + 4);
      thislen = rv + 4;
    }
    if (len + thislen > USBDEV_MAX_XFER_3) {
      avrdude_message(MSG_INFO, "%s: jtag3_edbg_recv(): Length exceeds max size (%d > %d)\n",
                      progname, len + thislen, USBDEV_MAX_XFER_3);
      thislen = USBDEV_MAX_XFER_3 - len;
    }
    memmove(buf, buf + 4, thislen);
    thisfrag++;
    len += thislen;
    buf += thislen;
  } while (thisfrag <= nfrags);

  free(request);
  return len;
}

int jtag3_recv(PROGRAMMER * pgm, unsigned char **msg) {
  unsigned short r_seqno;
  int rv;

  for (;;) {
    if ((rv = jtag3_recv_frame(pgm, msg)) <= 0)
      return rv;

    if ((rv & USB_RECV_FLAG_EVENT) != 0) {
      if (verbose >= 3)
	jtag3_prevent(pgm, *msg, rv & USB_RECV_LENGTH_MASK);

      free(*msg);
      continue;
    }

    rv &= USB_RECV_LENGTH_MASK;
    r_seqno = ((*msg)[2] << 8) | (*msg)[1];
    avrdude_message(MSG_DEBUG, "%s: jtag3_recv(): "
	      "Got message seqno %d (command_sequence == %d)\n",
	      progname, r_seqno, PDATA(pgm)->command_sequence);
    if (r_seqno == PDATA(pgm)->command_sequence) {
      if (++(PDATA(pgm)->command_sequence) == 0xffff)
	PDATA(pgm)->command_sequence = 0;
      /*
       * We move the payload to the beginning of the buffer, to make
       * the job easier for the caller.  We have to return the
       * original pointer though, as the caller must free() it.
       */
      rv -= 3;
      memmove(*msg, *msg + 3, rv);

      return rv;
    }
    avrdude_message(MSG_NOTICE2, "%s: jtag3_recv(): "
	      "got wrong sequence number, %u != %u\n",
	      progname, r_seqno, PDATA(pgm)->command_sequence);

    free(*msg);
  }
}

 int jtag3_command(PROGRAMMER *pgm, unsigned char *cmd, unsigned int cmdlen,
		   unsigned char **resp, const char *descr)
{
  int status;
  unsigned char c;

  avrdude_message(MSG_NOTICE2, "%s: Sending %s command: ",
	    progname, descr);
  jtag3_send(pgm, cmd, cmdlen);

  status = jtag3_recv(pgm, resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_NOTICE2, "%s: %s command: timeout/error communicating with programmer (status %d)\n",
                    progname, descr, status);
    return -1;
  } else if (verbose >= 3) {
    putc('\n', stderr);
    jtag3_prmsg(pgm, *resp, status);
  } else {
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", (*resp)[1], status);
  }

  c = (*resp)[1];
  if ((c & RSP3_STATUS_MASK) != RSP3_OK) {
    avrdude_message(MSG_INFO, "%s: bad response to %s command: 0x%02x\n",
                    progname, descr, c);
    free(*resp);
    resp = 0;
    return -1;
  }

  return status;
}


int jtag3_getsync(PROGRAMMER * pgm, int mode) {

  unsigned char buf[3], *resp;

  avrdude_message(MSG_DEBUG, "%s: jtag3_getsync()\n", progname);

  if (pgm->flag & PGM_FL_IS_EDBG) {
    if (jtag3_edbg_prepare(pgm) < 0)
      return -1;
  }

  /* Get the sign-on information. */
  buf[0] = SCOPE_GENERAL;
  buf[1] = CMD3_SIGN_ON;
  buf[2] = 0;

  if (jtag3_command(pgm, buf, 3, &resp, "sign-on") < 0)
    return -1;

  free(resp);

  return 0;
}

/*
 * issue the 'chip erase' command to the AVR device
 */
static int jtag3_chip_erase(PROGRAMMER * pgm, AVRPART * p)
{
  unsigned char buf[8], *resp;

  buf[0] = SCOPE_AVR;
  buf[1] = CMD3_ERASE_MEMORY;
  buf[2] = 0;
  buf[3] = XMEGA_ERASE_CHIP;
  buf[4] = buf[5] = buf[6] = buf[7] = 0; /* page address */

  if (jtag3_command(pgm, buf, 8, &resp, "chip erase") < 0)
    return -1;

  free(resp);
  return 0;
}

/*
 * There is no chip erase functionality in debugWire mode.
 */
static int jtag3_chip_erase_dw(PROGRAMMER * pgm, AVRPART * p)
{

  avrdude_message(MSG_INFO, "%s: Chip erase not supported in debugWire mode\n",
	  progname);

  return 0;
}

static int jtag3_program_enable_dummy(PROGRAMMER * pgm, AVRPART * p)
{
  return 0;
}

static int jtag3_program_enable(PROGRAMMER * pgm)
{
  unsigned char buf[3], *resp;

  if (PDATA(pgm)->prog_enabled)
    return 0;

  buf[0] = SCOPE_AVR;
  buf[1] = CMD3_ENTER_PROGMODE;
  buf[2] = 0;

  if (jtag3_command(pgm, buf, 3, &resp, "enter progmode") >= 0) {
    free(resp);
    PDATA(pgm)->prog_enabled = 1;

    return 0;
  }

  return -1;
}

static int jtag3_program_disable(PROGRAMMER * pgm)
{
  unsigned char buf[3], *resp;

  if (!PDATA(pgm)->prog_enabled)
    return 0;

  buf[0] = SCOPE_AVR;
  buf[1] = CMD3_LEAVE_PROGMODE;
  buf[2] = 0;

  if (jtag3_command(pgm, buf, 3, &resp, "leave progmode") < 0)
    return -1;

  free(resp);

  PDATA(pgm)->prog_enabled = 0;

  return 0;
}

static int jtag3_set_sck_xmega_pdi(PROGRAMMER *pgm, unsigned char *clk)
{
    return jtag3_setparm(pgm, SCOPE_AVR, 1, PARM3_CLK_XMEGA_PDI, clk, 2);
}

static int jtag3_set_sck_xmega_jtag(PROGRAMMER *pgm, unsigned char *clk)
{
    return jtag3_setparm(pgm, SCOPE_AVR, 1, PARM3_CLK_XMEGA_JTAG, clk, 2);
}

static int jtag3_set_sck_mega_jtag(PROGRAMMER *pgm, unsigned char *clk)
{
    return jtag3_setparm(pgm, SCOPE_AVR, 1, PARM3_CLK_MEGA_PROG, clk, 2);
}


/*
 * initialize the AVR device and prepare it to accept commands
 */
static int jtag3_initialize(PROGRAMMER * pgm, AVRPART * p)
{
  unsigned char conn = 0, parm[4];
  const char *ifname;
  unsigned char cmd[4], *resp;
  int status;

  /*
   * At least, as of firmware 2.12, the JTAGICE3 doesn't handle
   * splitting packets correctly.  On a large transfer, the first
   * split packets are correct, but remaining packets contain just
   * garbage.
   *
   * We move the check here so in case future firmware versions fix
   * this, the check below can be made dependended on the actual
   * firmware level.  Retrieving the firmware version can always be
   * accomplished with USB 1.1 (64 byte max) packets.
   *
   * Allow to override the check by -F (so users could try on newer
   * firmware), but warn loudly.
   */
  if (jtag3_getparm(pgm, SCOPE_GENERAL, 0, PARM3_FW_MAJOR, parm, 2) < 0)
    return -1;
  if (pgm->fd.usb.max_xfer < USBDEV_MAX_XFER_3 && (pgm->flag & PGM_FL_IS_EDBG) == 0) {
    avrdude_message(MSG_INFO, "%s: the JTAGICE3's firmware %d.%d is broken on USB 1.1 connections, sorry\n",
                    progname, parm[0], parm[1]);
    if (ovsigck) {
      avrdude_message(MSG_INFO, "%s: forced to continue by option -F; THIS PUTS THE DEVICE'S DATA INTEGRITY AT RISK!\n",
                      progname);
    } else {
      return -1;
    }
  }

  if (pgm->flag & PGM_FL_IS_DW) {
    ifname = "debugWire";
    if (p->flags & AVRPART_HAS_DW)
      conn = PARM3_CONN_DW;
  } else if (pgm->flag & PGM_FL_IS_PDI) {
    ifname = "PDI";
    if (p->flags & AVRPART_HAS_PDI)
      conn = PARM3_CONN_PDI;
  } else {
    ifname = "JTAG";
    if (p->flags & AVRPART_HAS_JTAG)
      conn = PARM3_CONN_JTAG;
  }

  if (conn == 0) {
    avrdude_message(MSG_INFO, "%s: jtag3_initialize(): part %s has no %s interface\n",
	    progname, p->desc, ifname);
    return -1;
  }

  if (p->flags & AVRPART_HAS_PDI)
    parm[0] = PARM3_ARCH_XMEGA;
  else if (p->flags & AVRPART_HAS_DW)
    parm[0] = PARM3_ARCH_TINY;
  else
    parm[0] = PARM3_ARCH_MEGA;
  if (jtag3_setparm(pgm, SCOPE_AVR, 0, PARM3_ARCH, parm, 1) < 0)
    return -1;

  parm[0] = PARM3_SESS_PROGRAMMING;
  if (jtag3_setparm(pgm, SCOPE_AVR, 0, PARM3_SESS_PURPOSE, parm, 1) < 0)
    return -1;

  parm[0] = conn;
  if (jtag3_setparm(pgm, SCOPE_AVR, 1, PARM3_CONNECTION, parm, 1) < 0)
    return -1;

  if (conn == PARM3_CONN_PDI)
    PDATA(pgm)->set_sck = jtag3_set_sck_xmega_pdi;
  else if (conn == PARM3_CONN_JTAG) {
    if (p->flags & AVRPART_HAS_PDI)
      PDATA(pgm)->set_sck = jtag3_set_sck_xmega_jtag;
    else
      PDATA(pgm)->set_sck = jtag3_set_sck_mega_jtag;
  }
  if (pgm->bitclock != 0.0 && PDATA(pgm)->set_sck != NULL)
  {
    unsigned int clock = 1E-3 / pgm->bitclock; /* kHz */
    avrdude_message(MSG_NOTICE2, "%s: jtag3_initialize(): "
	      "trying to set JTAG clock to %u kHz\n",
	      progname, clock);
    parm[0] = clock & 0xff;
    parm[1] = (clock >> 8) & 0xff;
    if (PDATA(pgm)->set_sck(pgm, parm) < 0)
      return -1;
  }

  if (conn == PARM3_CONN_JTAG)
  {
    avrdude_message(MSG_NOTICE2, "%s: jtag3_initialize(): "
	      "trying to set JTAG daisy-chain info to %d,%d,%d,%d\n",
	      progname,
	      PDATA(pgm)->jtagchain[0], PDATA(pgm)->jtagchain[1],
	      PDATA(pgm)->jtagchain[2], PDATA(pgm)->jtagchain[3]);
    if (jtag3_setparm(pgm, SCOPE_AVR, 1, PARM3_JTAGCHAIN, PDATA(pgm)->jtagchain, 4) < 0)
      return -1;
  }

  /* set device descriptor data */
  if ((p->flags & AVRPART_HAS_PDI))
  {
    struct xmega_device_desc xd;
    LNODEID ln;
    AVRMEM * m;

    u16_to_b2(xd.nvm_base_addr, p->nvm_base);
    u16_to_b2(xd.mcu_base_addr, p->mcu_base);

    for (ln = lfirst(p->mem); ln; ln = lnext(ln)) {
      m = ldata(ln);
      if (strcmp(m->desc, "flash") == 0) {
	if (m->readsize != 0 && m->readsize < m->page_size)
	  PDATA(pgm)->flash_pagesize = m->readsize;
	else
	  PDATA(pgm)->flash_pagesize = m->page_size;
	u16_to_b2(xd.flash_page_size, m->page_size);
      } else if (strcmp(m->desc, "eeprom") == 0) {
	PDATA(pgm)->eeprom_pagesize = m->page_size;
	xd.eeprom_page_size = m->page_size;
	u16_to_b2(xd.eeprom_size, m->size);
	u32_to_b4(xd.nvm_eeprom_offset, m->offset);
      } else if (strcmp(m->desc, "application") == 0) {
	u32_to_b4(xd.app_size, m->size);
	u32_to_b4(xd.nvm_app_offset, m->offset);
      } else if (strcmp(m->desc, "boot") == 0) {
	u16_to_b2(xd.boot_size, m->size);
	u32_to_b4(xd.nvm_boot_offset, m->offset);
      } else if (strcmp(m->desc, "fuse1") == 0) {
	u32_to_b4(xd.nvm_fuse_offset, m->offset & ~7);
      } else if (strncmp(m->desc, "lock", 4) == 0) {
	u32_to_b4(xd.nvm_lock_offset, m->offset);
      } else if (strcmp(m->desc, "usersig") == 0) {
	u32_to_b4(xd.nvm_user_sig_offset, m->offset);
      } else if (strcmp(m->desc, "prodsig") == 0) {
	u32_to_b4(xd.nvm_prod_sig_offset, m->offset);
      } else if (strcmp(m->desc, "data") == 0) {
	u32_to_b4(xd.nvm_data_offset, m->offset);
      }
    }

    if (jtag3_setparm(pgm, SCOPE_AVR, 2, PARM3_DEVICEDESC, (unsigned char *)&xd, sizeof xd) < 0)
      return -1;
  }
  else
  {
    struct mega_device_desc md;
    LNODEID ln;
    AVRMEM * m;
    unsigned int flashsize = 0;

    memset(&md, 0, sizeof md);

    for (ln = lfirst(p->mem); ln; ln = lnext(ln)) {
      m = ldata(ln);
      if (strcmp(m->desc, "flash") == 0) {
	if (m->readsize != 0 && m->readsize < m->page_size)
	  PDATA(pgm)->flash_pagesize = m->readsize;
	else
	  PDATA(pgm)->flash_pagesize = m->page_size;
	u16_to_b2(md.flash_page_size, m->page_size);
	u32_to_b4(md.flash_size, (flashsize = m->size));
	// do we need it?  just a wild guess
	u32_to_b4(md.boot_address, (m->size - m->page_size * 4) / 2);
      } else if (strcmp(m->desc, "eeprom") == 0) {
	PDATA(pgm)->eeprom_pagesize = m->page_size;
	md.eeprom_page_size = m->page_size;
	u16_to_b2(md.eeprom_size, m->size);
      }
    }

    //md.sram_offset[2] = p->sram;  // do we need it?
    if (p->ocdrev == -1) {
      int ocdrev;

      /* lacking a proper definition, guess the OCD revision */
      if (p->flags & AVRPART_HAS_DW)
	ocdrev = 1;		/* exception: ATtiny13, 2313, 4313 */
      else if (flashsize > 128 * 1024)
	ocdrev = 4;
      else
	ocdrev = 3;		/* many exceptions from that, actually */
      avrdude_message(MSG_INFO, "%s: part definition for %s lacks \"ocdrev\"; guessing %d\n",
                      progname, p->desc, ocdrev);
      md.ocd_revision = ocdrev;
    } else {
      md.ocd_revision = p->ocdrev;
    }
    md.always_one = 1;
    md.allow_full_page_bitstream = (p->flags & AVRPART_ALLOWFULLPAGEBITSTREAM) != 0;
    md.idr_address = p->idr;

    if (p->eecr == 0)
      p->eecr = 0x3f;		/* matches most "modern" mega/tiny AVRs */
    md.eearh_address = p->eecr - 0x20 + 3;
    md.eearl_address = p->eecr - 0x20 + 2;
    md.eecr_address = p->eecr - 0x20;
    md.eedr_address = p->eecr - 0x20 + 1;
    md.spmcr_address = p->spmcr;
    //md.osccal_address = p->osccal;  // do we need it at all?

    if (jtag3_setparm(pgm, SCOPE_AVR, 2, PARM3_DEVICEDESC, (unsigned char *)&md, sizeof md) < 0)
      return -1;
  }

  int use_ext_reset;

  for (use_ext_reset = 0; use_ext_reset <= 1; use_ext_reset++) {
    cmd[0] = SCOPE_AVR;
    cmd[1] = CMD3_SIGN_ON;
    cmd[2] = 0;
    cmd[3] = use_ext_reset;			/* external reset */

    if ((status = jtag3_command(pgm, cmd, 4, &resp, "AVR sign-on")) >= 0)
      break;

    avrdude_message(MSG_INFO, "%s: retrying with external reset applied\n",
		    progname);
  }

  if (use_ext_reset > 1) {
    avrdude_message(MSG_INFO, "%s: JTAGEN fuse disabled?\n", progname);
    return -1;
  }

  /*
   * Depending on the target connection, there are two different
   * possible replies of the ICE.  For a JTAG connection, the reply
   * format is RSP3_DATA, followed by 4 bytes of the JTAG ID read from
   * the device (followed by a trailing 0).  For all other connections
   * (except ISP which is handled completely differently, but that
   * doesn't apply here anyway), the response is just RSP_OK.
   */
  if (resp[1] == RSP3_DATA && status >= 7)
    /* JTAG ID has been returned */
    avrdude_message(MSG_NOTICE, "%s: JTAG ID returned: 0x%02x 0x%02x 0x%02x 0x%02x\n",
	    progname, resp[3], resp[4], resp[5], resp[6]);

  free(resp);

  PDATA(pgm)->boot_start = ULONG_MAX;
  if ((p->flags & AVRPART_HAS_PDI)) {
    /*
     * Find out where the border between application and boot area
     * is.
     */
    AVRMEM *bootmem = avr_locate_mem(p, "boot");
    AVRMEM *flashmem = avr_locate_mem(p, "flash");
    if (bootmem == NULL || flashmem == NULL) {
      avrdude_message(MSG_INFO, "%s: jtagmk3_initialize(): Cannot locate \"flash\" and \"boot\" memories in description\n",
                      progname);
    } else {
      PDATA(pgm)->boot_start = bootmem->offset - flashmem->offset;
    }
  }

  free(PDATA(pgm)->flash_pagecache);
  free(PDATA(pgm)->eeprom_pagecache);
  if ((PDATA(pgm)->flash_pagecache = malloc(PDATA(pgm)->flash_pagesize)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtag3_initialize(): Out of memory\n",
	    progname);
    return -1;
  }
  if ((PDATA(pgm)->eeprom_pagecache = malloc(PDATA(pgm)->eeprom_pagesize)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtag3_initialize(): Out of memory\n",
	    progname);
    free(PDATA(pgm)->flash_pagecache);
    return -1;
  }
  PDATA(pgm)->flash_pageaddr = PDATA(pgm)->eeprom_pageaddr = (unsigned long)-1L;

  return 0;
}

static void jtag3_disable(PROGRAMMER * pgm)
{

  free(PDATA(pgm)->flash_pagecache);
  PDATA(pgm)->flash_pagecache = NULL;
  free(PDATA(pgm)->eeprom_pagecache);
  PDATA(pgm)->eeprom_pagecache = NULL;

  /*
   * jtag3_program_disable() doesn't do anything if the
   * device is currently not in programming mode, so just
   * call it unconditionally here.
   */
  (void)jtag3_program_disable(pgm);
}

static void jtag3_enable(PROGRAMMER * pgm)
{
  return;
}

static int jtag3_parseextparms(PROGRAMMER * pgm, LISTID extparms)
{
  LNODEID ln;
  const char *extended_param;
  int rv = 0;

  for (ln = lfirst(extparms); ln; ln = lnext(ln)) {
    extended_param = ldata(ln);

    if (strncmp(extended_param, "jtagchain=", strlen("jtagchain=")) == 0) {
      unsigned int ub, ua, bb, ba;
      if (sscanf(extended_param, "jtagchain=%u,%u,%u,%u", &ub, &ua, &bb, &ba)
          != 4) {
        avrdude_message(MSG_INFO, "%s: jtag3_parseextparms(): invalid JTAG chain '%s'\n",
                        progname, extended_param);
        rv = -1;
        continue;
      }
      avrdude_message(MSG_NOTICE2, "%s: jtag3_parseextparms(): JTAG chain parsed as:\n"
                        "%s %u units before, %u units after, %u bits before, %u bits after\n",
                        progname,
                        progbuf, ub, ua, bb, ba);
      PDATA(pgm)->jtagchain[0] = ub;
      PDATA(pgm)->jtagchain[1] = ua;
      PDATA(pgm)->jtagchain[2] = bb;
      PDATA(pgm)->jtagchain[3] = ba;

      continue;
    }

    avrdude_message(MSG_INFO, "%s: jtag3_parseextparms(): invalid extended parameter '%s'\n",
                    progname, extended_param);
    rv = -1;
  }

  return rv;
}

int jtag3_open_common(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;
  LNODEID usbpid;
  int rv = -1;

#if !defined(HAVE_LIBUSB) && !defined(HAVE_LIBHIDAPI)
  avrdude_message(MSG_INFO, "avrdude was compiled without USB or HIDAPI support.\n");
  return -1;
#endif

  if (strncmp(port, "usb", 3) != 0) {
    avrdude_message(MSG_INFO, "%s: jtag3_open_common(): JTAGICE3/EDBG port names must start with \"usb\"\n",
                    progname);
    return -1;
  }

  if (pgm->usbvid)
    pinfo.usbinfo.vid = pgm->usbvid;
  else
    pinfo.usbinfo.vid = USB_VENDOR_ATMEL;

  /* If the config entry did not specify a USB PID, insert the default one. */
  if (lfirst(pgm->usbpid) == NULL)
    ladd(pgm->usbpid, (void *)USB_DEVICE_JTAGICE3);

#if defined(HAVE_LIBHIDAPI)
  /*
   * Try HIDAPI first.  LibUSB is more generic, but might then cause
   * troubles for HID-class devices in some OSes (like Windows).
   */
  serdev = &usbhid_serdev;
  for (usbpid = lfirst(pgm->usbpid); rv < 0 && usbpid != NULL; usbpid = lnext(usbpid)) {
    pinfo.usbinfo.flags = PINFO_FL_SILENT;
    pinfo.usbinfo.pid = *(int *)(ldata(usbpid));
    pgm->fd.usb.max_xfer = USBDEV_MAX_XFER_3;
    pgm->fd.usb.rep = USBDEV_BULK_EP_READ_3;
    pgm->fd.usb.wep = USBDEV_BULK_EP_WRITE_3;
    pgm->fd.usb.eep = 0;

    strcpy(pgm->port, port);
    rv = serial_open(port, pinfo, &pgm->fd);
  }
  if (rv < 0) {
#endif	/* HAVE_LIBHIDAPI */
#if defined(HAVE_LIBUSB)
    serdev = &usb_serdev_frame;
    for (usbpid = lfirst(pgm->usbpid); rv < 0 && usbpid != NULL; usbpid = lnext(usbpid)) {
      pinfo.usbinfo.flags = PINFO_FL_SILENT;
      pinfo.usbinfo.pid = *(int *)(ldata(usbpid));
      pgm->fd.usb.max_xfer = USBDEV_MAX_XFER_3;
      pgm->fd.usb.rep = USBDEV_BULK_EP_READ_3;
      pgm->fd.usb.wep = USBDEV_BULK_EP_WRITE_3;
      pgm->fd.usb.eep = USBDEV_EVT_EP_READ_3;

      strcpy(pgm->port, port);
      rv = serial_open(port, pinfo, &pgm->fd);
    }
#endif	/* HAVE_LIBUSB */
#if defined(HAVE_LIBHIDAPI)
  }
#endif
  if (rv < 0) {
    avrdude_message(MSG_INFO, "%s: jtag3_open_common(): Did not find any device matching VID 0x%04x and PID list: ",
                    progname, (unsigned)pinfo.usbinfo.vid);
    int notfirst = 0;
    for (usbpid = lfirst(pgm->usbpid); usbpid != NULL; usbpid = lnext(usbpid)) {
      if (notfirst)
        avrdude_message(MSG_INFO, ", ");
      avrdude_message(MSG_INFO, "0x%04x", (unsigned int)(*(int *)(ldata(usbpid))));
      notfirst = 1;
    }
    fputc('\n', stderr);

    return -1;
  }

  if (pgm->fd.usb.eep == 0)
  {
    /* The event EP has been deleted by usb_open(), so we are
       running on a CMSIS-DAP device, using EDBG protocol */
    pgm->flag |= PGM_FL_IS_EDBG;
    avrdude_message(MSG_NOTICE, "%s: Found CMSIS-DAP compliant device, using EDBG protocol\n",
                      progname);
  }

  /*
   * drain any extraneous input
   */
  jtag3_drain(pgm, 0);

  return 0;
}



static int jtag3_open(PROGRAMMER * pgm, char * port)
{
  avrdude_message(MSG_NOTICE2, "%s: jtag3_open()\n", progname);

  if (jtag3_open_common(pgm, port) < 0)
    return -1;

  if (jtag3_getsync(pgm, PARM3_CONN_JTAG) < 0)
    return -1;

  return 0;
}

static int jtag3_open_dw(PROGRAMMER * pgm, char * port)
{
  avrdude_message(MSG_NOTICE2, "%s: jtag3_open_dw()\n", progname);

  if (jtag3_open_common(pgm, port) < 0)
    return -1;

  if (jtag3_getsync(pgm, PARM3_CONN_DW) < 0)
    return -1;

  return 0;
}

static int jtag3_open_pdi(PROGRAMMER * pgm, char * port)
{
  avrdude_message(MSG_NOTICE2, "%s: jtag3_open_pdi()\n", progname);

  if (jtag3_open_common(pgm, port) < 0)
    return -1;

  if (jtag3_getsync(pgm, PARM3_CONN_PDI) < 0)
    return -1;

  return 0;
}


void jtag3_close(PROGRAMMER * pgm)
{
  unsigned char buf[4], *resp;

  avrdude_message(MSG_NOTICE2, "%s: jtag3_close()\n", progname);

  buf[0] = SCOPE_AVR;
  buf[1] = CMD3_SIGN_OFF;
  buf[2] = buf[3] = 0;

  if (jtag3_command(pgm, buf, 3, &resp, "AVR sign-off") >= 0)
    free(resp);

  buf[0] = SCOPE_GENERAL;
  buf[1] = CMD3_SIGN_OFF;

  if (jtag3_command(pgm, buf, 4, &resp, "sign-off") >= 0)
    free(resp);

  if (pgm->flag & PGM_FL_IS_EDBG)
    jtag3_edbg_signoff(pgm);

  serial_close(&pgm->fd);
  pgm->fd.ifd = -1;
}

static int jtag3_page_erase(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                               unsigned int addr)
{
  unsigned char cmd[8], *resp;

  avrdude_message(MSG_NOTICE2, "%s: jtag3_page_erase(.., %s, 0x%x)\n",
	    progname, m->desc, addr);

  if (!(p->flags & AVRPART_HAS_PDI)) {
    avrdude_message(MSG_INFO, "%s: jtag3_page_erase: not an Xmega device\n",
	    progname);
    return -1;
  }

  if (jtag3_program_enable(pgm) < 0)
    return -1;

  cmd[0] = SCOPE_AVR;
  cmd[1] = CMD3_ERASE_MEMORY;
  cmd[2] = 0;

  if (strcmp(m->desc, "flash") == 0) {
    if (jtag3_memtype(pgm, p, addr) == MTYPE_FLASH)
      cmd[3] = XMEGA_ERASE_APP_PAGE;
    else
      cmd[3] = XMEGA_ERASE_BOOT_PAGE;
  } else if (strcmp(m->desc, "eeprom") == 0) {
    cmd[3] = XMEGA_ERASE_EEPROM_PAGE;
  } else if ( ( strcmp(m->desc, "usersig") == 0 ) ) {
    cmd[3] = XMEGA_ERASE_USERSIG;
  } else if ( ( strcmp(m->desc, "boot") == 0 ) ) {
    cmd[3] = XMEGA_ERASE_BOOT_PAGE;
  } else {
    cmd[3] = XMEGA_ERASE_APP_PAGE;
  }

  u32_to_b4(cmd + 4, addr + m->offset);

  if (jtag3_command(pgm, cmd, 8, &resp, "page erase") < 0)
    return -1;

  free(resp);
  return 0;
}

static int jtag3_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                unsigned int page_size,
                                unsigned int addr, unsigned int n_bytes)
{
  unsigned int block_size;
  unsigned int maxaddr = addr + n_bytes;
  unsigned char *cmd;
  unsigned char *resp;
  int status, dynamic_memtype = 0;
  long otimeout = serial_recv_timeout;

  avrdude_message(MSG_NOTICE2, "%s: jtag3_paged_write(.., %s, %d, %d)\n",
	    progname, m->desc, page_size, n_bytes);

  if (!(pgm->flag & PGM_FL_IS_DW) && jtag3_program_enable(pgm) < 0)
    return -1;

  if (page_size == 0) page_size = 256;

  if ((cmd = malloc(page_size + 13)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtag3_paged_write(): Out of memory\n",
	    progname);
    return -1;
  }

  cmd[0] = SCOPE_AVR;
  cmd[1] = CMD3_WRITE_MEMORY;
  cmd[2] = 0;
  if (strcmp(m->desc, "flash") == 0) {
    PDATA(pgm)->flash_pageaddr = (unsigned long)-1L;
    cmd[3] = jtag3_memtype(pgm, p, addr);
    if (p->flags & AVRPART_HAS_PDI)
      /* dynamically decide between flash/boot memtype */
      dynamic_memtype = 1;
  } else if (strcmp(m->desc, "eeprom") == 0) {
    if (pgm->flag & PGM_FL_IS_DW) {
      /*
       * jtag3_paged_write() to EEPROM attempted while in
       * DW mode.  Use jtag3_write_byte() instead.
       */
      for (; addr < maxaddr; addr++) {
	status = jtag3_write_byte(pgm, p, m, addr, m->buf[addr]);
	if (status < 0) {
	  free(cmd);
	  return -1;
	}
      }
      free(cmd);
      return n_bytes;
    }
    cmd[3] = ( p->flags & AVRPART_HAS_PDI ) ? MTYPE_EEPROM_XMEGA : MTYPE_EEPROM_PAGE;
    PDATA(pgm)->eeprom_pageaddr = (unsigned long)-1L;
  } else if ( ( strcmp(m->desc, "usersig") == 0 ) ) {
    cmd[3] = MTYPE_USERSIG;
  } else if ( ( strcmp(m->desc, "boot") == 0 ) ) {
    cmd[3] = MTYPE_BOOT_FLASH;
  } else if ( p->flags & AVRPART_HAS_PDI ) {
    cmd[3] = MTYPE_FLASH;
  } else {
    cmd[3] = MTYPE_SPM;
  }
  serial_recv_timeout = 100;
  for (; addr < maxaddr; addr += page_size) {
    if ((maxaddr - addr) < page_size)
      block_size = maxaddr - addr;
    else
      block_size = page_size;
    avrdude_message(MSG_DEBUG, "%s: jtag3_paged_write(): "
	      "block_size at addr %d is %d\n",
	      progname, addr, block_size);

    if (dynamic_memtype)
      cmd[3] = jtag3_memtype(pgm, p, addr);

    u32_to_b4(cmd + 8, page_size);
    u32_to_b4(cmd + 4, jtag3_memaddr(pgm, p, m, addr));
    cmd[12] = 0;

    /*
     * The JTAG ICE will refuse to write anything but a full page, at
     * least for the flash ROM.  If a partial page has been requested,
     * set the remainder to 0xff.  (Maybe we should rather read back
     * the existing contents instead before?  Doesn't matter much, as
     * bits cannot be written to 1 anyway.)
     */
    memset(cmd + 13, 0xff, page_size);
    memcpy(cmd + 13, m->buf + addr, block_size);

    if ((status = jtag3_command(pgm, cmd, page_size + 13,
				&resp, "write memory")) < 0) {
      free(cmd);
      serial_recv_timeout = otimeout;
      return -1;
    }

    free(resp);
  }

  free(cmd);
  serial_recv_timeout = otimeout;

  return n_bytes;
}

static int jtag3_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                               unsigned int page_size,
                               unsigned int addr, unsigned int n_bytes)
{
  unsigned int block_size;
  unsigned int maxaddr = addr + n_bytes;
  unsigned char cmd[12];
  unsigned char *resp;
  int status, dynamic_memtype = 0;
  long otimeout = serial_recv_timeout;

  avrdude_message(MSG_NOTICE2, "%s: jtag3_paged_load(.., %s, %d, %d)\n",
	    progname, m->desc, page_size, n_bytes);

  if (!(pgm->flag & PGM_FL_IS_DW) && jtag3_program_enable(pgm) < 0)
    return -1;

  page_size = m->readsize;

  cmd[0] = SCOPE_AVR;
  cmd[1] = CMD3_READ_MEMORY;
  cmd[2] = 0;

  if (strcmp(m->desc, "flash") == 0) {
    cmd[3] = jtag3_memtype(pgm, p, addr);
    if (p->flags & AVRPART_HAS_PDI)
      /* dynamically decide between flash/boot memtype */
      dynamic_memtype = 1;
  } else if (strcmp(m->desc, "eeprom") == 0) {
    cmd[3] = ( p->flags & AVRPART_HAS_PDI ) ? MTYPE_EEPROM : MTYPE_EEPROM_PAGE;
    if (pgm->flag & PGM_FL_IS_DW)
      return -1;
  } else if ( ( strcmp(m->desc, "prodsig") == 0 ) ) {
    cmd[3] = MTYPE_PRODSIG;
  } else if ( ( strcmp(m->desc, "usersig") == 0 ) ) {
    cmd[3] = MTYPE_USERSIG;
  } else if ( ( strcmp(m->desc, "boot") == 0 ) ) {
    cmd[3] = MTYPE_BOOT_FLASH;
  } else if ( p->flags & AVRPART_HAS_PDI ) {
    cmd[3] = MTYPE_FLASH;
  } else {
    cmd[3] = MTYPE_SPM;
  }
  serial_recv_timeout = 100;
  for (; addr < maxaddr; addr += page_size) {
    if ((maxaddr - addr) < page_size)
      block_size = maxaddr - addr;
    else
      block_size = page_size;
    avrdude_message(MSG_DEBUG, "%s: jtag3_paged_load(): "
	      "block_size at addr %d is %d\n",
	      progname, addr, block_size);

    if (dynamic_memtype)
      cmd[3] = jtag3_memtype(pgm, p, addr);

    u32_to_b4(cmd + 8, block_size);
    u32_to_b4(cmd + 4, jtag3_memaddr(pgm, p, m, addr));

    if ((status = jtag3_command(pgm, cmd, 12, &resp, "read memory")) < 0)
      return -1;

    if (resp[1] != RSP3_DATA ||
	status < block_size + 4) {
      avrdude_message(MSG_INFO, "%s: wrong/short reply to read memory command\n",
	      progname);
      serial_recv_timeout = otimeout;
      free(resp);
      return -1;
    }
    memcpy(m->buf + addr, resp + 3, status - 4);
    free(resp);
  }
  serial_recv_timeout = otimeout;

  return n_bytes;
}

static int jtag3_read_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
			      unsigned long addr, unsigned char * value)
{
  unsigned char cmd[12];
  unsigned char *resp, *cache_ptr = NULL;
  int status, unsupp = 0;
  unsigned long paddr = 0UL, *paddr_ptr = NULL;
  unsigned int pagesize = 0;

  avrdude_message(MSG_NOTICE2, "%s: jtag3_read_byte(.., %s, 0x%lx, ...)\n",
	    progname, mem->desc, addr);

  if (!(pgm->flag & PGM_FL_IS_DW) && jtag3_program_enable(pgm) < 0)
    return -1;

  cmd[0] = SCOPE_AVR;
  cmd[1] = CMD3_READ_MEMORY;
  cmd[2] = 0;

  cmd[3] = ( p->flags & AVRPART_HAS_PDI ) ? MTYPE_FLASH : MTYPE_FLASH_PAGE;
  if (strcmp(mem->desc, "flash") == 0 ||
      strcmp(mem->desc, "application") == 0 ||
      strcmp(mem->desc, "apptable") == 0 ||
      strcmp(mem->desc, "boot") == 0) {
    addr += mem->offset & (512 * 1024 - 1); /* max 512 KiB flash */
    pagesize = PDATA(pgm)->flash_pagesize;
    paddr = addr & ~(pagesize - 1);
    paddr_ptr = &PDATA(pgm)->flash_pageaddr;
    cache_ptr = PDATA(pgm)->flash_pagecache;
  } else if (strcmp(mem->desc, "eeprom") == 0) {
    if ( (pgm->flag & PGM_FL_IS_DW) || ( p->flags & AVRPART_HAS_PDI ) ) {
      cmd[3] = MTYPE_EEPROM;
    } else {
      cmd[3] = MTYPE_EEPROM_PAGE;
    }
    pagesize = mem->page_size;
    paddr = addr & ~(pagesize - 1);
    paddr_ptr = &PDATA(pgm)->eeprom_pageaddr;
    cache_ptr = PDATA(pgm)->eeprom_pagecache;
  } else if (strcmp(mem->desc, "lfuse") == 0) {
    cmd[3] = MTYPE_FUSE_BITS;
    addr = 0;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "hfuse") == 0) {
    cmd[3] = MTYPE_FUSE_BITS;
    addr = 1;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "efuse") == 0) {
    cmd[3] = MTYPE_FUSE_BITS;
    addr = 2;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strncmp(mem->desc, "lock", 4) == 0) {
    cmd[3] = MTYPE_LOCK_BITS;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strncmp(mem->desc, "fuse", strlen("fuse")) == 0) {
    cmd[3] = MTYPE_FUSE_BITS;
    addr = mem->offset & 7;
  } else if (strcmp(mem->desc, "usersig") == 0) {
    cmd[3] = MTYPE_USERSIG;
  } else if (strcmp(mem->desc, "prodsig") == 0) {
    cmd[3] = MTYPE_PRODSIG;
  } else if (strcmp(mem->desc, "calibration") == 0) {
    cmd[3] = MTYPE_OSCCAL_BYTE;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "signature") == 0) {
    static unsigned char signature_cache[2];

    cmd[3] = MTYPE_SIGN_JTAG;

    /*
     * dW can read out the signature on JTAGICE3, but only allows
     * for a full three-byte read.  We cache them in a local
     * variable to avoid multiple reads.  This optimization does not
     * harm for other connection types either.
     */
    u32_to_b4(cmd + 8, 3);
    u32_to_b4(cmd + 4, 0);

    if (addr == 0) {
      if ((status = jtag3_command(pgm, cmd, 12, &resp, "read memory")) < 0)
	return -1;

      signature_cache[0] = resp[4];
      signature_cache[1] = resp[5];
      *value = resp[3];
      free(resp);
      return 0;
    } else if (addr <= 2) {
      *value = signature_cache[addr - 1];
      return 0;
    } else {
      /* should not happen */
      avrdude_message(MSG_INFO, "address out of range for signature memory: %lu\n", addr);
      return -1;
    }
  }

  /*
   * If the respective memory area is not supported under debugWire,
   * leave here.
   */
  if (unsupp) {
    *value = 42;
    return -1;
  }

  /*
   * To improve the read speed, we used paged reads for flash and
   * EEPROM, and cache the results in a page cache.
   *
   * Page cache validation is based on "{flash,eeprom}_pageaddr"
   * (holding the base address of the most recent cache fill
   * operation).  This variable is set to (unsigned long)-1L when the
   * cache needs to be invalidated.
   */
  if (pagesize && paddr == *paddr_ptr) {
    *value = cache_ptr[addr & (pagesize - 1)];
    return 0;
  }

  if (pagesize) {
    u32_to_b4(cmd + 8, pagesize);
    u32_to_b4(cmd + 4, paddr);
  } else {
    u32_to_b4(cmd + 8, 1);
    u32_to_b4(cmd + 4, addr);
  }

  if ((status = jtag3_command(pgm, cmd, 12, &resp, "read memory")) < 0)
    return -1;

  if (resp[1] != RSP3_DATA ||
      status < (pagesize? pagesize: 1) + 4) {
    avrdude_message(MSG_INFO, "%s: wrong/short reply to read memory command\n",
	    progname);
    free(resp);
    return -1;
  }

  if (pagesize) {
    *paddr_ptr = paddr;
    memcpy(cache_ptr, resp + 3, pagesize);
    *value = cache_ptr[addr & (pagesize - 1)];
  } else
    *value = resp[3];

  free(resp);
  return 0;
}

static int jtag3_write_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
			       unsigned long addr, unsigned char data)
{
  unsigned char cmd[14];
  unsigned char *resp;
  unsigned char *cache_ptr = 0;
  int status, unsupp = 0;
  unsigned int pagesize = 0;

  avrdude_message(MSG_NOTICE2, "%s: jtag3_write_byte(.., %s, 0x%lx, ...)\n",
	    progname, mem->desc, addr);

  cmd[0] = SCOPE_AVR;
  cmd[1] = CMD3_WRITE_MEMORY;
  cmd[2] = 0;
  cmd[3] = ( p->flags & AVRPART_HAS_PDI ) ? MTYPE_FLASH : MTYPE_SPM;
  if (strcmp(mem->desc, "flash") == 0) {
     cache_ptr = PDATA(pgm)->flash_pagecache;
     pagesize = PDATA(pgm)->flash_pagesize;
     PDATA(pgm)->flash_pageaddr = (unsigned long)-1L;
     if (pgm->flag & PGM_FL_IS_DW)
       unsupp = 1;
  } else if (strcmp(mem->desc, "eeprom") == 0) {
    if (pgm->flag & PGM_FL_IS_DW) {
      cmd[3] = MTYPE_EEPROM;
    } else {
      cache_ptr = PDATA(pgm)->eeprom_pagecache;
      pagesize = PDATA(pgm)->eeprom_pagesize;
    }
    PDATA(pgm)->eeprom_pageaddr = (unsigned long)-1L;
  } else if (strcmp(mem->desc, "lfuse") == 0) {
    cmd[3] = MTYPE_FUSE_BITS;
    addr = 0;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "hfuse") == 0) {
    cmd[3] = MTYPE_FUSE_BITS;
    addr = 1;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "efuse") == 0) {
    cmd[3] = MTYPE_FUSE_BITS;
    addr = 2;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strncmp(mem->desc, "fuse", strlen("fuse")) == 0) {
    cmd[3] = MTYPE_FUSE_BITS;
    addr = mem->offset & 7;
  } else if (strcmp(mem->desc, "usersig") == 0) {
    cmd[3] = MTYPE_USERSIG;
  } else if (strcmp(mem->desc, "prodsig") == 0) {
    cmd[3] = MTYPE_PRODSIG;
  } else if (strncmp(mem->desc, "lock", 4) == 0) {
    cmd[3] = MTYPE_LOCK_BITS;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "calibration") == 0) {
    cmd[3] = MTYPE_OSCCAL_BYTE;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "signature") == 0) {
    cmd[3] = MTYPE_SIGN_JTAG;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  }

  if (unsupp)
    return -1;

  if (pagesize != 0) {
    /* flash or EEPROM write: use paged algorithm */
    unsigned char dummy;
    int i;

    /* step #1: ensure the page cache is up to date */
    if (jtag3_read_byte(pgm, p, mem, addr, &dummy) < 0)
      return -1;
    /* step #2: update our value in page cache, and copy
     * cache to mem->buf */
    cache_ptr[addr & (pagesize - 1)] = data;
    addr &= ~(pagesize - 1);	/* page base address */
    memcpy(mem->buf + addr, cache_ptr, pagesize);
    /* step #3: write back */
    i = jtag3_paged_write(pgm, p, mem, pagesize, addr, pagesize);
    if (i < 0)
      return -1;
    else
      return 0;
  }

  /* non-paged writes go here */
  if (!(pgm->flag & PGM_FL_IS_DW) && jtag3_program_enable(pgm) < 0)
    return -1;

  u32_to_b4(cmd + 8, 1);
  u32_to_b4(cmd + 4, addr);
  cmd[12] = 0;
  cmd[13] = data;

  if ((status = jtag3_command(pgm, cmd, 14, &resp, "write memory")) < 0)
    return -1;

  free(resp);

  return 0;
}


/*
 * Set the JTAG clock.  The actual frequency is quite a bit of
 * guesswork, based on the values claimed by AVR Studio.  Inside the
 * JTAG ICE, the value is the delay count of a delay loop between the
 * JTAG clock edges.  A count of 0 bypasses the delay loop.
 *
 * As the STK500 expresses it as a period length (and we actualy do
 * program a period length as well), we rather call it by that name.
 */
static int jtag3_set_sck_period(PROGRAMMER * pgm, double v)
{
  unsigned char parm[2];
  unsigned int clock = 1E-3 / v; /* kHz */

  parm[0] = clock & 0xff;
  parm[1] = (clock >> 8) & 0xff;

  if (PDATA(pgm)->set_sck == NULL) {
    avrdude_message(MSG_INFO, "%s: No backend to set the SCK period for\n",
	    progname);
    return -1;
  }

  return (PDATA(pgm)->set_sck(pgm, parm) < 0)? -1: 0;
}


/*
 * Read (an) emulator parameter(s).
 */
int jtag3_getparm(PROGRAMMER * pgm, unsigned char scope,
		  unsigned char section, unsigned char parm,
		  unsigned char *value, unsigned char length)
{
  int status;
  unsigned char buf[6], *resp, c;
  char descr[60];

  avrdude_message(MSG_NOTICE2, "%s: jtag3_getparm()\n", progname);

  buf[0] = scope;
  buf[1] = CMD3_GET_PARAMETER;
  buf[2] = 0;
  buf[3] = section;
  buf[4] = parm;
  buf[5] = length;

  sprintf(descr, "get parameter (scope 0x%02x, section %d, parm %d)",
	  scope, section, parm);

  if ((status = jtag3_command(pgm, buf, 6, &resp, descr)) < 0)
    return -1;

  c = resp[1];
  if (c != RSP3_DATA || status < 3) {
    avrdude_message(MSG_INFO, "%s: jtag3_getparm(): "
                    "bad response to %s\n",
                    progname, descr);
    free(resp);
    return -1;
  }

  status -= 3;
  memcpy(value, resp + 3, (length < status? length: status));
  free(resp);

  return 0;
}

/*
 * Write an emulator parameter.
 */
int jtag3_setparm(PROGRAMMER * pgm, unsigned char scope,
		  unsigned char section, unsigned char parm,
		  unsigned char *value, unsigned char length)
{
  int status;
  unsigned char *buf, *resp;
  char descr[60];

  avrdude_message(MSG_NOTICE2, "%s: jtag3_setparm()\n", progname);

  sprintf(descr, "set parameter (scope 0x%02x, section %d, parm %d)",
	  scope, section, parm);

  if ((buf = malloc(6 + length)) == NULL)
  {
    avrdude_message(MSG_INFO, "%s: jtag3_setparm(): Out of memory\n",
	    progname);
    return -1;
  }

  buf[0] = scope;
  buf[1] = CMD3_SET_PARAMETER;
  buf[2] = 0;
  buf[3] = section;
  buf[4] = parm;
  buf[5] = length;
  memcpy(buf + 6, value, length);

  status = jtag3_command(pgm, buf, length + 6, &resp, descr);

  free(buf);
  if (status > 0)
    free(resp);

  return status;
}


static void jtag3_display(PROGRAMMER * pgm, const char * p)
{
  unsigned char parms[5];
  unsigned char cmd[4], *resp, c;
  int status;

  /*
   * Ask for:
   *  PARM3_HW_VER (1 byte)
   *  PARM3_FW_MAJOR (1 byte)
   *  PARM3_FW_MINOR (1 byte)
   *  PARM3_FW_RELEASE (2 bytes)
   */
  if (jtag3_getparm(pgm, SCOPE_GENERAL, 0, PARM3_HW_VER, parms, 5) < 0)
    return;

  cmd[0] = SCOPE_INFO;
  cmd[1] = CMD3_GET_INFO;
  cmd[2] = 0;
  cmd[3] = CMD3_INFO_SERIAL;

  if ((status = jtag3_command(pgm, cmd, 4, &resp, "get info (serial number)")) < 0)
    return;

  c = resp[1];
  if (c != RSP3_INFO) {
    avrdude_message(MSG_INFO, "%s: jtag3_display(): response is not RSP3_INFO\n",
                    progname);
    free(resp);
    return;
  }
  memmove(resp, resp + 3, status - 3);
  resp[status - 3] = 0;

  avrdude_message(MSG_INFO, "%sICE hardware version: %d\n", p, parms[0]);
  avrdude_message(MSG_INFO, "%sICE firmware version: %d.%02d (rel. %d)\n", p,
	  parms[1], parms[2],
	  (parms[3] | (parms[4] << 8)));
  avrdude_message(MSG_INFO, "%sSerial number   : %s\n", p, resp);
  free(resp);

  jtag3_print_parms1(pgm, p);
}


static void jtag3_print_parms1(PROGRAMMER * pgm, const char * p)
{
  unsigned char buf[2];

  if (jtag3_getparm(pgm, SCOPE_GENERAL, 1, PARM3_VTARGET, buf, 2) < 0)
    return;

  avrdude_message(MSG_INFO, "%sVtarget         : %.2f V\n", p,
	  b2_to_u16(buf) / 1000.0);

  if (jtag3_getparm(pgm, SCOPE_AVR, 1, PARM3_CLK_MEGA_PROG, buf, 2) < 0)
    return;
  avrdude_message(MSG_INFO, "%sJTAG clock megaAVR/program: %u kHz\n", p,
	  b2_to_u16(buf));

  if (jtag3_getparm(pgm, SCOPE_AVR, 1, PARM3_CLK_MEGA_DEBUG, buf, 2) < 0)
    return;
  avrdude_message(MSG_INFO, "%sJTAG clock megaAVR/debug:   %u kHz\n", p,
	  b2_to_u16(buf));

  if (jtag3_getparm(pgm, SCOPE_AVR, 1, PARM3_CLK_XMEGA_JTAG, buf, 2) < 0)
    return;
  avrdude_message(MSG_INFO, "%sJTAG clock Xmega: %u kHz\n", p,
	  b2_to_u16(buf));

  if (jtag3_getparm(pgm, SCOPE_AVR, 1, PARM3_CLK_XMEGA_PDI, buf, 2) < 0)
    return;
  avrdude_message(MSG_INFO, "%sPDI clock Xmega : %u kHz\n", p,
	  b2_to_u16(buf));
}

static void jtag3_print_parms(PROGRAMMER * pgm)
{
  jtag3_print_parms1(pgm, "");
}

static unsigned char jtag3_memtype(PROGRAMMER * pgm, AVRPART * p, unsigned long addr)
{
  if ( p->flags & AVRPART_HAS_PDI ) {
    if (addr >= PDATA(pgm)->boot_start)
      return MTYPE_BOOT_FLASH;
    else
      return MTYPE_FLASH;
  } else {
    return MTYPE_FLASH_PAGE;
  }
}

static unsigned int jtag3_memaddr(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, unsigned long addr)
{
  if ((p->flags & AVRPART_HAS_PDI) != 0) {
    if (addr >= PDATA(pgm)->boot_start)
      /*
       * all memories but "flash" are smaller than boot_start anyway, so
       * no need for an extra check we are operating on "flash"
       */
      return addr - PDATA(pgm)->boot_start;
    else
      /* normal flash, or anything else */
      return addr;
  }
  /*
   * Non-Xmega device.
   */
  return addr;
}


const char jtag3_desc[] = "Atmel JTAGICE3";

void jtag3_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "JTAGICE3");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtag3_initialize;
  pgm->display        = jtag3_display;
  pgm->enable         = jtag3_enable;
  pgm->disable        = jtag3_disable;
  pgm->program_enable = jtag3_program_enable_dummy;
  pgm->chip_erase     = jtag3_chip_erase;
  pgm->open           = jtag3_open;
  pgm->close          = jtag3_close;
  pgm->read_byte      = jtag3_read_byte;
  pgm->write_byte     = jtag3_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtag3_paged_write;
  pgm->paged_load     = jtag3_paged_load;
  pgm->page_erase     = jtag3_page_erase;
  pgm->print_parms    = jtag3_print_parms;
  pgm->set_sck_period = jtag3_set_sck_period;
  pgm->parseextparams = jtag3_parseextparms;
  pgm->setup          = jtag3_setup;
  pgm->teardown       = jtag3_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_JTAG;
}

const char jtag3_dw_desc[] = "Atmel JTAGICE3 in debugWire mode";

void jtag3_dw_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "JTAGICE3_DW");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtag3_initialize;
  pgm->display        = jtag3_display;
  pgm->enable         = jtag3_enable;
  pgm->disable        = jtag3_disable;
  pgm->program_enable = jtag3_program_enable_dummy;
  pgm->chip_erase     = jtag3_chip_erase_dw;
  pgm->open           = jtag3_open_dw;
  pgm->close          = jtag3_close;
  pgm->read_byte      = jtag3_read_byte;
  pgm->write_byte     = jtag3_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtag3_paged_write;
  pgm->paged_load     = jtag3_paged_load;
  pgm->print_parms    = jtag3_print_parms;
  pgm->setup          = jtag3_setup;
  pgm->teardown       = jtag3_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_DW;
}

const char jtag3_pdi_desc[] = "Atmel JTAGICE3 in PDI mode";

void jtag3_pdi_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "JTAGICE3_PDI");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtag3_initialize;
  pgm->display        = jtag3_display;
  pgm->enable         = jtag3_enable;
  pgm->disable        = jtag3_disable;
  pgm->program_enable = jtag3_program_enable_dummy;
  pgm->chip_erase     = jtag3_chip_erase;
  pgm->open           = jtag3_open_pdi;
  pgm->close          = jtag3_close;
  pgm->read_byte      = jtag3_read_byte;
  pgm->write_byte     = jtag3_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtag3_paged_write;
  pgm->paged_load     = jtag3_paged_load;
  pgm->page_erase     = jtag3_page_erase;
  pgm->print_parms    = jtag3_print_parms;
  pgm->set_sck_period = jtag3_set_sck_period;
  pgm->setup          = jtag3_setup;
  pgm->teardown       = jtag3_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_PDI;
}

