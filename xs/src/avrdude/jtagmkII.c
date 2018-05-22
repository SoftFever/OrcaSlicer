/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2005-2007 Joerg Wunsch <j@uriah.heep.sax.de>
 *
 * Derived from stk500 code which is:
 * Copyright (C) 2002-2004 Brian S. Dean <bsd@bsdhome.com>
 * Copyright (C) 2005 Erik Walthinsen
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
 * avrdude interface for Atmel JTAG ICE mkII programmer
 *
 * The AVR Dragon also uses the same protocol, so it is handled here
 * as well.
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
#include "jtagmkII.h"
#include "jtagmkII_private.h"
#include "usbdevs.h"

/*
 * Private data for this programmer.
 */
struct pdata
{
  unsigned short command_sequence; /* Next cmd seqno to issue. */

  /*
   * See jtagmkII_read_byte() for an explanation of the flash and
   * EEPROM page caches.
   */
  unsigned char *flash_pagecache;
  unsigned long flash_pageaddr;
  unsigned int flash_pagesize;

  unsigned char *eeprom_pagecache;
  unsigned long eeprom_pageaddr;
  unsigned int eeprom_pagesize;

  int prog_enabled;	     /* Cached value of PROGRAMMING status. */
  unsigned char serno[6];	/* JTAG ICE serial number. */

  /* JTAG chain stuff */
  unsigned char jtagchain[4];

  /* The length of the device descriptor is firmware-dependent. */
  size_t device_descriptor_length;

  /* Start address of Xmega boot area */
  unsigned long boot_start;

  /* Major firmware version (needed for Xmega programming) */
  unsigned int fwver;
};

#define PDATA(pgm) ((struct pdata *)(pgm->cookie))

/*
 * The OCDEN fuse is bit 7 of the high fuse (hfuse).  In order to
 * perform memory operations on MTYPE_SPM and MTYPE_EEPROM, OCDEN
 * needs to be programmed.
 *
 * OCDEN should probably rather be defined via the configuration, but
 * if this ever changes to a different fuse byte for one MCU, quite
 * some code here needs to be generalized anyway.
 */
#define OCDEN (1 << 7)

#define RC(x) { x, #x },
static struct {
  unsigned int code;
  const char *descr;
} jtagresults[] = {
  RC(RSP_DEBUGWIRE_SYNC_FAILED)
  RC(RSP_FAILED)
  RC(RSP_ILLEGAL_BREAKPOINT)
  RC(RSP_ILLEGAL_COMMAND)
  RC(RSP_ILLEGAL_EMULATOR_MODE)
  RC(RSP_ILLEGAL_JTAG_ID)
  RC(RSP_ILLEGAL_MCU_STATE)
  RC(RSP_ILLEGAL_MEMORY_TYPE)
  RC(RSP_ILLEGAL_MEMORY_RANGE)
  RC(RSP_ILLEGAL_PARAMETER)
  RC(RSP_ILLEGAL_POWER_STATE)
  RC(RSP_ILLEGAL_VALUE)
  RC(RSP_NO_TARGET_POWER)
  RC(RSP_SET_N_PARAMETERS)
};

/*
 * pgm->flag is marked as "for private use of the programmer".
 * The following defines this programmer's use of that field.
 */
#define PGM_FL_IS_DW		(0x0001)
#define PGM_FL_IS_PDI           (0x0002)
#define PGM_FL_IS_JTAG          (0x0004)

static int jtagmkII_open(PROGRAMMER * pgm, char * port);

static int jtagmkII_initialize(PROGRAMMER * pgm, AVRPART * p);
static int jtagmkII_chip_erase(PROGRAMMER * pgm, AVRPART * p);
static int jtagmkII_read_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
                                unsigned long addr, unsigned char * value);
static int jtagmkII_write_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
                                unsigned long addr, unsigned char data);
static int jtagmkII_reset(PROGRAMMER * pgm, unsigned char flags);
static int jtagmkII_set_sck_period(PROGRAMMER * pgm, double v);
static int jtagmkII_setparm(PROGRAMMER * pgm, unsigned char parm,
                            unsigned char * value);
static void jtagmkII_print_parms1(PROGRAMMER * pgm, const char * p);
static int jtagmkII_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                unsigned int page_size,
                                unsigned int addr, unsigned int n_bytes);
static unsigned char jtagmkII_memtype(PROGRAMMER * pgm, AVRPART * p, unsigned long addr);
static unsigned int jtagmkII_memaddr(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, unsigned long addr);

// AVR32
#define ERROR_SAB 0xFFFFFFFF

static int jtagmkII_open32(PROGRAMMER * pgm, char * port);
static void jtagmkII_close32(PROGRAMMER * pgm);
static int jtagmkII_reset32(PROGRAMMER * pgm, unsigned short flags);
static int jtagmkII_initialize32(PROGRAMMER * pgm, AVRPART * p);
static int jtagmkII_chip_erase32(PROGRAMMER * pgm, AVRPART * p);
static unsigned long jtagmkII_read_SABaddr(PROGRAMMER * pgm, unsigned long addr,
                      unsigned int prefix); // ERROR_SAB illegal
static int jtagmkII_write_SABaddr(PROGRAMMER * pgm, unsigned long addr,
                                  unsigned int prefix, unsigned long val);
static int jtagmkII_avr32_reset(PROGRAMMER * pgm, unsigned char val,
                                  unsigned char ret1, unsigned char ret2);
static int jtagmkII_smc_init32(PROGRAMMER * pgm);
static int jtagmkII_paged_write32(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned int page_size,
                                  unsigned int addr, unsigned int n_bytes);
static int jtagmkII_flash_lock32(PROGRAMMER * pgm, unsigned char lock,
                                  unsigned int page);
static int jtagmkII_flash_erase32(PROGRAMMER * pgm, unsigned int page);
static int jtagmkII_flash_write_page32(PROGRAMMER * pgm, unsigned int page);
static int jtagmkII_flash_clear_pagebuffer32(PROGRAMMER * pgm);
static int jtagmkII_paged_load32(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                 unsigned int page_size,
                                 unsigned int addr, unsigned int n_bytes);

void jtagmkII_setup(PROGRAMMER * pgm)
{
  if ((pgm->cookie = malloc(sizeof(struct pdata))) == 0) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_setup(): Out of memory allocating private data\n",
                    progname);
    exit(1);
  }
  memset(pgm->cookie, 0, sizeof(struct pdata));
}

void jtagmkII_teardown(PROGRAMMER * pgm)
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
static unsigned long
b4_to_u32r(unsigned char *b)
{
  unsigned long l;
  l = b[3];
  l += (unsigned)b[2] << 8;
  l += (unsigned)b[1] << 16;
  l += (unsigned)b[0] << 24;

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
static void
u32_to_b4r(unsigned char *b, unsigned long l)
{
  b[3] = l & 0xff;
  b[2] = (l >> 8) & 0xff;
  b[1] = (l >> 16) & 0xff;
  b[0] = (l >> 24) & 0xff;
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

static const char *
jtagmkII_get_rc(unsigned int rc)
{
  int i;
  static char msg[50];

  for (i = 0; i < sizeof jtagresults / sizeof jtagresults[0]; i++)
    if (jtagresults[i].code == rc)
      return jtagresults[i].descr;

  sprintf(msg, "Unknown JTAG ICE mkII result code 0x%02x", rc);
  return msg;
}


static void jtagmkII_print_memory(unsigned char *b, size_t s)
{
  int i;

  if (s < 2)
    return;

  for (i = 0; i < s - 1; i++) {
    avrdude_message(MSG_INFO, "0x%02x ", b[i + 1]);
    if (i % 16 == 15)
      putc('\n', stderr);
    else
      putc(' ', stderr);
  }
  if (i % 16 != 0)
    putc('\n', stderr);
}

static void jtagmkII_prmsg(PROGRAMMER * pgm, unsigned char * data, size_t len)
{
  int i;

  if (verbose >= 4) {
    avrdude_message(MSG_TRACE, "Raw message:\n");

    for (i = 0; i < len; i++) {
      avrdude_message(MSG_TRACE, "0x%02x", data[i]);
      if (i % 16 == 15)
	putc('\n', stderr);
      else
	putc(' ', stderr);
    }
    if (i % 16 != 0)
      putc('\n', stderr);
  }

  switch (data[0]) {
  case RSP_OK:
    avrdude_message(MSG_INFO, "OK\n");
    break;

  case RSP_FAILED:
    avrdude_message(MSG_INFO, "FAILED\n");
    break;

  case RSP_ILLEGAL_BREAKPOINT:
    avrdude_message(MSG_INFO, "Illegal breakpoint\n");
    break;

  case RSP_ILLEGAL_COMMAND:
    avrdude_message(MSG_INFO, "Illegal command\n");
    break;

  case RSP_ILLEGAL_EMULATOR_MODE:
    avrdude_message(MSG_INFO, "Illegal emulator mode");
    if (len > 1)
      switch (data[1]) {
      case EMULATOR_MODE_DEBUGWIRE: avrdude_message(MSG_INFO, ": DebugWire"); break;
      case EMULATOR_MODE_JTAG:      avrdude_message(MSG_INFO, ": JTAG"); break;
      case EMULATOR_MODE_HV:        avrdude_message(MSG_INFO, ": HVSP/PP"); break;
      case EMULATOR_MODE_SPI:       avrdude_message(MSG_INFO, ": SPI"); break;
      case EMULATOR_MODE_JTAG_XMEGA: avrdude_message(MSG_INFO, ": JTAG/Xmega"); break;
      }
    putc('\n', stderr);
    break;

  case RSP_ILLEGAL_JTAG_ID:
    avrdude_message(MSG_INFO, "Illegal JTAG ID\n");
    break;

  case RSP_ILLEGAL_MCU_STATE:
    avrdude_message(MSG_INFO, "Illegal MCU state");
    if (len > 1)
      switch (data[1]) {
      case STOPPED:     avrdude_message(MSG_INFO, ": Stopped"); break;
      case RUNNING:     avrdude_message(MSG_INFO, ": Running"); break;
      case PROGRAMMING: avrdude_message(MSG_INFO, ": Programming"); break;
      }
    putc('\n', stderr);
    break;

  case RSP_ILLEGAL_MEMORY_TYPE:
    avrdude_message(MSG_INFO, "Illegal memory type\n");
    break;

  case RSP_ILLEGAL_MEMORY_RANGE:
    avrdude_message(MSG_INFO, "Illegal memory range\n");
    break;

  case RSP_ILLEGAL_PARAMETER:
    avrdude_message(MSG_INFO, "Illegal parameter\n");
    break;

  case RSP_ILLEGAL_POWER_STATE:
    avrdude_message(MSG_INFO, "Illegal power state\n");
    break;

  case RSP_ILLEGAL_VALUE:
    avrdude_message(MSG_INFO, "Illegal value\n");
    break;

  case RSP_NO_TARGET_POWER:
    avrdude_message(MSG_INFO, "No target power\n");
    break;

  case RSP_SIGN_ON:
    avrdude_message(MSG_INFO, "Sign-on succeeded\n");
    /* Sign-on data will be printed below anyway. */
    break;

  case RSP_MEMORY:
    avrdude_message(MSG_INFO, "memory contents:\n");
    jtagmkII_print_memory(data, len);
    break;

  case RSP_PARAMETER:
    avrdude_message(MSG_INFO, "parameter values:\n");
    jtagmkII_print_memory(data, len);
    break;

  case RSP_SPI_DATA:
    avrdude_message(MSG_INFO, "SPI data returned:\n");
    for (i = 1; i < len; i++)
      avrdude_message(MSG_INFO, "0x%02x ", data[i]);
    putc('\n', stderr);
    break;

  case EVT_BREAK:
    avrdude_message(MSG_INFO, "BREAK event");
    if (len >= 6) {
      avrdude_message(MSG_INFO, ", PC = 0x%lx, reason ", b4_to_u32(data + 1));
      switch (data[5]) {
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
	avrdude_message(MSG_INFO, "unknown: 0x%02x", data[5]);
      }
    }
    putc('\n', stderr);
    break;

  default:
    avrdude_message(MSG_INFO, "unknown message 0x%02x\n", data[0]);
  }

  putc('\n', stderr);
}


int jtagmkII_send(PROGRAMMER * pgm, unsigned char * data, size_t len)
{
  unsigned char *buf;

  avrdude_message(MSG_DEBUG, "\n%s: jtagmkII_send(): sending %lu bytes\n",
	    progname, (unsigned long)len);

  if ((buf = malloc(len + 10)) == NULL)
    {
      avrdude_message(MSG_INFO, "%s: jtagmkII_send(): out of memory",
	      progname);
      return -1;
    }

  buf[0] = MESSAGE_START;
  u16_to_b2(buf + 1, PDATA(pgm)->command_sequence);
  u32_to_b4(buf + 3, len);
  buf[7] = TOKEN;
  memcpy(buf + 8, data, len);

  crcappend(buf, len + 8);

  if (serial_send(&pgm->fd, buf, len + 10) != 0) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_send(): failed to send command to serial port\n",
                    progname);
    free(buf);
    return -1;
  }

  free(buf);

  return 0;
}


static int jtagmkII_drain(PROGRAMMER * pgm, int display)
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
static int jtagmkII_recv_frame(PROGRAMMER * pgm, unsigned char **msg,
			       unsigned short * seqno) {
  enum states { sSTART,
		/* NB: do NOT change the sequence of the following: */
		sSEQNUM1, sSEQNUM2,
		sSIZE1, sSIZE2, sSIZE3, sSIZE4,
		sTOKEN,
		sDATA,
		sCSUM1, sCSUM2,
		/* end NB */
		sDONE
  }  state = sSTART;
  unsigned long msglen = 0, l = 0;
  int headeridx = 0;
  int timeout = 0;
  int ignorpkt = 0;
  int rv;
  unsigned char c, *buf = NULL, header[8];
  unsigned short r_seqno = 0;
  unsigned short checksum = 0;

  struct timeval tv;
  double timeoutval = 100;	/* seconds */
  double tstart, tnow;

  avrdude_message(MSG_TRACE, "%s: jtagmkII_recv():\n", progname);

  gettimeofday(&tv, NULL);
  tstart = tv.tv_sec;

  while ( (state != sDONE ) && (!timeout) ) {
    if (state == sDATA) {
      rv = 0;
      if (ignorpkt) {
	/* skip packet's contents */
	for(l = 0; l < msglen; l++)
	  rv += serial_recv(&pgm->fd, &c, 1);
      } else {
	rv += serial_recv(&pgm->fd, buf + 8, msglen);
      }
      if (rv != 0) {
	timedout:
	/* timeout in receive */
        avrdude_message(MSG_NOTICE2, "%s: jtagmkII_recv(): Timeout receiving packet\n",
                          progname);
	free(buf);
	return -1;
      }
    } else {
      if (serial_recv(&pgm->fd, &c, 1) != 0)
	goto timedout;
    }
    checksum ^= c;

    if (state < sDATA)
      header[headeridx++] = c;

    switch (state) {
      case sSTART:
        if (c == MESSAGE_START) {
          state = sSEQNUM1;
        } else {
	  headeridx = 0;
	}
        break;
      case sSEQNUM1:
      case sSEQNUM2:
	r_seqno >>= 8;
	r_seqno |= ((unsigned)c << 8);
	state++;
	break;
      case sSIZE1:
      case sSIZE2:
      case sSIZE3:
      case sSIZE4:
	msglen >>= 8;
	msglen |= ((unsigned)c << 24);
        state++;
        break;
      case sTOKEN:
        if (c == TOKEN) {
	  state = sDATA;
	  if (msglen > MAX_MESSAGE) {
	    avrdude_message(MSG_INFO, "%s: jtagmkII_recv(): msglen %lu exceeds max message "
                            "size %u, ignoring message\n",
                            progname, msglen, MAX_MESSAGE);
	    state = sSTART;
	    headeridx = 0;
	  } else if ((buf = malloc(msglen + 10)) == NULL) {
	    avrdude_message(MSG_INFO, "%s: jtagmkII_recv(): out of memory\n",
		    progname);
	    ignorpkt++;
	  } else {
	    memcpy(buf, header, 8);
	  }
	} else {
	  state = sSTART;
	  headeridx = 0;
	}
        break;
      case sDATA:
	/* The entire payload has been read above. */
	l = msglen + 8;
        state = sCSUM1;
        break;
      case sCSUM1:
      case sCSUM2:
	buf[l++] = c;
	if (state == sCSUM2) {
	  if (crcverify(buf, msglen + 10)) {
	    if (verbose >= 9)
	      avrdude_message(MSG_TRACE2, "%s: jtagmkII_recv(): CRC OK",
		      progname);
	    state = sDONE;
	  } else {
	    avrdude_message(MSG_INFO, "%s: jtagmkII_recv(): checksum error\n",
		    progname);
	    free(buf);
	    return -4;
	  }
	} else
	  state++;
        break;
      default:
        avrdude_message(MSG_INFO, "%s: jtagmkII_recv(): unknown state\n",
                progname);
	free(buf);
        return -5;
     }

     gettimeofday(&tv, NULL);
     tnow = tv.tv_sec;
     if (tnow - tstart > timeoutval) {
       avrdude_message(MSG_INFO, "%s: jtagmkII_recv_frame(): timeout\n",
               progname);
       return -1;
     }

  }
  avrdude_message(MSG_DEBUG, "\n");

  *seqno = r_seqno;
  *msg = buf;

  return msglen;
}

int jtagmkII_recv(PROGRAMMER * pgm, unsigned char **msg) {
  unsigned short r_seqno;
  int rv;

  for (;;) {
    if ((rv = jtagmkII_recv_frame(pgm, msg, &r_seqno)) <= 0)
      return rv;
    avrdude_message(MSG_DEBUG, "%s: jtagmkII_recv(): "
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
      memmove(*msg, *msg + 8, rv);

      if (verbose == 4)
      {
          int i = rv;
          unsigned char *p = *msg;
          avrdude_message(MSG_TRACE, "%s: Recv: ", progname);

          while (i) {
            unsigned char c = *p;
            if (isprint(c)) {
              avrdude_message(MSG_TRACE, "%c ", c);
            }
            else {
              avrdude_message(MSG_TRACE, ". ");
            }
            avrdude_message(MSG_TRACE, "[%02x] ", c);

            p++;
            i--;
          }
          avrdude_message(MSG_TRACE, "\n");
      }
      return rv;
    }
    if (r_seqno == 0xffff) {
      avrdude_message(MSG_DEBUG, "%s: jtagmkII_recv(): got asynchronous event\n",
		progname);
    } else {
      avrdude_message(MSG_NOTICE2, "%s: jtagmkII_recv(): "
		"got wrong sequence number, %u != %u\n",
		progname, r_seqno, PDATA(pgm)->command_sequence);
    }
    free(*msg);
  }
}


int jtagmkII_getsync(PROGRAMMER * pgm, int mode) {
  int tries;
#define MAXTRIES 33
  unsigned char buf[3], *resp, c = 0xff;
  int status;
  unsigned int fwver, hwver;
  int is_dragon;

  avrdude_message(MSG_DEBUG, "%s: jtagmkII_getsync()\n", progname);

  if (strncmp(pgm->type, "JTAG", strlen("JTAG")) == 0) {
    is_dragon = 0;
  } else if (strncmp(pgm->type, "DRAGON", strlen("DRAGON")) == 0) {
    is_dragon = 1;
  } else {
    avrdude_message(MSG_INFO, "%s: Programmer is neither JTAG ICE mkII nor AVR Dragon\n",
                    progname);
    return -1;
  }
  for (tries = 0; tries < MAXTRIES; tries++) {

    /* Get the sign-on information. */
    buf[0] = CMND_GET_SIGN_ON;
    avrdude_message(MSG_NOTICE2, "%s: jtagmkII_getsync(): Sending sign-on command: ",
	      progname);
    jtagmkII_send(pgm, buf, 1);

    status = jtagmkII_recv(pgm, &resp);
    if (status <= 0) {
	avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): sign-on command: "
		"status %d\n",
		progname, status);
    } else if (verbose >= 3) {
      putc('\n', stderr);
      jtagmkII_prmsg(pgm, resp, status);
    } else if (verbose == 2)
      avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);

    if (status > 0) {
      if ((c = resp[0]) == RSP_SIGN_ON) {
	fwver = ((unsigned)resp[8] << 8) | (unsigned)resp[7];
        PDATA(pgm)->fwver = fwver;
	hwver = (unsigned)resp[9];
	memcpy(PDATA(pgm)->serno, resp + 10, 6);
	if (status > 17) {
	  avrdude_message(MSG_NOTICE, "JTAG ICE mkII sign-on message:\n");
	  avrdude_message(MSG_NOTICE, "Communications protocol version: %u\n",
		  (unsigned)resp[1]);
	  avrdude_message(MSG_NOTICE, "M_MCU:\n");
	  avrdude_message(MSG_NOTICE, "  boot-loader FW version:        %u\n",
		  (unsigned)resp[2]);
	  avrdude_message(MSG_NOTICE, "  firmware version:              %u.%02u\n",
		  (unsigned)resp[4], (unsigned)resp[3]);
	  avrdude_message(MSG_NOTICE, "  hardware version:              %u\n",
		  (unsigned)resp[5]);
	  avrdude_message(MSG_NOTICE, "S_MCU:\n");
	  avrdude_message(MSG_NOTICE, "  boot-loader FW version:        %u\n",
		  (unsigned)resp[6]);
	  avrdude_message(MSG_NOTICE, "  firmware version:              %u.%02u\n",
		  (unsigned)resp[8], (unsigned)resp[7]);
	  avrdude_message(MSG_NOTICE, "  hardware version:              %u\n",
		  (unsigned)resp[9]);
	  avrdude_message(MSG_NOTICE, "Serial number:                   "
		  "%02x:%02x:%02x:%02x:%02x:%02x\n",
		  PDATA(pgm)->serno[0], PDATA(pgm)->serno[1], PDATA(pgm)->serno[2], PDATA(pgm)->serno[3], PDATA(pgm)->serno[4], PDATA(pgm)->serno[5]);
	  resp[status - 1] = '\0';
	  avrdude_message(MSG_NOTICE, "Device ID:                       %s\n",
		  resp + 16);
	}
	break;
      }
      free(resp);
    }
  }
  if (tries >= MAXTRIES) {
    if (status <= 0)
      avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): "
                      "timeout/error communicating with programmer (status %d)\n",
                      progname, status);
    else
      avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): "
                      "bad response to sign-on command: %s\n",
                      progname, jtagmkII_get_rc(c));
    return -1;
  }

  PDATA(pgm)->device_descriptor_length = sizeof(struct device_descriptor);
  /*
   * There's no official documentation from Atmel about what firmware
   * revision matches what device descriptor length.  The algorithm
   * below has been found empirically.
   */
#define FWVER(maj, min) ((maj << 8) | (min))
  if (!is_dragon && fwver < FWVER(3, 16)) {
    PDATA(pgm)->device_descriptor_length -= 2;
    avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): "
                    "S_MCU firmware version might be too old to work correctly\n",
                    progname);
  } else if (!is_dragon && fwver < FWVER(4, 0)) {
    PDATA(pgm)->device_descriptor_length -= 2;
  }
  if (mode != EMULATOR_MODE_SPI)
    avrdude_message(MSG_NOTICE2, "%s: jtagmkII_getsync(): Using a %u-byte device descriptor\n",
                    progname, (unsigned)PDATA(pgm)->device_descriptor_length);
  if (mode == EMULATOR_MODE_SPI) {
    PDATA(pgm)->device_descriptor_length = 0;
    if (!is_dragon && fwver < FWVER(4, 14)) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): ISP functionality requires firmware "
                      "version >= 4.14\n",
                      progname);
      return -1;
    }
  }
  if (mode == EMULATOR_MODE_PDI || mode == EMULATOR_MODE_JTAG_XMEGA) {
    if (!is_dragon && mode == EMULATOR_MODE_PDI && hwver < 1) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): Xmega PDI support requires hardware "
                      "revision >= 1\n",
                      progname);
      return -1;
    }
    if (!is_dragon && fwver < FWVER(5, 37)) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): Xmega support requires firmware "
                      "version >= 5.37\n",
                      progname);
      return -1;
    }
    if (is_dragon && fwver < FWVER(6, 11)) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): Xmega support requires firmware "
                      "version >= 6.11\n",
                      progname);
      return -1;
    }
  }
#undef FWVER

  if(mode < 0) return 0;  // for AVR32

  tries = 0;
retry:
  /* Turn the ICE into JTAG or ISP mode as requested. */
  buf[0] = mode;
  if (jtagmkII_setparm(pgm, PAR_EMULATOR_MODE, buf) < 0) {
    if (mode == EMULATOR_MODE_SPI) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): "
                      "ISP activation failed, trying debugWire\n",
                      progname);
      buf[0] = EMULATOR_MODE_DEBUGWIRE;
      if (jtagmkII_setparm(pgm, PAR_EMULATOR_MODE, buf) < 0)
	return -1;
      else {
	/*
	 * We are supposed to send a CMND_RESET with the
	 * MONCOM_DISABLE flag set right now, and then
	 * restart from scratch.
	 *
	 * As this will make the ICE sign off from USB, so
	 * we risk losing our USB connection, it's easier
	 * to instruct the user to restart AVRDUDE rather
	 * than trying to cope with all this inside the
	 * program.
	 */
	(void)jtagmkII_reset(pgm, 0x04);
	if (tries++ > 3) {
	    avrdude_message(MSG_INFO, "%s: Failed to return from debugWIRE to ISP.\n",
                            progname);
	    return -1;
	}
	avrdude_message(MSG_INFO, "%s: Target prepared for ISP, signed off.\n"
                        "%s: Now retrying without power-cycling the target.\n",
                        progname, progname);
        goto retry;
      }
    } else {
      return -1;
    }
  }

  /* GET SYNC forces the target into STOPPED mode */
  buf[0] = CMND_GET_SYNC;
  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_getsync(): Sending get sync command: ",
	    progname);
  jtagmkII_send(pgm, buf, 1);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): "
                    "timeout/error communicating with programmer (status %d)\n",
                    progname, status);
    return -1;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  free(resp);
  if (c != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_getsync(): "
                    "bad response to set parameter command: %s\n",
                    progname, jtagmkII_get_rc(c));
    return -1;
  }

  return 0;
}

/*
 * issue the 'chip erase' command to the AVR device
 */
static int jtagmkII_chip_erase(PROGRAMMER * pgm, AVRPART * p)
{
  int status, len;
  unsigned char buf[6], *resp, c;

  if (p->flags & AVRPART_HAS_PDI) {
    buf[0] = CMND_XMEGA_ERASE;
    buf[1] = XMEGA_ERASE_CHIP;
    memset(buf + 2, 0, 4);      /* address of area to be erased */
    len = 6;
  } else {
    buf[0] = CMND_CHIP_ERASE;
    len = 1;
  }
  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_chip_erase(): Sending %schip erase command: ",
                    progname,
                    (p->flags & AVRPART_HAS_PDI)? "Xmega ": "");
  jtagmkII_send(pgm, buf, len);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_chip_erase(): "
                    "timeout/error communicating with programmer (status %d)\n",
                    progname, status);
    return -1;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  free(resp);
  if (c != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_chip_erase(): "
                    "bad response to chip erase command: %s\n",
                    progname, jtagmkII_get_rc(c));
    return -1;
  }

  if (!(p->flags & AVRPART_HAS_PDI))
      pgm->initialize(pgm, p);

  return 0;
}

/*
 * There is no chip erase functionality in debugWire mode.
 */
static int jtagmkII_chip_erase_dw(PROGRAMMER * pgm, AVRPART * p)
{

  avrdude_message(MSG_INFO, "%s: Chip erase not supported in debugWire mode\n",
	  progname);

  return 0;
}

static void jtagmkII_set_devdescr(PROGRAMMER * pgm, AVRPART * p)
{
  int status;
  unsigned char *resp, c;
  LNODEID ln;
  AVRMEM * m;
  struct {
    unsigned char cmd;
    struct device_descriptor dd;
  } sendbuf;

  memset(&sendbuf, 0, sizeof sendbuf);
  sendbuf.cmd = CMND_SET_DEVICE_DESCRIPTOR;
  sendbuf.dd.ucSPMCRAddress = p->spmcr;
  sendbuf.dd.ucRAMPZAddress = p->rampz;
  sendbuf.dd.ucIDRAddress = p->idr;
  u16_to_b2(sendbuf.dd.EECRAddress, p->eecr);
  sendbuf.dd.ucAllowFullPageBitstream =
    (p->flags & AVRPART_ALLOWFULLPAGEBITSTREAM) != 0;
  sendbuf.dd.EnablePageProgramming =
    (p->flags & AVRPART_ENABLEPAGEPROGRAMMING) != 0;
  for (ln = lfirst(p->mem); ln; ln = lnext(ln)) {
    m = ldata(ln);
    if (strcmp(m->desc, "flash") == 0) {
      if (m->page_size > 256)
        PDATA(pgm)->flash_pagesize = 256;
      else
        PDATA(pgm)->flash_pagesize = m->page_size;
      u32_to_b4(sendbuf.dd.ulFlashSize, m->size);
      u16_to_b2(sendbuf.dd.uiFlashPageSize, m->page_size);
      u16_to_b2(sendbuf.dd.uiFlashpages, m->size / m->page_size);
      if (p->flags & AVRPART_HAS_DW) {
	memcpy(sendbuf.dd.ucFlashInst, p->flash_instr, FLASH_INSTR_SIZE);
	memcpy(sendbuf.dd.ucEepromInst, p->eeprom_instr, EEPROM_INSTR_SIZE);
      }
    } else if (strcmp(m->desc, "eeprom") == 0) {
      sendbuf.dd.ucEepromPageSize = PDATA(pgm)->eeprom_pagesize = m->page_size;
    }
  }
  sendbuf.dd.ucCacheType =
    (p->flags & AVRPART_HAS_PDI)? 0x02 /* ATxmega */: 0x00;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_set_devdescr(): "
	    "Sending set device descriptor command: ",
	    progname);
  jtagmkII_send(pgm, (unsigned char *)&sendbuf,
		PDATA(pgm)->device_descriptor_length + sizeof(unsigned char));

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_set_devdescr(): "
                    "timeout/error communicating with programmer (status %d)\n",
                    progname, status);
    return;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  free(resp);
  if (c != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_set_devdescr(): "
                    "bad response to set device descriptor command: %s\n",
                    progname, jtagmkII_get_rc(c));
  }
}

static void jtagmkII_set_xmega_params(PROGRAMMER * pgm, AVRPART * p)
{
  int status;
  unsigned char *resp, c;
  LNODEID ln;
  AVRMEM * m;
  struct {
    unsigned char cmd;
    struct xmega_device_desc dd;
  } sendbuf;

  memset(&sendbuf, 0, sizeof sendbuf);
  sendbuf.cmd = CMND_SET_XMEGA_PARAMS;
  u16_to_b2(sendbuf.dd.whatever, 0x0002);
  sendbuf.dd.datalen = 47;
  u16_to_b2(sendbuf.dd.nvm_base_addr, p->nvm_base);
  u16_to_b2(sendbuf.dd.mcu_base_addr, p->mcu_base);

  for (ln = lfirst(p->mem); ln; ln = lnext(ln)) {
    m = ldata(ln);
    if (strcmp(m->desc, "flash") == 0) {
      if (m->page_size > 256)
        PDATA(pgm)->flash_pagesize = 256;
      else
        PDATA(pgm)->flash_pagesize = m->page_size;
      u16_to_b2(sendbuf.dd.flash_page_size, m->page_size);
    } else if (strcmp(m->desc, "eeprom") == 0) {
      sendbuf.dd.eeprom_page_size = m->page_size;
      u16_to_b2(sendbuf.dd.eeprom_size, m->size);
      u32_to_b4(sendbuf.dd.nvm_eeprom_offset, m->offset);
    } else if (strcmp(m->desc, "application") == 0) {
      u32_to_b4(sendbuf.dd.app_size, m->size);
      u32_to_b4(sendbuf.dd.nvm_app_offset, m->offset);
    } else if (strcmp(m->desc, "boot") == 0) {
      u16_to_b2(sendbuf.dd.boot_size, m->size);
      u32_to_b4(sendbuf.dd.nvm_boot_offset, m->offset);
    } else if (strcmp(m->desc, "fuse1") == 0) {
      u32_to_b4(sendbuf.dd.nvm_fuse_offset, m->offset & ~7);
    } else if (strncmp(m->desc, "lock", 4) == 0) {
      u32_to_b4(sendbuf.dd.nvm_lock_offset, m->offset);
    } else if (strcmp(m->desc, "usersig") == 0) {
      u32_to_b4(sendbuf.dd.nvm_user_sig_offset, m->offset);
    } else if (strcmp(m->desc, "prodsig") == 0) {
      u32_to_b4(sendbuf.dd.nvm_prod_sig_offset, m->offset);
    } else if (strcmp(m->desc, "data") == 0) {
      u32_to_b4(sendbuf.dd.nvm_data_offset, m->offset);
    }
  }

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_set_xmega_params(): "
	    "Sending set Xmega params command: ",
	    progname);
  jtagmkII_send(pgm, (unsigned char *)&sendbuf, sizeof sendbuf);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_set_xmega_params(): "
                    "timeout/error communicating with programmer (status %d)\n",
                    progname, status);
    return;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  free(resp);
  if (c != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_set_xmega_params(): "
                    "bad response to set device descriptor command: %s\n",
                    progname, jtagmkII_get_rc(c));
  }
}

/*
 * Reset the target.
 */
static int jtagmkII_reset(PROGRAMMER * pgm, unsigned char flags)
{
  int status;
  unsigned char buf[2], *resp, c;

  /*
   * In debugWire mode, don't reset.  Do a forced stop, and tell the
   * ICE to stop any timers, too.
   */
  if (pgm->flag & PGM_FL_IS_DW) {
    unsigned char parm[] = { 0 };

    (void)jtagmkII_setparm(pgm, PAR_TIMERS_RUNNING, parm);
  }

  buf[0] = (pgm->flag & PGM_FL_IS_DW)? CMND_FORCED_STOP: CMND_RESET;
  buf[1] = (pgm->flag & PGM_FL_IS_DW)? 1: flags;
  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_reset(): Sending %s command: ",
	    progname, (pgm->flag & PGM_FL_IS_DW)? "stop": "reset");
  jtagmkII_send(pgm, buf, 2);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_reset(): "
                    "timeout/error communicating with programmer (status %d)\n",
                    progname, status);
    return -1;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  free(resp);
  if (c != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_reset(): "
                    "bad response to reset command: %s\n",
                    progname, jtagmkII_get_rc(c));
    return -1;
  }

  return 0;
}

static int jtagmkII_program_enable_INFO(PROGRAMMER * pgm, AVRPART * p)
{
  return 0;
}

static int jtagmkII_program_enable(PROGRAMMER * pgm)
{
  int status;
  unsigned char buf[1], *resp, c;
  int use_ext_reset;

  if (PDATA(pgm)->prog_enabled)
    return 0;

  for (use_ext_reset = 0; use_ext_reset <= 1; use_ext_reset++) {
    buf[0] = CMND_ENTER_PROGMODE;
    avrdude_message(MSG_NOTICE2, "%s: jtagmkII_program_enable(): "
	      "Sending enter progmode command: ",
	      progname);
    jtagmkII_send(pgm, buf, 1);

    status = jtagmkII_recv(pgm, &resp);
    if (status <= 0) {
      if (verbose >= 2)
	putc('\n', stderr);
      avrdude_message(MSG_INFO, "%s: jtagmkII_program_enable(): "
                      "timeout/error communicating with programmer (status %d)\n",
                      progname, status);
      return -1;
    }
    if (verbose >= 3) {
      putc('\n', stderr);
      jtagmkII_prmsg(pgm, resp, status);
    } else if (verbose == 2)
      avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
    c = resp[0];
    free(resp);
    if (c != RSP_OK) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_program_enable(): "
                      "bad response to enter progmode command: %s\n",
                      progname, jtagmkII_get_rc(c));
      if (c == RSP_ILLEGAL_JTAG_ID) {
	if (use_ext_reset == 0) {
	  unsigned char parm[] = { 1};
          avrdude_message(MSG_INFO, "%s: retrying with external reset applied\n",
                            progname);

	  (void)jtagmkII_setparm(pgm, PAR_EXTERNAL_RESET, parm);
	  continue;
	}

	avrdude_message(MSG_INFO, "%s: JTAGEN fuse disabled?\n", progname);
	return -1;
      }
    }
  }

  PDATA(pgm)->prog_enabled = 1;
  return 0;
}

static int jtagmkII_program_disable(PROGRAMMER * pgm)
{
  int status;
  unsigned char buf[1], *resp, c;

  if (!PDATA(pgm)->prog_enabled)
    return 0;

  buf[0] = CMND_LEAVE_PROGMODE;
  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_program_disable(): "
	    "Sending leave progmode command: ",
	    progname);
  jtagmkII_send(pgm, buf, 1);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_program_disable(): "
                    "timeout/error communicating with programmer (status %d)\n",
                    progname, status);
    return -1;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  free(resp);
  if (c != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_program_disable(): "
                    "bad response to leave progmode command: %s\n",
                    progname, jtagmkII_get_rc(c));
    return -1;
  }

  PDATA(pgm)->prog_enabled = 0;
  (void)jtagmkII_reset(pgm, 0x01);

  return 0;
}

static unsigned char jtagmkII_get_baud(long baud)
{
  static struct {
    long baud;
    unsigned char val;
  } baudtab[] = {
    { 2400L, PAR_BAUD_2400 },
    { 4800L, PAR_BAUD_4800 },
    { 9600L, PAR_BAUD_9600 },
    { 19200L, PAR_BAUD_19200 },
    { 38400L, PAR_BAUD_38400 },
    { 57600L, PAR_BAUD_57600 },
    { 115200L, PAR_BAUD_115200 },
    { 14400L, PAR_BAUD_14400 },
  };
  int i;

  for (i = 0; i < sizeof baudtab / sizeof baudtab[0]; i++)
    if (baud == baudtab[i].baud)
      return baudtab[i].val;

  return 0;
}

/*
 * initialize the AVR device and prepare it to accept commands
 */
static int jtagmkII_initialize(PROGRAMMER * pgm, AVRPART * p)
{
  AVRMEM hfuse;
  unsigned char b;
  int ok;
  const char *ifname;

  ok = 0;
  if (pgm->flag & PGM_FL_IS_DW) {
    ifname = "debugWire";
    if (p->flags & AVRPART_HAS_DW)
      ok = 1;
  } else if (pgm->flag & PGM_FL_IS_PDI) {
    ifname = "PDI";
    if (p->flags & AVRPART_HAS_PDI)
      ok = 1;
  } else {
    ifname = "JTAG";
    if (p->flags & AVRPART_HAS_JTAG)
      ok = 1;
  }

  if (!ok) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_initialize(): part %s has no %s interface\n",
	    progname, p->desc, ifname);
    return -1;
  }

  if ((serdev->flags & SERDEV_FL_CANSETSPEED) && pgm->baudrate && pgm->baudrate != 19200) {
    if ((b = jtagmkII_get_baud(pgm->baudrate)) == 0) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_initialize(): unsupported baudrate %d\n",
	      progname, pgm->baudrate);
    } else {
      avrdude_message(MSG_NOTICE2, "%s: jtagmkII_initialize(): "
		"trying to set baudrate to %d\n",
		progname, pgm->baudrate);
      if (jtagmkII_setparm(pgm, PAR_BAUD_RATE, &b) == 0)
	serial_setspeed(&pgm->fd, pgm->baudrate);
    }
  }
  if ((pgm->flag & PGM_FL_IS_JTAG) && pgm->bitclock != 0.0) {
    avrdude_message(MSG_NOTICE2, "%s: jtagmkII_initialize(): "
	      "trying to set JTAG clock period to %.1f us\n",
	      progname, pgm->bitclock);
    if (jtagmkII_set_sck_period(pgm, pgm->bitclock) != 0)
      return -1;
  }

  if ((pgm->flag & PGM_FL_IS_JTAG) &&
      jtagmkII_setparm(pgm, PAR_DAISY_CHAIN_INFO, PDATA(pgm)->jtagchain) < 0) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_initialize(): Failed to setup JTAG chain\n",
            progname);
    return -1;
  }

  /*
   * If this is an ATxmega device in JTAG mode, change the emulator
   * mode from JTAG to JTAG_XMEGA.
   */
  if ((pgm->flag & PGM_FL_IS_JTAG) &&
      (p->flags & AVRPART_HAS_PDI)) {
    if (jtagmkII_getsync(pgm, EMULATOR_MODE_JTAG_XMEGA) < 0)
      return -1;
  }
  /*
   * Must set the device descriptor before entering programming mode.
   */
  if (PDATA(pgm)->fwver >= 0x700 && (p->flags & AVRPART_HAS_PDI) != 0)
    jtagmkII_set_xmega_params(pgm, p);
  else
    jtagmkII_set_devdescr(pgm, p);

  PDATA(pgm)->boot_start = ULONG_MAX;
  /*
   * If this is an ATxmega device in JTAG mode, change the emulator
   * mode from JTAG to JTAG_XMEGA.
   */
  if ((pgm->flag & PGM_FL_IS_JTAG) &&
      (p->flags & AVRPART_HAS_PDI)) {
    /*
     * Find out where the border between application and boot area
     * is.
     */
    AVRMEM *bootmem = avr_locate_mem(p, "boot");
    AVRMEM *flashmem = avr_locate_mem(p, "flash");
    if (bootmem == NULL || flashmem == NULL) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_initialize(): Cannot locate \"flash\" and \"boot\" memories in description\n",
                      progname);
    } else {
      if (PDATA(pgm)->fwver < 0x700) {
        /* V7+ firmware does not need this anymore */
        unsigned char par[4];

        u32_to_b4(par, flashmem->offset);
        (void) jtagmkII_setparm(pgm, PAR_PDI_OFFSET_START, par);
        u32_to_b4(par, bootmem->offset);
        (void) jtagmkII_setparm(pgm, PAR_PDI_OFFSET_END, par);
      }

      PDATA(pgm)->boot_start = bootmem->offset - flashmem->offset;
    }
  }

  free(PDATA(pgm)->flash_pagecache);
  free(PDATA(pgm)->eeprom_pagecache);
  if ((PDATA(pgm)->flash_pagecache = malloc(PDATA(pgm)->flash_pagesize)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_initialize(): Out of memory\n",
	    progname);
    return -1;
  }
  if ((PDATA(pgm)->eeprom_pagecache = malloc(PDATA(pgm)->eeprom_pagesize)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_initialize(): Out of memory\n",
	    progname);
    free(PDATA(pgm)->flash_pagecache);
    return -1;
  }
  PDATA(pgm)->flash_pageaddr = PDATA(pgm)->eeprom_pageaddr = (unsigned long)-1L;

  if (PDATA(pgm)->fwver >= 0x700 && (p->flags & AVRPART_HAS_PDI)) {
    /*
     * Work around for
     * https://savannah.nongnu.org/bugs/index.php?37942
     *
     * Firmware version 7.24 (at least) on the Dragon behaves very
     * strange when it gets a RESET request here.  All subsequent
     * responses are completely off, so the emulator becomes unusable.
     * This appears to be a firmware bug (earlier versions, at least
     * 7.14, didn't experience this), but by omitting the RESET for
     * Xmega devices, we can work around it.
     */
  } else {
    if (jtagmkII_reset(pgm, 0x01) < 0)
      return -1;
  }

  if ((pgm->flag & PGM_FL_IS_JTAG) && !(p->flags & AVRPART_HAS_PDI)) {
    strcpy(hfuse.desc, "hfuse");
    if (jtagmkII_read_byte(pgm, p, &hfuse, 1, &b) < 0)
      return -1;
    if ((b & OCDEN) != 0)
      avrdude_message(MSG_INFO, "%s: jtagmkII_initialize(): warning: OCDEN fuse not programmed, "
                      "single-byte EEPROM updates not possible\n",
                      progname);
  }

  return 0;
}

static void jtagmkII_disable(PROGRAMMER * pgm)
{

  free(PDATA(pgm)->flash_pagecache);
  PDATA(pgm)->flash_pagecache = NULL;
  free(PDATA(pgm)->eeprom_pagecache);
  PDATA(pgm)->eeprom_pagecache = NULL;

  /*
   * jtagmkII_program_disable() doesn't do anything if the
   * device is currently not in programming mode, so just
   * call it unconditionally here.
   */
  (void)jtagmkII_program_disable(pgm);
}

static void jtagmkII_enable(PROGRAMMER * pgm)
{
  return;
}

static int jtagmkII_parseextparms(PROGRAMMER * pgm, LISTID extparms)
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
        avrdude_message(MSG_INFO, "%s: jtagmkII_parseextparms(): invalid JTAG chain '%s'\n",
                        progname, extended_param);
        rv = -1;
        continue;
      }
      avrdude_message(MSG_NOTICE2, "%s: jtagmkII_parseextparms(): JTAG chain parsed as:\n"
                        "%s %u units before, %u units after, %u bits before, %u bits after\n",
                        progname,
                        progbuf, ub, ua, bb, ba);
      PDATA(pgm)->jtagchain[0] = ub;
      PDATA(pgm)->jtagchain[1] = ua;
      PDATA(pgm)->jtagchain[2] = bb;
      PDATA(pgm)->jtagchain[3] = ba;

      continue;
    }

    avrdude_message(MSG_INFO, "%s: jtagmkII_parseextparms(): invalid extended parameter '%s'\n",
                    progname, extended_param);
    rv = -1;
  }

  return rv;
}


static int jtagmkII_open(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_open()\n", progname);

  /*
   * The JTAG ICE mkII always starts with a baud rate of 19200 Bd upon
   * attaching.  If the config file or command-line parameters specify
   * a higher baud rate, we switch to it later on, after establishing
   * the connection with the ICE.
   */
  pinfo.baud = 19200;

  /*
   * If the port name starts with "usb", divert the serial routines
   * to the USB ones.  The serial_open() function for USB overrides
   * the meaning of the "baud" parameter to be the USB device ID to
   * search for.
   */
  if (strncmp(port, "usb", 3) == 0) {
#if defined(HAVE_LIBUSB)
    serdev = &usb_serdev;
    pinfo.usbinfo.vid = USB_VENDOR_ATMEL;
    pinfo.usbinfo.flags = 0;
    pinfo.usbinfo.pid = USB_DEVICE_JTAGICEMKII;
    pgm->fd.usb.max_xfer = USBDEV_MAX_XFER_MKII;
    pgm->fd.usb.rep = USBDEV_BULK_EP_READ_MKII;
    pgm->fd.usb.wep = USBDEV_BULK_EP_WRITE_MKII;
    pgm->fd.usb.eep = 0;           /* no seperate EP for events */
#else
    avrdude_message(MSG_INFO, "avrdude was compiled without usb support.\n");
    return -1;
#endif
  }

  strcpy(pgm->port, port);
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  jtagmkII_drain(pgm, 0);

  if (jtagmkII_getsync(pgm, EMULATOR_MODE_JTAG) < 0)
    return -1;

  return 0;
}

static int jtagmkII_open_dw(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_open_dw()\n", progname);

  /*
   * The JTAG ICE mkII always starts with a baud rate of 19200 Bd upon
   * attaching.  If the config file or command-line parameters specify
   * a higher baud rate, we switch to it later on, after establishing
   * the connection with the ICE.
   */
  pinfo.baud = 19200;

  /*
   * If the port name starts with "usb", divert the serial routines
   * to the USB ones.  The serial_open() function for USB overrides
   * the meaning of the "baud" parameter to be the USB device ID to
   * search for.
   */
  if (strncmp(port, "usb", 3) == 0) {
#if defined(HAVE_LIBUSB)
    serdev = &usb_serdev;
    pinfo.usbinfo.vid = USB_VENDOR_ATMEL;
    pinfo.usbinfo.flags = 0;
    pinfo.usbinfo.pid = USB_DEVICE_JTAGICEMKII;
    pgm->fd.usb.max_xfer = USBDEV_MAX_XFER_MKII;
    pgm->fd.usb.rep = USBDEV_BULK_EP_READ_MKII;
    pgm->fd.usb.wep = USBDEV_BULK_EP_WRITE_MKII;
    pgm->fd.usb.eep = 0;           /* no seperate EP for events */
#else
    avrdude_message(MSG_INFO, "avrdude was compiled without usb support.\n");
    return -1;
#endif
  }

  strcpy(pgm->port, port);
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  jtagmkII_drain(pgm, 0);

  if (jtagmkII_getsync(pgm, EMULATOR_MODE_DEBUGWIRE) < 0)
    return -1;

  return 0;
}

static int jtagmkII_open_pdi(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_open_pdi()\n", progname);

  /*
   * The JTAG ICE mkII always starts with a baud rate of 19200 Bd upon
   * attaching.  If the config file or command-line parameters specify
   * a higher baud rate, we switch to it later on, after establishing
   * the connection with the ICE.
   */
  pinfo.baud = 19200;

  /*
   * If the port name starts with "usb", divert the serial routines
   * to the USB ones.  The serial_open() function for USB overrides
   * the meaning of the "baud" parameter to be the USB device ID to
   * search for.
   */
  if (strncmp(port, "usb", 3) == 0) {
#if defined(HAVE_LIBUSB)
    serdev = &usb_serdev;
    pinfo.usbinfo.vid = USB_VENDOR_ATMEL;
    pinfo.usbinfo.flags = 0;
    pinfo.usbinfo.pid = USB_DEVICE_JTAGICEMKII;
    pgm->fd.usb.max_xfer = USBDEV_MAX_XFER_MKII;
    pgm->fd.usb.rep = USBDEV_BULK_EP_READ_MKII;
    pgm->fd.usb.wep = USBDEV_BULK_EP_WRITE_MKII;
    pgm->fd.usb.eep = 0;           /* no seperate EP for events */
#else
    avrdude_message(MSG_INFO, "avrdude was compiled without usb support.\n");
    return -1;
#endif
  }

  strcpy(pgm->port, port);
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  jtagmkII_drain(pgm, 0);

  if (jtagmkII_getsync(pgm, EMULATOR_MODE_PDI) < 0)
    return -1;

  return 0;
}


static int jtagmkII_dragon_open(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_dragon_open()\n", progname);

  /*
   * The JTAG ICE mkII always starts with a baud rate of 19200 Bd upon
   * attaching.  If the config file or command-line parameters specify
   * a higher baud rate, we switch to it later on, after establishing
   * the connection with the ICE.
   */
  pinfo.baud = 19200;

  /*
   * If the port name starts with "usb", divert the serial routines
   * to the USB ones.  The serial_open() function for USB overrides
   * the meaning of the "baud" parameter to be the USB device ID to
   * search for.
   */
  if (strncmp(port, "usb", 3) == 0) {
#if defined(HAVE_LIBUSB)
    serdev = &usb_serdev;
    pinfo.usbinfo.vid = USB_VENDOR_ATMEL;
    pinfo.usbinfo.flags = 0;
    pinfo.usbinfo.pid = USB_DEVICE_AVRDRAGON;
    pgm->fd.usb.max_xfer = USBDEV_MAX_XFER_MKII;
    pgm->fd.usb.rep = USBDEV_BULK_EP_READ_MKII;
    pgm->fd.usb.wep = USBDEV_BULK_EP_WRITE_MKII;
    pgm->fd.usb.eep = 0;           /* no seperate EP for events */
#else
    avrdude_message(MSG_INFO, "avrdude was compiled without usb support.\n");
    return -1;
#endif
  }

  strcpy(pgm->port, port);
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  jtagmkII_drain(pgm, 0);

  if (jtagmkII_getsync(pgm, EMULATOR_MODE_JTAG) < 0)
    return -1;

  return 0;
}


static int jtagmkII_dragon_open_dw(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_dragon_open_dw()\n", progname);

  /*
   * The JTAG ICE mkII always starts with a baud rate of 19200 Bd upon
   * attaching.  If the config file or command-line parameters specify
   * a higher baud rate, we switch to it later on, after establishing
   * the connection with the ICE.
   */
  pinfo.baud = 19200;

  /*
   * If the port name starts with "usb", divert the serial routines
   * to the USB ones.  The serial_open() function for USB overrides
   * the meaning of the "baud" parameter to be the USB device ID to
   * search for.
   */
  if (strncmp(port, "usb", 3) == 0) {
#if defined(HAVE_LIBUSB)
    serdev = &usb_serdev;
    pinfo.usbinfo.vid = USB_VENDOR_ATMEL;
    pinfo.usbinfo.flags = 0;
    pinfo.usbinfo.pid = USB_DEVICE_AVRDRAGON;
    pgm->fd.usb.max_xfer = USBDEV_MAX_XFER_MKII;
    pgm->fd.usb.rep = USBDEV_BULK_EP_READ_MKII;
    pgm->fd.usb.wep = USBDEV_BULK_EP_WRITE_MKII;
    pgm->fd.usb.eep = 0;           /* no seperate EP for events */
#else
    avrdude_message(MSG_INFO, "avrdude was compiled without usb support.\n");
    return -1;
#endif
  }

  strcpy(pgm->port, port);
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  jtagmkII_drain(pgm, 0);

  if (jtagmkII_getsync(pgm, EMULATOR_MODE_DEBUGWIRE) < 0)
    return -1;

  return 0;
}


static int jtagmkII_dragon_open_pdi(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_dragon_open_pdi()\n", progname);

  /*
   * The JTAG ICE mkII always starts with a baud rate of 19200 Bd upon
   * attaching.  If the config file or command-line parameters specify
   * a higher baud rate, we switch to it later on, after establishing
   * the connection with the ICE.
   */
  pinfo.baud = 19200;

  /*
   * If the port name starts with "usb", divert the serial routines
   * to the USB ones.  The serial_open() function for USB overrides
   * the meaning of the "baud" parameter to be the USB device ID to
   * search for.
   */
  if (strncmp(port, "usb", 3) == 0) {
#if defined(HAVE_LIBUSB)
    serdev = &usb_serdev;
    pinfo.usbinfo.vid = USB_VENDOR_ATMEL;
    pinfo.usbinfo.flags = 0;
    pinfo.usbinfo.pid = USB_DEVICE_AVRDRAGON;
    pgm->fd.usb.max_xfer = USBDEV_MAX_XFER_MKII;
    pgm->fd.usb.rep = USBDEV_BULK_EP_READ_MKII;
    pgm->fd.usb.wep = USBDEV_BULK_EP_WRITE_MKII;
    pgm->fd.usb.eep = 0;           /* no seperate EP for events */
#else
    avrdude_message(MSG_INFO, "avrdude was compiled without usb support.\n");
    return -1;
#endif
  }

  strcpy(pgm->port, port);
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  jtagmkII_drain(pgm, 0);

  if (jtagmkII_getsync(pgm, EMULATOR_MODE_PDI) < 0)
    return -1;

  return 0;
}


void jtagmkII_close(PROGRAMMER * pgm)
{
  int status;
  unsigned char buf[1], *resp, c;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_close()\n", progname);

  if (pgm->flag & PGM_FL_IS_PDI) {
    /* When in PDI mode, restart target. */
    buf[0] = CMND_GO;
    avrdude_message(MSG_NOTICE2, "%s: jtagmkII_close(): Sending GO command: ",
	      progname);
    jtagmkII_send(pgm, buf, 1);

    status = jtagmkII_recv(pgm, &resp);
    if (status <= 0) {
      if (verbose >= 2)
	putc('\n', stderr);
      avrdude_message(MSG_INFO, "%s: jtagmkII_close(): "
                      "timeout/error communicating with programmer (status %d)\n",
                      progname, status);
    } else {
      if (verbose >= 3) {
	putc('\n', stderr);
	jtagmkII_prmsg(pgm, resp, status);
      } else if (verbose == 2)
	avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
      c = resp[0];
      free(resp);
      if (c != RSP_OK) {
	avrdude_message(MSG_INFO, "%s: jtagmkII_close(): "
                        "bad response to GO command: %s\n",
                        progname, jtagmkII_get_rc(c));
      }
    }
  }

  buf[0] = CMND_SIGN_OFF;
  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_close(): Sending sign-off command: ",
	    progname);
  jtagmkII_send(pgm, buf, 1);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_close(): "
                    "timeout/error communicating with programmer (status %d)\n",
                    progname, status);
    return;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  free(resp);
  if (c != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_close(): "
                    "bad response to sign-off command: %s\n",
                    progname, jtagmkII_get_rc(c));
  }

  serial_close(&pgm->fd);
  pgm->fd.ifd = -1;
}

static int jtagmkII_page_erase(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                               unsigned int addr)
{
  unsigned char cmd[6];
  unsigned char *resp;
  int status, tries;
  long otimeout = serial_recv_timeout;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_page_erase(.., %s, 0x%x)\n",
	    progname, m->desc, addr);

  if (!(p->flags & AVRPART_HAS_PDI)) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_page_erase: not an Xmega device\n",
	    progname);
    return -1;
  }
  if ((pgm->flag & PGM_FL_IS_DW)) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_page_erase: not applicable to debugWIRE\n",
	    progname);
    return -1;
  }

  if (jtagmkII_program_enable(pgm) < 0)
    return -1;

  cmd[0] = CMND_XMEGA_ERASE;
  if (strcmp(m->desc, "flash") == 0) {
    if (jtagmkII_memtype(pgm, p, addr) == MTYPE_FLASH)
      cmd[1] = XMEGA_ERASE_APP_PAGE;
    else
      cmd[1] = XMEGA_ERASE_BOOT_PAGE;
  } else if (strcmp(m->desc, "eeprom") == 0) {
    cmd[1] = XMEGA_ERASE_EEPROM_PAGE;
  } else if ( ( strcmp(m->desc, "usersig") == 0 ) ) {
    cmd[1] = XMEGA_ERASE_USERSIG;
  } else if ( ( strcmp(m->desc, "boot") == 0 ) ) {
    cmd[1] = XMEGA_ERASE_BOOT_PAGE;
  } else {
    cmd[1] = XMEGA_ERASE_APP_PAGE;
  }
  serial_recv_timeout = 100;

  /*
   * Don't use jtagmkII_memaddr() here.  While with all other
   * commands, firmware 7+ doesn't require the NVM offsets being
   * applied, the erase page commands make an exception, and do
   * require the NVM offsets as part of the (page) address.
   */
  u32_to_b4(cmd + 2, addr + m->offset);

  tries = 0;

  retry:
    avrdude_message(MSG_NOTICE2, "%s: jtagmkII_page_erase(): "
            "Sending xmega erase command: ",
            progname);
  jtagmkII_send(pgm, cmd, sizeof cmd);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_page_erase(): "
                      "timeout/error communicating with programmer (status %d)\n",
                      progname, status);
    if (tries++ < 4) {
      serial_recv_timeout *= 2;
      goto retry;
    }
    avrdude_message(MSG_INFO, "%s: jtagmkII_page_erase(): fatal timeout/"
                    "error communicating with programmer (status %d)\n",
                    progname, status);
    serial_recv_timeout = otimeout;
    return -1;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  if (resp[0] != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_page_erase(): "
                    "bad response to xmega erase command: %s\n",
                    progname, jtagmkII_get_rc(resp[0]));
    free(resp);
    serial_recv_timeout = otimeout;
    return -1;
  }
  free(resp);

  serial_recv_timeout = otimeout;

  return 0;
}

static int jtagmkII_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                unsigned int page_size,
                                unsigned int addr, unsigned int n_bytes)
{
  unsigned int block_size;
  unsigned int maxaddr = addr + n_bytes;
  unsigned char *cmd;
  unsigned char *resp;
  int status, tries, dynamic_memtype = 0;
  long otimeout = serial_recv_timeout;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_paged_write(.., %s, %d, %d)\n",
	    progname, m->desc, page_size, n_bytes);

  if (!(pgm->flag & PGM_FL_IS_DW) && jtagmkII_program_enable(pgm) < 0)
    return -1;

  if (page_size == 0) page_size = 256;
  else if (page_size > 256) page_size = 256;

  if ((cmd = malloc(page_size + 10)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_paged_write(): Out of memory\n",
	    progname);
    return -1;
  }

  cmd[0] = CMND_WRITE_MEMORY;
  if (strcmp(m->desc, "flash") == 0) {
    PDATA(pgm)->flash_pageaddr = (unsigned long)-1L;
    cmd[1] = jtagmkII_memtype(pgm, p, addr);
    if (p->flags & AVRPART_HAS_PDI)
      /* dynamically decide between flash/boot memtype */
      dynamic_memtype = 1;
  } else if (strcmp(m->desc, "eeprom") == 0) {
    if (pgm->flag & PGM_FL_IS_DW) {
      /*
       * jtagmkII_paged_write() to EEPROM attempted while in
       * DW mode.  Use jtagmkII_write_byte() instead.
       */
      for (; addr < maxaddr; addr++) {
	status = jtagmkII_write_byte(pgm, p, m, addr, m->buf[addr]);
	if (status < 0) {
	  free(cmd);
	  return -1;
	}
      }
      free(cmd);
      return n_bytes;
    }
    cmd[1] = ( p->flags & AVRPART_HAS_PDI ) ? MTYPE_EEPROM : MTYPE_EEPROM_PAGE;
    PDATA(pgm)->eeprom_pageaddr = (unsigned long)-1L;
  } else if ( ( strcmp(m->desc, "usersig") == 0 ) ) {
    cmd[1] = MTYPE_USERSIG;
  } else if ( ( strcmp(m->desc, "boot") == 0 ) ) {
    cmd[1] = MTYPE_BOOT_FLASH;
  } else if ( p->flags & AVRPART_HAS_PDI ) {
    cmd[1] = MTYPE_FLASH;
  } else {
    cmd[1] = MTYPE_SPM;
  }
  serial_recv_timeout = 100;
  for (; addr < maxaddr; addr += page_size) {
    if ((maxaddr - addr) < page_size)
      block_size = maxaddr - addr;
    else
      block_size = page_size;
    avrdude_message(MSG_DEBUG, "%s: jtagmkII_paged_write(): "
	      "block_size at addr %d is %d\n",
	      progname, addr, block_size);

    if (dynamic_memtype)
      cmd[1] = jtagmkII_memtype(pgm, p, addr);

    u32_to_b4(cmd + 2, page_size);
    u32_to_b4(cmd + 6, jtagmkII_memaddr(pgm, p, m, addr));

    /*
     * The JTAG ICE will refuse to write anything but a full page, at
     * least for the flash ROM.  If a partial page has been requested,
     * set the remainder to 0xff.  (Maybe we should rather read back
     * the existing contents instead before?  Doesn't matter much, as
     * bits cannot be written to 1 anyway.)
     */
    memset(cmd + 10, 0xff, page_size);
    memcpy(cmd + 10, m->buf + addr, block_size);

    tries = 0;

    retry:
      avrdude_message(MSG_NOTICE2, "%s: jtagmkII_paged_write(): "
	      "Sending write memory command: ",
	      progname);
    jtagmkII_send(pgm, cmd, page_size + 10);

    status = jtagmkII_recv(pgm, &resp);
    if (status <= 0) {
      if (verbose >= 2)
	putc('\n', stderr);
      avrdude_message(MSG_INFO, "%s: jtagmkII_paged_write(): "
                        "timeout/error communicating with programmer (status %d)\n",
                        progname, status);
      if (tries++ < 4) {
	serial_recv_timeout *= 2;
	goto retry;
      }
      avrdude_message(MSG_INFO, "%s: jtagmkII_paged_write(): fatal timeout/"
                      "error communicating with programmer (status %d)\n",
                      progname, status);
      free(cmd);
      serial_recv_timeout = otimeout;
      return -1;
    }
    if (verbose >= 3) {
      putc('\n', stderr);
      jtagmkII_prmsg(pgm, resp, status);
    } else if (verbose == 2)
      avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
    if (resp[0] != RSP_OK) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_paged_write(): "
                      "bad response to write memory command: %s\n",
                      progname, jtagmkII_get_rc(resp[0]));
      free(resp);
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

static int jtagmkII_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                               unsigned int page_size,
                               unsigned int addr, unsigned int n_bytes)
{
  unsigned int block_size;
  unsigned int maxaddr = addr + n_bytes;
  unsigned char cmd[10];
  unsigned char *resp;
  int status, tries, dynamic_memtype = 0;
  long otimeout = serial_recv_timeout;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_paged_load(.., %s, %d, %d)\n",
	    progname, m->desc, page_size, n_bytes);

  if (!(pgm->flag & PGM_FL_IS_DW) && jtagmkII_program_enable(pgm) < 0)
    return -1;

  page_size = m->readsize;

  cmd[0] = CMND_READ_MEMORY;
  if (strcmp(m->desc, "flash") == 0) {
    cmd[1] = jtagmkII_memtype(pgm, p, addr);
    if (p->flags & AVRPART_HAS_PDI)
      /* dynamically decide between flash/boot memtype */
      dynamic_memtype = 1;
  } else if (strcmp(m->desc, "eeprom") == 0) {
    cmd[1] = ( p->flags & AVRPART_HAS_PDI ) ? MTYPE_EEPROM : MTYPE_EEPROM_PAGE;
    if (pgm->flag & PGM_FL_IS_DW)
      return -1;
  } else if ( ( strcmp(m->desc, "prodsig") == 0 ) ) {
    cmd[1] = MTYPE_PRODSIG;
  } else if ( ( strcmp(m->desc, "usersig") == 0 ) ) {
    cmd[1] = MTYPE_USERSIG;
  } else if ( ( strcmp(m->desc, "boot") == 0 ) ) {
    cmd[1] = MTYPE_BOOT_FLASH;
  } else if ( p->flags & AVRPART_HAS_PDI ) {
    cmd[1] = MTYPE_FLASH;
  } else {
    cmd[1] = MTYPE_SPM;
  }
  serial_recv_timeout = 100;
  for (; addr < maxaddr; addr += page_size) {
    if ((maxaddr - addr) < page_size)
      block_size = maxaddr - addr;
    else
      block_size = page_size;
    avrdude_message(MSG_DEBUG, "%s: jtagmkII_paged_load(): "
	      "block_size at addr %d is %d\n",
	      progname, addr, block_size);

    if (dynamic_memtype)
      cmd[1] = jtagmkII_memtype(pgm, p, addr);

    u32_to_b4(cmd + 2, block_size);
    u32_to_b4(cmd + 6, jtagmkII_memaddr(pgm, p, m, addr));

    tries = 0;

    retry:
      avrdude_message(MSG_NOTICE2, "%s: jtagmkII_paged_load(): Sending read memory command: ",
	      progname);
    jtagmkII_send(pgm, cmd, 10);

    status = jtagmkII_recv(pgm, &resp);
    if (status <= 0) {
      if (verbose >= 2)
	putc('\n', stderr);
      avrdude_message(MSG_INFO, "%s: jtagmkII_paged_load(): "
                        "timeout/error communicating with programmer (status %d)\n",
                        progname, status);
      if (tries++ < 4) {
	serial_recv_timeout *= 2;
	goto retry;
      }
      avrdude_message(MSG_INFO, "%s: jtagmkII_paged_load(): fatal timeout/"
                      "error communicating with programmer (status %d)\n",
                      progname, status);
      serial_recv_timeout = otimeout;
      return -1;
    }
    if (verbose >= 3) {
      putc('\n', stderr);
      jtagmkII_prmsg(pgm, resp, status);
    } else if (verbose == 2)
      avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
    if (resp[0] != RSP_MEMORY) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_paged_load(): "
	      "bad response to read memory command: %s\n",
	      progname, jtagmkII_get_rc(resp[0]));
      free(resp);
      serial_recv_timeout = otimeout;
      return -1;
    }
    memcpy(m->buf + addr, resp + 1, status-1);
    free(resp);
  }
  serial_recv_timeout = otimeout;

  return n_bytes;
}

static int jtagmkII_read_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
			      unsigned long addr, unsigned char * value)
{
  unsigned char cmd[10];
  unsigned char *resp = NULL, *cache_ptr = NULL;
  int status, tries, unsupp;
  unsigned long paddr = 0UL, *paddr_ptr = NULL;
  unsigned int pagesize = 0;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_read_byte(.., %s, 0x%lx, ...)\n",
	    progname, mem->desc, addr);

  if (!(pgm->flag & PGM_FL_IS_DW) && jtagmkII_program_enable(pgm) < 0)
    return -1;

  cmd[0] = CMND_READ_MEMORY;
  unsupp = 0;

  addr += mem->offset;
  cmd[1] = ( p->flags & AVRPART_HAS_PDI ) ? MTYPE_FLASH : MTYPE_FLASH_PAGE;
  if (strcmp(mem->desc, "flash") == 0 ||
      strcmp(mem->desc, "application") == 0 ||
      strcmp(mem->desc, "apptable") == 0 ||
      strcmp(mem->desc, "boot") == 0) {
    pagesize = PDATA(pgm)->flash_pagesize;
    paddr = addr & ~(pagesize - 1);
    paddr_ptr = &PDATA(pgm)->flash_pageaddr;
    cache_ptr = PDATA(pgm)->flash_pagecache;
  } else if (strcmp(mem->desc, "eeprom") == 0) {
    if ( (pgm->flag & PGM_FL_IS_DW) || ( p->flags & AVRPART_HAS_PDI ) ) {
      /* debugWire cannot use page access for EEPROM */
      cmd[1] = MTYPE_EEPROM;
    } else {
      cmd[1] = MTYPE_EEPROM_PAGE;
      pagesize = mem->page_size;
      paddr = addr & ~(pagesize - 1);
      paddr_ptr = &PDATA(pgm)->eeprom_pageaddr;
      cache_ptr = PDATA(pgm)->eeprom_pagecache;
    }
  } else if (strcmp(mem->desc, "lfuse") == 0) {
    cmd[1] = MTYPE_FUSE_BITS;
    addr = 0;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "hfuse") == 0) {
    cmd[1] = MTYPE_FUSE_BITS;
    addr = 1;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "efuse") == 0) {
    cmd[1] = MTYPE_FUSE_BITS;
    addr = 2;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strncmp(mem->desc, "lock", 4) == 0) {
    cmd[1] = MTYPE_LOCK_BITS;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strncmp(mem->desc, "fuse", strlen("fuse")) == 0) {
    cmd[1] = MTYPE_FUSE_BITS;
  } else if (strcmp(mem->desc, "usersig") == 0) {
    cmd[1] = MTYPE_USERSIG;
  } else if (strcmp(mem->desc, "prodsig") == 0) {
    cmd[1] = MTYPE_PRODSIG;
  } else if (strcmp(mem->desc, "calibration") == 0) {
    cmd[1] = MTYPE_OSCCAL_BYTE;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "signature") == 0) {
    cmd[1] = MTYPE_SIGN_JTAG;

    if (pgm->flag & PGM_FL_IS_DW) {
      /*
       * In debugWire mode, there is no accessible memory area to read
       * the signature from, but the essential two bytes can be read
       * as a parameter from the ICE.
       */
      unsigned char parm[4];

      switch (addr) {
      case 0:
	*value = 0x1E;		/* Atmel vendor ID */
	break;

      case 1:
      case 2:
	if (jtagmkII_getparm(pgm, PAR_TARGET_SIGNATURE, parm) < 0)
	  return -1;
	*value = parm[2 - addr];
	break;

      default:
	avrdude_message(MSG_INFO, "%s: illegal address %lu for signature memory\n",
		progname, addr);
	return -1;
      }
      return 0;
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
    u32_to_b4(cmd + 2, pagesize);
    u32_to_b4(cmd + 6, paddr);
  } else {
    u32_to_b4(cmd + 2, 1);
    u32_to_b4(cmd + 6, addr);
  }

  tries = 0;
  retry:
    avrdude_message(MSG_NOTICE2, "%s: jtagmkII_read_byte(): Sending read memory command: ",
	    progname);
  jtagmkII_send(pgm, cmd, 10);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_read_byte(): "
	      "timeout/error communicating with programmer (status %d)\n",
	      progname, status);
    if (tries++ < 3)
      goto retry;
    avrdude_message(MSG_INFO, "%s: jtagmkII_read_byte(): "
	    "fatal timeout/error communicating with programmer (status %d)\n",
	    progname, status);
    if (status < 0)
      resp = 0;
    goto fail;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  if (resp[0] != RSP_MEMORY) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_read_byte(): "
	    "bad response to read memory command: %s\n",
	    progname, jtagmkII_get_rc(resp[0]));
    goto fail;
  }

  if (pagesize) {
    *paddr_ptr = paddr;
    memcpy(cache_ptr, resp + 1, pagesize);
    *value = cache_ptr[addr & (pagesize - 1)];
  } else
    *value = resp[1];

  free(resp);
  return 0;

fail:
  free(resp);
  return -1;
}

static int jtagmkII_write_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
			       unsigned long addr, unsigned char data)
{
  unsigned char cmd[12];
  unsigned char *resp = NULL, writedata, writedata2 = 0xFF;
  int status, tries, need_progmode = 1, unsupp = 0, writesize = 1;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_write_byte(.., %s, 0x%lx, ...)\n",
	    progname, mem->desc, addr);

  addr += mem->offset;

  writedata = data;
  cmd[0] = CMND_WRITE_MEMORY;
  cmd[1] = ( p->flags & AVRPART_HAS_PDI ) ? MTYPE_FLASH : MTYPE_SPM;
  if (strcmp(mem->desc, "flash") == 0) {
     if ((addr & 1) == 1) {
       /* odd address = high byte */
       writedata = 0xFF;	/* don't modify the low byte */
       writedata2 = data;
       addr &= ~1L;
     }
     writesize = 2;
     need_progmode = 0;
     PDATA(pgm)->flash_pageaddr = (unsigned long)-1L;
     if (pgm->flag & PGM_FL_IS_DW)
       unsupp = 1;
  } else if (strcmp(mem->desc, "eeprom") == 0) {
    cmd[1] = ( p->flags & AVRPART_HAS_PDI ) ? MTYPE_EEPROM_XMEGA: MTYPE_EEPROM;
    need_progmode = 0;
    PDATA(pgm)->eeprom_pageaddr = (unsigned long)-1L;
  } else if (strcmp(mem->desc, "lfuse") == 0) {
    cmd[1] = MTYPE_FUSE_BITS;
    addr = 0;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "hfuse") == 0) {
    cmd[1] = MTYPE_FUSE_BITS;
    addr = 1;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "efuse") == 0) {
    cmd[1] = MTYPE_FUSE_BITS;
    addr = 2;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strncmp(mem->desc, "fuse", strlen("fuse")) == 0) {
    cmd[1] = MTYPE_FUSE_BITS;
  } else if (strcmp(mem->desc, "usersig") == 0) {
    cmd[1] = MTYPE_USERSIG;
  } else if (strcmp(mem->desc, "prodsig") == 0) {
    cmd[1] = MTYPE_PRODSIG;
  } else if (strncmp(mem->desc, "lock", 4) == 0) {
    cmd[1] = MTYPE_LOCK_BITS;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "calibration") == 0) {
    cmd[1] = MTYPE_OSCCAL_BYTE;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  } else if (strcmp(mem->desc, "signature") == 0) {
    cmd[1] = MTYPE_SIGN_JTAG;
    if (pgm->flag & PGM_FL_IS_DW)
      unsupp = 1;
  }

  if (unsupp)
    return -1;

  if (need_progmode) {
    if (jtagmkII_program_enable(pgm) < 0)
      return -1;
  } else {
    if (jtagmkII_program_disable(pgm) < 0)
      return -1;
  }

  u32_to_b4(cmd + 2, writesize);
  u32_to_b4(cmd + 6, addr);
  cmd[10] = writedata;
  cmd[11] = writedata2;

  tries = 0;
  retry:
  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_write_byte(): Sending write memory command: ",
	    progname);
  jtagmkII_send(pgm, cmd, 10 + writesize);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_NOTICE2, "%s: jtagmkII_write_byte(): "
	      "timeout/error communicating with programmer (status %d)\n",
	      progname, status);
    if (tries++ < 3)
      goto retry;
    avrdude_message(MSG_INFO, "%s: jtagmkII_write_byte(): "
	    "fatal timeout/error communicating with programmer (status %d)\n",
	    progname, status);
    goto fail;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  if (resp[0] != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_write_byte(): "
	    "bad response to write memory command: %s\n",
	    progname, jtagmkII_get_rc(resp[0]));
    goto fail;
  }

  free(resp);
  return 0;

fail:
  free(resp);
  return -1;
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
static int jtagmkII_set_sck_period(PROGRAMMER * pgm, double v)
{
  unsigned char dur;

  v = 1 / v;			/* convert to frequency */
  if (v >= 6.4e6)
    dur = 0;
  else if (v >= 2.8e6)
    dur = 1;
  else if (v >= 20.9e3)
    dur = (unsigned char)(5.35e6 / v);
  else
    dur = 255;

  return jtagmkII_setparm(pgm, PAR_OCD_JTAG_CLK, &dur);
}


/*
 * Read an emulator parameter.  As the maximal parameter length is 4
 * bytes by now, we always copy out 4 bytes to *value, so the caller
 * must have allocated sufficient space.
 */
int jtagmkII_getparm(PROGRAMMER * pgm, unsigned char parm,
		     unsigned char * value)
{
  int status;
  unsigned char buf[2], *resp, c;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_getparm()\n", progname);

  buf[0] = CMND_GET_PARAMETER;
  buf[1] = parm;
  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_getparm(): "
	    "Sending get parameter command (parm 0x%02x): ",
	    progname, parm);
  jtagmkII_send(pgm, buf, 2);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_getparm(): "
	    "timeout/error communicating with programmer (status %d)\n",
	    progname, status);
    return -1;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  if (c != RSP_PARAMETER) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_getparm(): "
	    "bad response to get parameter command: %s\n",
	    progname, jtagmkII_get_rc(c));
    free(resp);
    return -1;
  }

  memcpy(value, resp + 1, 4);
  free(resp);

  return 0;
}

/*
 * Write an emulator parameter.
 */
static int jtagmkII_setparm(PROGRAMMER * pgm, unsigned char parm,
			    unsigned char * value)
{
  int status;
  /*
   * As the maximal parameter length is 4 bytes, we use a fixed-length
   * buffer, as opposed to malloc()ing it.
   */
  unsigned char buf[2 + 4], *resp, c;
  size_t size;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_setparm()\n", progname);

  switch (parm) {
  case PAR_HW_VERSION: size = 2; break;
  case PAR_FW_VERSION: size = 4; break;
  case PAR_EMULATOR_MODE: size = 1; break;
  case PAR_BAUD_RATE: size = 1; break;
  case PAR_OCD_VTARGET: size = 2; break;
  case PAR_OCD_JTAG_CLK: size = 1; break;
  case PAR_TIMERS_RUNNING: size = 1; break;
  case PAR_EXTERNAL_RESET: size = 1; break;
  case PAR_DAISY_CHAIN_INFO: size = 4; break;
  case PAR_PDI_OFFSET_START:
  case PAR_PDI_OFFSET_END: size = 4; break;
  default:
    avrdude_message(MSG_INFO, "%s: jtagmkII_setparm(): unknown parameter 0x%02x\n",
	    progname, parm);
    return -1;
  }

  buf[0] = CMND_SET_PARAMETER;
  buf[1] = parm;
  memcpy(buf + 2, value, size);
  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_setparm(): "
	    "Sending set parameter command (parm 0x%02x, %u bytes): ",
	    progname, parm, (unsigned)size);
  jtagmkII_send(pgm, buf, size + 2);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_setparm(): "
	    "timeout/error communicating with programmer (status %d)\n",
	    progname, status);
    return -1;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  free(resp);
  if (c != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_setparm(): "
	    "bad response to set parameter command: %s\n",
	    progname, jtagmkII_get_rc(c));
    return -1;
  }

  return 0;
}


static void jtagmkII_display(PROGRAMMER * pgm, const char * p)
{
  unsigned char hw[4], fw[4];

  if (jtagmkII_getparm(pgm, PAR_HW_VERSION, hw) < 0 ||
      jtagmkII_getparm(pgm, PAR_FW_VERSION, fw) < 0)
    return;

  avrdude_message(MSG_INFO, "%sM_MCU hardware version: %d\n", p, hw[0]);
  avrdude_message(MSG_INFO, "%sM_MCU firmware version: %d.%02d\n", p, fw[1], fw[0]);
  avrdude_message(MSG_INFO, "%sS_MCU hardware version: %d\n", p, hw[1]);
  avrdude_message(MSG_INFO, "%sS_MCU firmware version: %d.%02d\n", p, fw[3], fw[2]);
  avrdude_message(MSG_INFO, "%sSerial number:          %02x:%02x:%02x:%02x:%02x:%02x\n",
	  p, PDATA(pgm)->serno[0], PDATA(pgm)->serno[1], PDATA(pgm)->serno[2], PDATA(pgm)->serno[3], PDATA(pgm)->serno[4], PDATA(pgm)->serno[5]);

  jtagmkII_print_parms1(pgm, p);

  return;
}


static void jtagmkII_print_parms1(PROGRAMMER * pgm, const char * p)
{
  unsigned char vtarget[4], jtag_clock[4];
  char clkbuf[20];
  double clk;

  if (jtagmkII_getparm(pgm, PAR_OCD_VTARGET, vtarget) < 0)
    return;

  avrdude_message(MSG_INFO, "%sVtarget         : %.1f V\n", p,
	  b2_to_u16(vtarget) / 1000.0);

  if ((pgm->flag & PGM_FL_IS_JTAG)) {
    if (jtagmkII_getparm(pgm, PAR_OCD_JTAG_CLK, jtag_clock) < 0)
      return;

    if (jtag_clock[0] == 0) {
      strcpy(clkbuf, "6.4 MHz");
      clk = 6.4e6;
    } else if (jtag_clock[0] == 1) {
      strcpy(clkbuf, "2.8 MHz");
      clk = 2.8e6;
    } else if (jtag_clock[0] <= 5) {
      sprintf(clkbuf, "%.1f MHz", 5.35 / (double)jtag_clock[0]);
      clk = 5.35e6 / (double)jtag_clock[0];
    } else {
      sprintf(clkbuf, "%.1f kHz", 5.35e3 / (double)jtag_clock[0]);
      clk = 5.35e6 / (double)jtag_clock[0];

      avrdude_message(MSG_INFO, "%sJTAG clock      : %s (%.1f us)\n", p, clkbuf,
	      1.0e6 / clk);
    }
  }

  return;
}

static void jtagmkII_print_parms(PROGRAMMER * pgm)
{
  jtagmkII_print_parms1(pgm, "");
}

static unsigned char jtagmkII_memtype(PROGRAMMER * pgm, AVRPART * p, unsigned long addr)
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

static unsigned int jtagmkII_memaddr(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, unsigned long addr)
{
  /*
   * Xmega devices handled by V7+ firmware don't want to be told their
   * m->offset within the write memory command.
   */
  if (PDATA(pgm)->fwver >= 0x700 && (p->flags & AVRPART_HAS_PDI) != 0) {
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
   * Old firmware, or non-Xmega device.  Non-Xmega (and non-AVR32)
   * devices always have an m->offset of 0, so we don't have to
   * distinguish them here.
   */
  return addr + m->offset;
}


#ifdef __OBJC__
#pragma mark -
#endif

static int jtagmkII_avr32_reset(PROGRAMMER * pgm, unsigned char val,
                                unsigned char ret1, unsigned char ret2)
{
  int status;
  unsigned char buf[3], *resp;

  avrdude_message(MSG_NOTICE, "%s: jtagmkII_avr32_reset(%2.2x)\n",
          progname, val);

  buf[0] = CMND_GET_IR;
  buf[1] = 0x0C;
  status = jtagmkII_send(pgm, buf, 2);
  if(status < 0) return -1;

  status = jtagmkII_recv(pgm, &resp);
  if (status != 2 || resp[0] != 0x87 || resp[1] != ret1) {
    avrdude_message(MSG_NOTICE, "%s: jtagmkII_avr32_reset(): "
	      "Get_IR, expecting %2.2x but got %2.2x\n",
	      progname, ret1, resp[1]);

    //return -1;
  }

  buf[0] = CMND_GET_xxx;
  buf[1] = 5;
  buf[2] = val;
  status = jtagmkII_send(pgm, buf, 3);
  if(status < 0) return -1;

  status = jtagmkII_recv(pgm, &resp);
  if (status != 2 || resp[0] != 0x87 || resp[1] != ret2) {
    avrdude_message(MSG_NOTICE, "%s: jtagmkII_avr32_reset(): "
	      "Get_XXX, expecting %2.2x but got %2.2x\n",
	      progname, ret2, resp[1]);
    //return -1;
  }

  return 0;
}

// At init: AVR32_RESET_READ_IR | AVR32_RESET_READ_READ_CHIPINFO
static int jtagmkII_reset32(PROGRAMMER * pgm, unsigned short flags)
{
  int status, j, lineno;
  unsigned char *resp, buf[3];
  unsigned long val=0;

  avrdude_message(MSG_NOTICE, "%s: jtagmkII_reset32(%2.2x)\n",
          progname, flags);

  status = -1;

  // Happens at the start of a programming operation
  if(flags & AVR32_RESET_READ) {
    buf[0] = CMND_GET_IR;
    buf[1] = 0x11;
    status = jtagmkII_send(pgm, buf, 2);
    if(status < 0) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_recv(pgm, &resp);
    if (status != 2 || resp[0] != 0x87 || resp[1] != 01)
      {lineno = __LINE__; goto eRR;};
  }

  if(flags & (AVR32_RESET_WRITE | AVR32_SET4RUNNING)) {
    // AVR_RESET(0x1F)
    status = jtagmkII_avr32_reset(pgm, 0x1F, 0x01, 0x00);
    if(status < 0) {lineno = __LINE__; goto eRR;}
    // AVR_RESET(0x07)
    status = jtagmkII_avr32_reset(pgm, 0x07, 0x11, 0x1F);
    if(status < 0) {lineno = __LINE__; goto eRR;}
  }

  //if(flags & AVR32_RESET_COMMON)
  {
    val = jtagmkII_read_SABaddr(pgm, AVR32_DS, 0x01);
    if(val != 0) {lineno = __LINE__; goto eRR;}
    val = jtagmkII_read_SABaddr(pgm, AVR32_DC, 0x01);
    if(val != 0) {lineno = __LINE__; goto eRR;}
  }

  if(flags & (AVR32_RESET_READ | AVR32_RESET_CHIP_ERASE)) {
    status = jtagmkII_write_SABaddr(pgm, AVR32_DC, 0x01,
                                    AVR32_DC_DBE | AVR32_DC_DBR);
    if(status < 0) return -1;
  }

  if(flags & (AVR32_RESET_WRITE | AVR32_SET4RUNNING)) {
    status = jtagmkII_write_SABaddr(pgm, AVR32_DC, 0x01,
             AVR32_DC_ABORT | AVR32_DC_RESET | AVR32_DC_DBE | AVR32_DC_DBR);
    if(status < 0) return -1;
    for(j=0; j<21; ++j) {
      val = jtagmkII_read_SABaddr(pgm, AVR32_DS, 0x01);
    }
    if(val != 0x04000000) {lineno = __LINE__; goto eRR;}

    // AVR_RESET(0x00)
    status = jtagmkII_avr32_reset(pgm, 0x00, 0x01, 0x07);
    if(status < 0) {lineno = __LINE__; goto eRR;}
  }
//  if(flags & (AVR32_RESET_READ | AVR32_RESET_WRITE))
  {
    for(j=0; j<2; ++j) {
      val = jtagmkII_read_SABaddr(pgm, AVR32_DS, 0x01);
      if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}
      if((val&0x05000020) != 0x05000020) {lineno = __LINE__; goto eRR;}
    }
  }

  //if(flags & (AVR32_RESET_READ | AVR32_RESET_WRITE | AVR32_RESET_CHIP_ERASE))
  {
    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe7b00044);  // mtdr 272, R0
    if(status < 0) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCSR, 0x01);
    if(val != 0x00000001) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCCPU, 0x01);
    if(val != 0x00000000) {lineno = __LINE__; goto eRR;}
  }

  // Read chip configuration - common for all
  if(flags & (AVR32_RESET_READ | AVR32_RESET_WRITE | AVR32_RESET_CHIP_ERASE)) {
    for(j=0; j<2; ++j) {
      val = jtagmkII_read_SABaddr(pgm, AVR32_DS, 0x01);
      if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}
      if((val&0x05000020) != 0x05000020) {lineno = __LINE__; goto eRR;}
    }

    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe7b00044);  // mtdr 272, R0
    if(status < 0) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCSR, 0x01);
    if(val != 0x00000001) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCCPU, 0x01);
    if(val != 0x00000000) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe1b00040);  // mfsr R0, 256
    if(status < 0) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe7b00044);  // mtdr 272, R0
    if(status < 0) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCSR, 0x01);
    if(val != 0x00000001) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCCPU, 0x01);
    if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DCEMU, 0x01, 0x00000000);
    if(status < 0) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe5b00045);  // mtdr R0, 276
    if(status < 0) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DS, 0x01);
    if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}
    if((val&0x05000020) != 0x05000020) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe7b00044);  // mtdr 272, R0
    if(status < 0) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCSR, 0x01);
    if(val != 0x00000001) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCCPU, 0x01);
    if(val != 0x00000000) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe1b00041);  // mfsr R0, 260
    if(status < 0) {lineno = __LINE__; goto eRR;}
    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe7b00044);  // mtdr 272, R0
    if(status < 0) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCSR, 0x01);
    if(val != 0x00000001) {lineno = __LINE__; goto eRR;}
    val = jtagmkII_read_SABaddr(pgm, AVR32_DCCPU, 0x01);
    if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DCEMU, 0x01, 0x00000000);
    if(status < 0) {lineno = __LINE__; goto eRR;}
    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe5b00045);  // mtdr R0, 276
    if(status < 0) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, 0x00000010, 0x06); // need to recheck who does this...
    if(val != 0x00000000) {lineno = __LINE__; goto eRR;}
  }

  if(flags & AVR32_RESET_CHIP_ERASE) {
    status = jtagmkII_avr32_reset(pgm, 0x1f, 0x01, 0x00);
    if(status < 0) {lineno = __LINE__; goto eRR;}
    status = jtagmkII_avr32_reset(pgm, 0x01, 0x11, 0x1f);
    if(status < 0) {lineno = __LINE__; goto eRR;}
  }

  if(flags & AVR32_SET4RUNNING) {
    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe1b00014);  // mfsr R0, 80
    if(status < 0) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe7b00044);  // mtdr 272, R0
    if(status < 0) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCSR, 0x01);
    if(val != 0x00000001) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DCCPU, 0x01);
    if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DCEMU, 0x01, 0x00000000);
    if(status < 0) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xe5b00045);  // mfdr R0, 276
    if(status < 0) {lineno = __LINE__; goto eRR;}

    val = jtagmkII_read_SABaddr(pgm, AVR32_DS, 0x01);
    if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}
    if((val&0x05000020) != 0x05000020) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_write_SABaddr(pgm, AVR32_DINST, 0x01, 0xd623d703);  // retd
    if(status < 0) {lineno = __LINE__; goto eRR;}
  }

  return 0;

  eRR:
    avrdude_message(MSG_INFO, "%s: jtagmkII_reset32(): "
	    "failed at line %d (status=%x val=%lx)\n",
	    progname, lineno, status, val);
    return -1;
}

static int jtagmkII_smc_init32(PROGRAMMER * pgm)
{
  int status, lineno;
  unsigned long val;

  // HMATRIX 0xFFFF1000
  status = jtagmkII_write_SABaddr(pgm, 0xffff1018, 0x05, 0x04000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1024, 0x05, 0x04000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1008, 0x05, 0x04000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1078, 0x05, 0x04000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1088, 0x05, 0x04000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  status = jtagmkII_write_SABaddr(pgm, 0xffff1018, 0x05, 0x08000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1024, 0x05, 0x08000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1008, 0x05, 0x08000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1078, 0x05, 0x08000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1088, 0x05, 0x08000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  status = jtagmkII_write_SABaddr(pgm, 0xffff1018, 0x05, 0x10000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1024, 0x05, 0x10000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1008, 0x05, 0x10000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1078, 0x05, 0x10000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1088, 0x05, 0x10000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  status = jtagmkII_write_SABaddr(pgm, 0xffff1018, 0x05, 0x00020000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1024, 0x05, 0x00020000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1008, 0x05, 0x00020000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1078, 0x05, 0x00020000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1088, 0x05, 0x00020000);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  status = jtagmkII_write_SABaddr(pgm, 0xffff1018, 0x05, 0x02000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1024, 0x05, 0x02000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1008, 0x05, 0x02000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1078, 0x05, 0x02000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xffff1088, 0x05, 0x02000000);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  status = jtagmkII_write_SABaddr(pgm, 0xfffe1c00, 0x05, 0x00010001);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xfffe1c04, 0x05, 0x05070a0b);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xfffe1c08, 0x05, 0x000b000c);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  status = jtagmkII_write_SABaddr(pgm, 0xfffe1c0c, 0x05, 0x00031103);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  // switchToClockSource
  val = jtagmkII_read_SABaddr(pgm, 0xffff0c28, 0x05);
  if (val != 0x00000000) {lineno = __LINE__; goto eRR;} // OSC 0
  status = jtagmkII_write_SABaddr(pgm, 0xffff0c28, 0x05, 0x0000607);
  if (status < 0) {lineno = __LINE__; goto eRR;}
  val = jtagmkII_read_SABaddr(pgm, 0xffff0c00, 0x05);
  if (val != 0x00000000) {lineno = __LINE__; goto eRR;} // PLL 0
  status = jtagmkII_write_SABaddr(pgm, 0xffff0c00, 0x05, 0x0000004);
  if (status < 0) {lineno = __LINE__; goto eRR;} // Power Manager
  status = jtagmkII_write_SABaddr(pgm, 0xffff0c00, 0x05, 0x0000005);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  usleep(1000000);

  val = jtagmkII_read_SABaddr(pgm, 0xfffe1408, 0x05);
  if (val != 0x0000a001) {lineno = __LINE__; goto eRR;} // PLL 0

  // need a small delay to let clock stabliize
  usleep(50*1000);

  return 0;

  eRR:
    avrdude_message(MSG_INFO, "%s: jtagmkII_smc_init32(): "
	    "failed at line %d\n",
	    progname, lineno);
    return -1;
}


/*
 * initialize the AVR device and prepare it to accept commands
 */
static int jtagmkII_initialize32(PROGRAMMER * pgm, AVRPART * p)
{
  int status, j;
  unsigned char buf[6], *resp;

  if (jtagmkII_setparm(pgm, PAR_DAISY_CHAIN_INFO, PDATA(pgm)->jtagchain) < 0) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_initialize(): Failed to setup JTAG chain\n",
            progname);
    return -1;
  }

  free(PDATA(pgm)->flash_pagecache);
  free(PDATA(pgm)->eeprom_pagecache);
  if ((PDATA(pgm)->flash_pagecache = malloc(PDATA(pgm)->flash_pagesize)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_initialize(): Out of memory\n",
	    progname);
    return -1;
  }
  if ((PDATA(pgm)->eeprom_pagecache = malloc(PDATA(pgm)->eeprom_pagesize)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_initialize32(): Out of memory\n",
	    progname);
    free(PDATA(pgm)->flash_pagecache);
    return -1;
  }
  PDATA(pgm)->flash_pageaddr = PDATA(pgm)->eeprom_pageaddr = (unsigned long)-1L;

  for(j=0; j<2; ++j) {
    buf[0] = CMND_GET_IR;
    buf[1] = 0x1;
    if(jtagmkII_send(pgm, buf, 2) < 0)
      return -1;
    status = jtagmkII_recv(pgm, &resp);
    if(status <= 0 || resp[0] != 0x87) {
      if (verbose >= 2)
        putc('\n', stderr);
      avrdude_message(MSG_INFO, "%s: jtagmkII_initialize32(): "
                "timeout/error communicating with programmer (status %d)\n",
                progname, status);
      return -1;
    }
    free(resp);

    memset(buf, 0, sizeof(buf));
    buf[0] = CMND_GET_xxx;
    buf[1] = 0x20;
    if(jtagmkII_send(pgm, buf, 6) < 0)
      return -1;
    status = jtagmkII_recv(pgm, &resp);
    if(status <= 0 || resp[0] != 0x87) {
      if (verbose >= 2)
        putc('\n', stderr);
      avrdude_message(MSG_INFO, "%s: jtagmkII_initialize32(): "
                "timeout/error communicating with programmer (status %d)\n",
                progname, status);
      return -1;
    }

    if (status != 5 ||
    resp[2] != p->signature[0] ||
    resp[3] != p->signature[1] ||
    resp[4] != p->signature[2]) {
      avrdude_message(MSG_INFO, "%s: Expected signature for %s is %02X %02X %02X\n",
          progname, p->desc,
          p->signature[0], p->signature[1], p->signature[2]);
      if (!ovsigck) {
        avrdude_message(MSG_INFO, "%sDouble check chip, "
        "or use -F to override this check.\n",
                progbuf);
        return -1;
      }
    }
    free(resp);
  }

  return 0;
}

static int jtagmkII_chip_erase32(PROGRAMMER * pgm, AVRPART * p)
{
  int status=0, loops;
  unsigned char *resp, buf[3], x, ret[4], *retP;
  unsigned long val=0;
  unsigned int lineno;

  avrdude_message(MSG_NOTICE, "%s: jtagmkII_chip_erase32()\n",
          progname);

  status = jtagmkII_reset32(pgm, AVR32_RESET_CHIP_ERASE);
  if(status != 0) {lineno = __LINE__; goto eRR;}

  // sequence of IR transitions
  ret[0] = 0x01;
  ret[1] = 0x05;
  ret[2] = 0x01;
  ret[3] = 0x00;

  retP = ret;
  for(loops=0; loops<1000; ++loops) {
    buf[0] = CMND_GET_IR;
    buf[1] = 0x0F;
    status = jtagmkII_send(pgm, buf, 2);
    if(status < 0) {lineno = __LINE__; goto eRR;}

    status = jtagmkII_recv(pgm, &resp);
    if (status != 2 || resp[0] != 0x87) {
      {lineno = __LINE__; goto eRR;}
    }
    x = resp[1];
    free(resp);
    if(x == *retP) ++retP;
    if(*retP == 0x00) break;
  }
  if(loops == 1000) {lineno = __LINE__; goto eRR;}

  status = jtagmkII_avr32_reset(pgm, 0x00, 0x01, 0x01);
  if(status < 0) {lineno = __LINE__; goto eRR;}

  val = jtagmkII_read_SABaddr(pgm, 0x00000010, 0x06);
  if(val != 0x00000000) {lineno = __LINE__; goto eRR;}

  // AVR32 "special"
  buf[0] = CMND_SET_PARAMETER;
  buf[1] = 0x03;
  buf[2] = 0x02;
  jtagmkII_send(pgm, buf, 3);
  status = jtagmkII_recv(pgm, &resp);
  if(status < 0 || resp[0] != RSP_OK) {lineno = __LINE__; goto eRR;}
  free(resp);

  return 0;

  eRR:
    avrdude_message(MSG_INFO, "%s: jtagmkII_reset32(): "
	    "failed at line %d (status=%x val=%lx)\n",
	    progname, lineno, status, val);
    return -1;
}

static unsigned long jtagmkII_read_SABaddr(PROGRAMMER * pgm, unsigned long addr,
                                           unsigned int prefix)
{
  unsigned char buf[6], *resp;
  int status;
  unsigned long val;
  unsigned long otimeout = serial_recv_timeout;

  serial_recv_timeout = 256;

  buf[0] = CMND_READ_SAB;
  buf[1] = prefix;
  u32_to_b4r(&buf[2], addr);

  if(jtagmkII_send(pgm, buf, 6) < 0)
    return ERROR_SAB;

  status = jtagmkII_recv(pgm, &resp);
  if(status <= 0 || resp[0] != 0x87) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_read_SABaddr(): "
	      "timeout/error communicating with programmer (status %d) resp=%x\n",
	      progname, status, resp[0]);
    serial_recv_timeout = otimeout;

    if(status > 0) {
      int i;
      avrdude_message(MSG_INFO, "Cmd: ");
      for(i=0; i<6; ++i) avrdude_message(MSG_INFO, "%2.2x ", buf[i]);
      avrdude_message(MSG_INFO, "\n");
      avrdude_message(MSG_INFO, "Data: ");
      for(i=0; i<status; ++i) avrdude_message(MSG_INFO, "%2.2x ", resp[i]);
      avrdude_message(MSG_INFO, "\n");
    }
    return ERROR_SAB;
  }

  if(status != 5) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_read_SABaddr(): "
	      "wrong number of bytes (status %d)\n",
	      progname, status);
    serial_recv_timeout = otimeout;
    return ERROR_SAB;
  }

  val = b4_to_u32r(&resp[1]);
  free(resp);

  if (verbose) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_read_SABaddr(): "
	      "OCD Register %lx -> %4.4lx\n",
	      progname, addr, val);
  }
  serial_recv_timeout = otimeout;
  return val;
}

static int jtagmkII_write_SABaddr(PROGRAMMER * pgm, unsigned long addr,
                                  unsigned int prefix, unsigned long val)
{
  unsigned char buf[10], *resp;
  int status;

  buf[0] = CMND_WRITE_SAB;
  buf[1] = prefix;
  u32_to_b4r(&buf[2], addr);
  u32_to_b4r(&buf[6], val);

  if(jtagmkII_send(pgm, buf, 10) < 0)
    return -1;

  status = jtagmkII_recv(pgm, &resp);
  if(status <= 0 || resp[0] != RSP_OK) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_write_SABaddr(): "
	      "timeout/error communicating with programmer (status %d)\n",
	      progname, status);
    return -1;
  }


  if (verbose) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_write_SABaddr(): "
	      "OCD Register %lx -> %4.4lx\n",
	      progname, addr, val);
  }
  return 0;
}

static int jtagmkII_open32(PROGRAMMER * pgm, char * port)
{
  int status;
  unsigned char buf[6], *resp;
  union pinfo pinfo;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_open32()\n", progname);

  /*
   * The JTAG ICE mkII always starts with a baud rate of 19200 Bd upon
   * attaching.  If the config file or command-line parameters specify
   * a higher baud rate, we switch to it later on, after establishing
   * the connection with the ICE.
   */
  pinfo.baud = 19200;

  /*
   * If the port name starts with "usb", divert the serial routines
   * to the USB ones.  The serial_open() function for USB overrides
   * the meaning of the "baud" parameter to be the USB device ID to
   * search for.
   */
  if (strncmp(port, "usb", 3) == 0) {
#if defined(HAVE_LIBUSB)
    serdev = &usb_serdev;
    pinfo.usbinfo.vid = USB_VENDOR_ATMEL;
    pinfo.usbinfo.flags = 0;
    pinfo.usbinfo.pid = USB_DEVICE_JTAGICEMKII;
    pgm->fd.usb.max_xfer = USBDEV_MAX_XFER_MKII;
    pgm->fd.usb.rep = USBDEV_BULK_EP_READ_MKII;
    pgm->fd.usb.wep = USBDEV_BULK_EP_WRITE_MKII;
    pgm->fd.usb.eep = 0;           /* no seperate EP for events */
#else
    avrdude_message(MSG_INFO, "avrdude was compiled without usb support.\n");
    return -1;
#endif
  }

  strcpy(pgm->port, port);
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  jtagmkII_drain(pgm, 0);

  status = jtagmkII_getsync(pgm, -1);
  if(status < 0) return -1;

  // AVR32 "special"
  buf[0] = CMND_SET_PARAMETER;
  buf[1] = 0x2D;
  buf[2] = 0x03;
  jtagmkII_send(pgm, buf, 3);
  status = jtagmkII_recv(pgm, &resp);
  if(status < 0 || resp[0] != RSP_OK)
    return -1;
  free(resp);

  buf[1] = 0x03;
  buf[2] = 0x02;
  jtagmkII_send(pgm, buf, 3);
  status = jtagmkII_recv(pgm, &resp);
  if(status < 0 || resp[0] != RSP_OK)
    return -1;
  free(resp);

  buf[1] = 0x03;
  buf[2] = 0x04;
  jtagmkII_send(pgm, buf, 3);
  status = jtagmkII_recv(pgm, &resp);
  if(status < 0 || resp[0] != RSP_OK)
    return -1;
  free(resp);

  return 0;
}

static void jtagmkII_close32(PROGRAMMER * pgm)
{
  int status, lineno;
  unsigned char *resp, buf[3], c;
  unsigned long val=0;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_close32()\n", progname);

  // AVR32 "special"
  buf[0] = CMND_SET_PARAMETER;
  buf[1] = 0x03;
  buf[2] = 0x02;
  jtagmkII_send(pgm, buf, 3);
  status = jtagmkII_recv(pgm, &resp);
  if(status < 0 || resp[0] != RSP_OK) {lineno = __LINE__; goto eRR;}
  free(resp);

  buf[0] = CMND_SIGN_OFF;
  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_close(): Sending sign-off command: ",
	    progname);
  jtagmkII_send(pgm, buf, 1);

  status = jtagmkII_recv(pgm, &resp);
  if (status <= 0) {
    if (verbose >= 2)
      putc('\n', stderr);
    avrdude_message(MSG_INFO, "%s: jtagmkII_close(): "
	    "timeout/error communicating with programmer (status %d)\n",
	    progname, status);
    return;
  }
  if (verbose >= 3) {
    putc('\n', stderr);
    jtagmkII_prmsg(pgm, resp, status);
  } else if (verbose == 2)
    avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
  c = resp[0];
  free(resp);
  if (c != RSP_OK) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_close(): "
	    "bad response to sign-off command: %s\n",
	    progname, jtagmkII_get_rc(c));
  }

  ret:
    serial_close(&pgm->fd);
    pgm->fd.ifd = -1;
    return;

  eRR:
    avrdude_message(MSG_INFO, "%s: jtagmkII_reset32(): "
	    "failed at line %d (status=%x val=%lx)\n",
	    progname, lineno, status, val);
    goto ret;
}

static int jtagmkII_paged_load32(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                 unsigned int page_size,
                                 unsigned int addr, unsigned int n_bytes)
{
  unsigned int block_size;
  unsigned int maxaddr = addr + n_bytes;
  unsigned char cmd[7];
  unsigned char *resp;
  int lineno, status;
  unsigned long val=0;
  long otimeout = serial_recv_timeout;

  avrdude_message(MSG_NOTICE2, "%s: jtagmkII_paged_load32(.., %s, %d, %d)\n",
	    progname, m->desc, page_size, n_bytes);

  serial_recv_timeout = 256;

  if(!(p->flags & AVRPART_WRITE)) {
    status = jtagmkII_reset32(pgm, AVR32_RESET_READ);
    if(status != 0) {lineno = __LINE__; goto eRR;}
  }

  // Init SMC and set clocks
  if(!(p->flags & AVRPART_INIT_SMC)) {
    status = jtagmkII_smc_init32(pgm);
    if(status != 0) {lineno = __LINE__; goto eRR;} // PLL 0
    p->flags |= AVRPART_INIT_SMC;
  }

  // Init SMC and set clocks
  if(!(p->flags & AVRPART_INIT_SMC)) {
    status = jtagmkII_smc_init32(pgm);
    if(status != 0) {lineno = __LINE__; goto eRR;} // PLL 0
    p->flags |= AVRPART_INIT_SMC;
  }

  //avrdude_message(MSG_INFO, "\n pageSize=%d bytes=%d pages=%d m->offset=0x%x pgm->page_size %d\n",
  //        page_size, n_bytes, pages, m->offset, pgm->page_size);

  cmd[0] = CMND_READ_MEMORY32;
  cmd[1] = 0x40;
  cmd[2] = 0x05;

  for (; addr < maxaddr; addr += block_size) {
    block_size = ((maxaddr-addr) < pgm->page_size) ? (maxaddr - addr) : pgm->page_size;
    avrdude_message(MSG_DEBUG, "%s: jtagmkII_paged_load32(): "
              "block_size at addr %d is %d\n",
              progname, addr, block_size);

    u32_to_b4r(cmd + 3, m->offset + addr);

    status = jtagmkII_send(pgm, cmd, 7);
    if(status<0) {lineno = __LINE__; goto eRR;}
    status = jtagmkII_recv(pgm, &resp);
    if(status<0) {lineno = __LINE__; goto eRR;}

    if (verbose >= 3) {
      putc('\n', stderr);
      jtagmkII_prmsg(pgm, resp, status);
    } else if (verbose == 2)
      avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
    if (resp[0] != 0x87) {
      avrdude_message(MSG_INFO, "%s: jtagmkII_paged_load32(): "
              "bad response to write memory command: %s\n",
              progname, jtagmkII_get_rc(resp[0]));
      free(resp);
      return -1;
    }
    memcpy(m->buf + addr, resp + 1, block_size);
    free(resp);

  }

  serial_recv_timeout = otimeout;

  status = jtagmkII_reset32(pgm, AVR32_SET4RUNNING);
  if(status < 0) {lineno = __LINE__; goto eRR;}

  return addr;

  eRR:
    serial_recv_timeout = otimeout;
    avrdude_message(MSG_INFO, "%s: jtagmkII_paged_load32(): "
	    "failed at line %d (status=%x val=%lx)\n",
	    progname, lineno, status, val);
    return -1;
}

static int jtagmkII_paged_write32(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned int page_size,
                                  unsigned int addr, unsigned int n_bytes)
{
  unsigned int block_size;
  unsigned char *cmd=NULL;
  unsigned char *resp;
  int lineno, status, pages, sPageNum, pageNum, blocks;
  unsigned long val=0;
  unsigned long otimeout = serial_recv_timeout;
  unsigned int maxaddr = addr + n_bytes;

  serial_recv_timeout = 256;

  if(n_bytes == 0) return -1;

  status = jtagmkII_reset32(pgm, AVR32_RESET_WRITE);
  if(status != 0) {lineno = __LINE__; goto eRR;}
  p->flags |= AVRPART_WRITE;

  pages = (n_bytes - addr - 1)/page_size + 1;
  sPageNum = addr/page_size;
  //avrdude_message(MSG_INFO, "\n pageSize=%d bytes=%d pages=%d m->offset=0x%x pgm->page_size %d\n",
  //        page_size, n_bytes, pages, m->offset, pgm->page_size);

  // Before any errors can happen
  if ((cmd = malloc(pgm->page_size + 10)) == NULL) {
    avrdude_message(MSG_INFO, "%s: jtagmkII_paged_write32(): Out of memory\n", progname);
    return -1;
  }

  // Init SMC and set clocks
  if(!(p->flags & AVRPART_INIT_SMC)) {
    status = jtagmkII_smc_init32(pgm);
    if(status != 0) {lineno = __LINE__; goto eRR;} // PLL 0
    p->flags |= AVRPART_INIT_SMC;
  }

  // First unlock the pages
  for(pageNum=sPageNum; pageNum < pages; ++pageNum) {
    status =jtagmkII_flash_lock32(pgm, 0, pageNum);
    if(status < 0) {lineno = __LINE__; goto eRR;}
  }

  // Then erase them (guess could do this in the same loop above?)
  for(pageNum=sPageNum; pageNum < pages; ++pageNum) {
    status =jtagmkII_flash_erase32(pgm, pageNum);
    if(status < 0) {lineno = __LINE__; goto eRR;}
  }

  cmd[0] = CMND_WRITE_MEMORY32;
  u32_to_b4r(&cmd[1], 0x40000000);  // who knows
  cmd[5] = 0x5;

  for(pageNum=sPageNum; pageNum < pages; ++pageNum) {

    status = jtagmkII_flash_clear_pagebuffer32(pgm);
    if(status != 0) {lineno = __LINE__; goto eRR;}

    for(blocks=0; blocks<2; ++blocks) {
      block_size = ((maxaddr-addr) < pgm->page_size) ? (maxaddr - addr) : pgm->page_size;
      avrdude_message(MSG_DEBUG, "%s: jtagmkII_paged_write32(): "
                "block_size at addr %d is %d\n",
                progname, addr, block_size);

      u32_to_b4r(cmd + 6, m->offset + addr);
      memset(cmd + 10, 0xff, pgm->page_size);
      memcpy(cmd + 10, m->buf + addr, block_size);

      status = jtagmkII_send(pgm, cmd, pgm->page_size + 10);
      if(status<0) {lineno = __LINE__; goto eRR;}
      status = jtagmkII_recv(pgm, &resp);
      if (status<0) {lineno = __LINE__; goto eRR;}

      if (verbose >= 3) {
        putc('\n', stderr);
        jtagmkII_prmsg(pgm, resp, status);
      } else if (verbose == 2)
        avrdude_message(MSG_NOTICE2, "0x%02x (%d bytes msg)\n", resp[0], status);
      if (resp[0] != RSP_OK) {
        avrdude_message(MSG_INFO, "%s: jtagmkII_paged_write32(): "
                "bad response to write memory command: %s\n",
                progname, jtagmkII_get_rc(resp[0]));
        free(resp);
        free(cmd);
        return -1;
      }
      free(resp);

      addr += block_size;


    }
    status = jtagmkII_flash_write_page32(pgm, pageNum);
    if(status < 0) {lineno = __LINE__; goto eRR;}
  }
  free(cmd);
  serial_recv_timeout = otimeout;

  status = jtagmkII_reset32(pgm, AVR32_SET4RUNNING);  // AVR32_SET4RUNNING | AVR32_RELEASE_JTAG
  if(status < 0) {lineno = __LINE__; goto eRR;}

  return addr;

  eRR:
    serial_recv_timeout = otimeout;
    free(cmd);
    avrdude_message(MSG_INFO, "%s: jtagmkII_paged_write32(): "
	    "failed at line %d (status=%x val=%lx)\n",
	    progname, lineno, status, val);
    return -1;
}


static int jtagmkII_flash_lock32(PROGRAMMER * pgm, unsigned char lock, unsigned int page)
{
  int status, lineno, i;
  unsigned long val, cmd=0;

  for(i=0; i<256; ++i) {
    val = jtagmkII_read_SABaddr(pgm, AVR32_FLASHC_FSR, 0x05);
    if(val == ERROR_SAB) continue;
    if(val & AVR32_FLASHC_FSR_RDY) break;
  }
  if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}
  if(!(val&AVR32_FLASHC_FSR_RDY)) {lineno = __LINE__; goto eRR;} // Flash better be ready

  page <<= 8;
  cmd = AVR32_FLASHC_FCMD_KEY | page | (lock ? AVR32_FLASHC_FCMD_LOCK : AVR32_FLASHC_FCMD_UNLOCK);
  status = jtagmkII_write_SABaddr(pgm, AVR32_FLASHC_FCMD, 0x05, cmd);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  return 0;

  eRR:
    avrdude_message(MSG_INFO, "%s: jtagmkII_flash_lock32(): "
	    "failed at line %d page %d cmd %8.8lx\n",
	    progname, lineno, page, cmd);
    return -1;
}

static int jtagmkII_flash_erase32(PROGRAMMER * pgm, unsigned int page)
{
  int status, lineno, i;
  unsigned long val=0, cmd=0, err=0;

  for(i=0; i<256; ++i) {
    val = jtagmkII_read_SABaddr(pgm, AVR32_FLASHC_FSR, 0x05);
    if(val == ERROR_SAB) continue;
    if(val & AVR32_FLASHC_FSR_RDY) break;
  }
  if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}
  if(!(val&AVR32_FLASHC_FSR_RDY)) {lineno = __LINE__; goto eRR;} // Flash better be ready

  page <<= 8;
  cmd = AVR32_FLASHC_FCMD_KEY | page | AVR32_FLASHC_FCMD_ERASE_PAGE;
  status = jtagmkII_write_SABaddr(pgm, AVR32_FLASHC_FCMD, 0x05, cmd);
  if (status < 0) {lineno = __LINE__; goto eRR;}

//avrdude_message(MSG_INFO, "ERASE %x -> %x\n", cmd, AVR32_FLASHC_FCMD);

  err = 0;
  for(i=0; i<256; ++i) {
    val = jtagmkII_read_SABaddr(pgm, AVR32_FLASHC_FSR, 0x05);
    if(val == ERROR_SAB) continue;
    err |= val;
    if(val & AVR32_FLASHC_FSR_RDY) break;
  }
  if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}
  if(!(val & AVR32_FLASHC_FSR_RDY)) {lineno = __LINE__; goto eRR;}
  if(err & AVR32_FLASHC_FSR_ERR) {lineno = __LINE__; goto eRR;}

  return 0;

  eRR:
    avrdude_message(MSG_INFO, "%s: jtagmkII_flash_erase32(): "
	    "failed at line %d page %d cmd %8.8lx val %lx\n",
	    progname, lineno, page, cmd, val);
    return -1;
}

static int jtagmkII_flash_write_page32(PROGRAMMER * pgm, unsigned int page)
{
  int status, lineno, i;
  unsigned long val=0, cmd, err;

  page <<= 8;
  cmd = AVR32_FLASHC_FCMD_KEY | page | AVR32_FLASHC_FCMD_WRITE_PAGE;
  status = jtagmkII_write_SABaddr(pgm, AVR32_FLASHC_FCMD, 0x05, cmd);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  err = 0;
  for(i=0; i<256; ++i) {
    val = jtagmkII_read_SABaddr(pgm, AVR32_FLASHC_FSR, 0x05);
    if(val == ERROR_SAB) continue;
    err |= val;
    if(val & AVR32_FLASHC_FSR_RDY) break;
  }
  if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}
  if(!(val & AVR32_FLASHC_FSR_RDY)) {lineno = __LINE__; goto eRR;}
  if(err & AVR32_FLASHC_FSR_ERR) {lineno = __LINE__; goto eRR;}

  return 0;

  eRR:
    avrdude_message(MSG_INFO, "%s: jtagmkII_flash_write_page32(): "
	    "failed at line %d page %d cmd %8.8lx val %lx\n",
	    progname, lineno, page, cmd, val);
    return -1;
}

static int jtagmkII_flash_clear_pagebuffer32(PROGRAMMER * pgm)
{
  int status, lineno, i;
  unsigned long val=0, cmd, err;

  cmd = AVR32_FLASHC_FCMD_KEY | AVR32_FLASHC_FCMD_CLEAR_PAGE_BUFFER;
  status = jtagmkII_write_SABaddr(pgm, AVR32_FLASHC_FCMD, 0x05, cmd);
  if (status < 0) {lineno = __LINE__; goto eRR;}

  err = 0;
  for(i=0; i<256; ++i) {
    val = jtagmkII_read_SABaddr(pgm, AVR32_FLASHC_FSR, 0x05);
    if(val == ERROR_SAB) continue;
    err |= val;
    if(val & AVR32_FLASHC_FSR_RDY) break;
  }
  if(val == ERROR_SAB) {lineno = __LINE__; goto eRR;}
  if(!(val & AVR32_FLASHC_FSR_RDY)) {lineno = __LINE__; goto eRR;}
  if(err & AVR32_FLASHC_FSR_ERR) {lineno = __LINE__; goto eRR;}

  return 0;

  eRR:
    avrdude_message(MSG_INFO, "%s: jtagmkII_flash_clear_pagebuffer32(): "
	    "failed at line %d cmd %8.8lx val %lx\n",
	    progname, lineno, cmd, val);
    return -1;
}

#ifdef __OBJC__
#pragma mark -
#endif

const char jtagmkII_desc[] = "Atmel JTAG ICE mkII";

void jtagmkII_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "JTAGMKII");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtagmkII_initialize;
  pgm->display        = jtagmkII_display;
  pgm->enable         = jtagmkII_enable;
  pgm->disable        = jtagmkII_disable;
  pgm->program_enable = jtagmkII_program_enable_INFO;
  pgm->chip_erase     = jtagmkII_chip_erase;
  pgm->open           = jtagmkII_open;
  pgm->close          = jtagmkII_close;
  pgm->read_byte      = jtagmkII_read_byte;
  pgm->write_byte     = jtagmkII_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtagmkII_paged_write;
  pgm->paged_load     = jtagmkII_paged_load;
  pgm->page_erase     = jtagmkII_page_erase;
  pgm->print_parms    = jtagmkII_print_parms;
  pgm->set_sck_period = jtagmkII_set_sck_period;
  pgm->parseextparams = jtagmkII_parseextparms;
  pgm->setup          = jtagmkII_setup;
  pgm->teardown       = jtagmkII_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_JTAG;
}

const char jtagmkII_dw_desc[] = "Atmel JTAG ICE mkII in debugWire mode";

void jtagmkII_dw_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "JTAGMKII_DW");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtagmkII_initialize;
  pgm->display        = jtagmkII_display;
  pgm->enable         = jtagmkII_enable;
  pgm->disable        = jtagmkII_disable;
  pgm->program_enable = jtagmkII_program_enable_INFO;
  pgm->chip_erase     = jtagmkII_chip_erase_dw;
  pgm->open           = jtagmkII_open_dw;
  pgm->close          = jtagmkII_close;
  pgm->read_byte      = jtagmkII_read_byte;
  pgm->write_byte     = jtagmkII_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtagmkII_paged_write;
  pgm->paged_load     = jtagmkII_paged_load;
  pgm->print_parms    = jtagmkII_print_parms;
  pgm->setup          = jtagmkII_setup;
  pgm->teardown       = jtagmkII_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_DW;
}

const char jtagmkII_pdi_desc[] = "Atmel JTAG ICE mkII in PDI mode";

void jtagmkII_pdi_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "JTAGMKII_PDI");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtagmkII_initialize;
  pgm->display        = jtagmkII_display;
  pgm->enable         = jtagmkII_enable;
  pgm->disable        = jtagmkII_disable;
  pgm->program_enable = jtagmkII_program_enable_INFO;
  pgm->chip_erase     = jtagmkII_chip_erase;
  pgm->open           = jtagmkII_open_pdi;
  pgm->close          = jtagmkII_close;
  pgm->read_byte      = jtagmkII_read_byte;
  pgm->write_byte     = jtagmkII_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtagmkII_paged_write;
  pgm->paged_load     = jtagmkII_paged_load;
  pgm->page_erase     = jtagmkII_page_erase;
  pgm->print_parms    = jtagmkII_print_parms;
  pgm->setup          = jtagmkII_setup;
  pgm->teardown       = jtagmkII_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_PDI;
}

const char jtagmkII_dragon_desc[] = "Atmel AVR Dragon in JTAG mode";

void jtagmkII_dragon_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "DRAGON_JTAG");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtagmkII_initialize;
  pgm->display        = jtagmkII_display;
  pgm->enable         = jtagmkII_enable;
  pgm->disable        = jtagmkII_disable;
  pgm->program_enable = jtagmkII_program_enable_INFO;
  pgm->chip_erase     = jtagmkII_chip_erase;
  pgm->open           = jtagmkII_dragon_open;
  pgm->close          = jtagmkII_close;
  pgm->read_byte      = jtagmkII_read_byte;
  pgm->write_byte     = jtagmkII_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtagmkII_paged_write;
  pgm->paged_load     = jtagmkII_paged_load;
  pgm->page_erase     = jtagmkII_page_erase;
  pgm->print_parms    = jtagmkII_print_parms;
  pgm->set_sck_period = jtagmkII_set_sck_period;
  pgm->parseextparams = jtagmkII_parseextparms;
  pgm->setup          = jtagmkII_setup;
  pgm->teardown       = jtagmkII_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_JTAG;
}

const char jtagmkII_dragon_dw_desc[] = "Atmel AVR Dragon in debugWire mode";

void jtagmkII_dragon_dw_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "DRAGON_DW");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtagmkII_initialize;
  pgm->display        = jtagmkII_display;
  pgm->enable         = jtagmkII_enable;
  pgm->disable        = jtagmkII_disable;
  pgm->program_enable = jtagmkII_program_enable_INFO;
  pgm->chip_erase     = jtagmkII_chip_erase_dw;
  pgm->open           = jtagmkII_dragon_open_dw;
  pgm->close          = jtagmkII_close;
  pgm->read_byte      = jtagmkII_read_byte;
  pgm->write_byte     = jtagmkII_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtagmkII_paged_write;
  pgm->paged_load     = jtagmkII_paged_load;
  pgm->print_parms    = jtagmkII_print_parms;
  pgm->setup          = jtagmkII_setup;
  pgm->teardown       = jtagmkII_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_DW;
}

const char jtagmkII_avr32_desc[] = "Atmel JTAG ICE mkII in AVR32 mode";

void jtagmkII_avr32_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "JTAGMKII_AVR32");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtagmkII_initialize32;
  pgm->display        = jtagmkII_display;
  pgm->enable         = jtagmkII_enable;
  pgm->disable        = jtagmkII_disable;
  pgm->program_enable = jtagmkII_program_enable_INFO;
  pgm->chip_erase     = jtagmkII_chip_erase32;
  pgm->open           = jtagmkII_open32;
  pgm->close          = jtagmkII_close32;
  pgm->read_byte      = jtagmkII_read_byte;
  pgm->write_byte     = jtagmkII_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtagmkII_paged_write32;
  pgm->paged_load     = jtagmkII_paged_load32;
  pgm->print_parms    = jtagmkII_print_parms;
  //pgm->set_sck_period = jtagmkII_set_sck_period;
  //pgm->parseextparams = jtagmkII_parseextparms;
  pgm->setup          = jtagmkII_setup;
  pgm->teardown       = jtagmkII_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_JTAG;
}

const char jtagmkII_dragon_pdi_desc[] = "Atmel AVR Dragon in PDI mode";

void jtagmkII_dragon_pdi_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "DRAGON_PDI");

  /*
   * mandatory functions
   */
  pgm->initialize     = jtagmkII_initialize;
  pgm->display        = jtagmkII_display;
  pgm->enable         = jtagmkII_enable;
  pgm->disable        = jtagmkII_disable;
  pgm->program_enable = jtagmkII_program_enable_INFO;
  pgm->chip_erase     = jtagmkII_chip_erase;
  pgm->open           = jtagmkII_dragon_open_pdi;
  pgm->close          = jtagmkII_close;
  pgm->read_byte      = jtagmkII_read_byte;
  pgm->write_byte     = jtagmkII_write_byte;

  /*
   * optional functions
   */
  pgm->paged_write    = jtagmkII_paged_write;
  pgm->paged_load     = jtagmkII_paged_load;
  pgm->page_erase     = jtagmkII_page_erase;
  pgm->print_parms    = jtagmkII_print_parms;
  pgm->setup          = jtagmkII_setup;
  pgm->teardown       = jtagmkII_teardown;
  pgm->page_size      = 256;
  pgm->flag           = PGM_FL_IS_PDI;
}

