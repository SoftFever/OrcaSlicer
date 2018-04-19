/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2003-2004  Theodore A. Roth  <troth@openavr.org>
 * Copyright 2007 Joerg Wunsch <j@uriah.heep.sax.de>
 * Copyright 2008 Klaus Leidinger <klaus@mikrocontroller-projekte.de>
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
 * avrdude interface for Atmel Low Cost Serial programmers which adher to the
 * protocol described in application note avr910.
 */

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "avr910.h"

/*
 * Private data for this programmer.
 */
struct pdata
{
  char has_auto_incr_addr;
  unsigned char devcode;
  unsigned int buffersize;
  unsigned char test_blockmode;
  unsigned char use_blockmode;
};

#define PDATA(pgm) ((struct pdata *)(pgm->cookie))

static void avr910_setup(PROGRAMMER * pgm)
{
  if ((pgm->cookie = malloc(sizeof(struct pdata))) == 0) {
    avrdude_message(MSG_INFO, "%s: avr910_setup(): Out of memory allocating private data\n",
                    progname);
    exit(1);
  }
  memset(pgm->cookie, 0, sizeof(struct pdata));
  PDATA(pgm)->test_blockmode = 1;
}

static void avr910_teardown(PROGRAMMER * pgm)
{
  free(pgm->cookie);
}


static int avr910_send(PROGRAMMER * pgm, char * buf, size_t len)
{
  return serial_send(&pgm->fd, (unsigned char *)buf, len);
}


static int avr910_recv(PROGRAMMER * pgm, char * buf, size_t len)
{
  int rv;

  rv = serial_recv(&pgm->fd, (unsigned char *)buf, len);
  if (rv < 0) {
    avrdude_message(MSG_INFO, "%s: avr910_recv(): programmer is not responding\n",
                    progname);
    return 1;
  }
  return 0;
}


static int avr910_drain(PROGRAMMER * pgm, int display)
{
  return serial_drain(&pgm->fd, display);
}


static int avr910_vfy_cmd_sent(PROGRAMMER * pgm, char * errmsg)
{
  char c;

  avr910_recv(pgm, &c, 1);
  if (c != '\r') {
    avrdude_message(MSG_INFO, "%s: error: programmer did not respond to command: %s\n",
            progname, errmsg);
    return 1;
  }
  return 0;
}


/*
 * issue the 'chip erase' command to the AVR device
 */
static int avr910_chip_erase(PROGRAMMER * pgm, AVRPART * p)
{
  avr910_send(pgm, "e", 1);
  if (avr910_vfy_cmd_sent(pgm, "chip erase") < 0)
    return -1;

  /*
   * avr910 firmware may not delay long enough
   */
  usleep (p->chip_erase_delay);

  return 0;
}


static int avr910_enter_prog_mode(PROGRAMMER * pgm)
{
  avr910_send(pgm, "P", 1);
  return avr910_vfy_cmd_sent(pgm, "enter prog mode");
}


static int avr910_leave_prog_mode(PROGRAMMER * pgm)
{
  avr910_send(pgm, "L", 1);
  return avr910_vfy_cmd_sent(pgm, "leave prog mode");
}


/*
 * issue the 'program enable' command to the AVR device
 */
static int avr910_program_enable(PROGRAMMER * pgm, AVRPART * p)
{
  return -1;
}


/*
 * initialize the AVR device and prepare it to accept commands
 */
static int avr910_initialize(PROGRAMMER * pgm, AVRPART * p)
{
  char id[8];
  char sw[2];
  char hw[2];
  char buf[10];
  char type;
  char c;
  AVRPART * part;

  /* Get the programmer identifier. Programmer returns exactly 7 chars
     _without_ the null.*/

  avr910_send(pgm, "S", 1);
  memset (id, 0, sizeof(id));
  avr910_recv(pgm, id, sizeof(id)-1);

  /* Get the HW and SW versions to see if the programmer is present. */

  avr910_send(pgm, "V", 1);
  avr910_recv(pgm, sw, sizeof(sw));

  avr910_send(pgm, "v", 1);
  avr910_recv(pgm, hw, sizeof(hw));

  /* Get the programmer type (serial or parallel). Expect serial. */

  avr910_send(pgm, "p", 1);
  avr910_recv(pgm, &type, 1);

  avrdude_message(MSG_INFO, "Found programmer: Id = \"%s\"; type = %c\n", id, type);
  avrdude_message(MSG_INFO, "    Software Version = %c.%c; ", sw[0], sw[1]);
  avrdude_message(MSG_INFO, "Hardware Version = %c.%c\n", hw[0], hw[1]);

  /* See if programmer supports autoincrement of address. */

  avr910_send(pgm, "a", 1);
  avr910_recv(pgm, &PDATA(pgm)->has_auto_incr_addr, 1);
  if (PDATA(pgm)->has_auto_incr_addr == 'Y')
      avrdude_message(MSG_INFO, "Programmer supports auto addr increment.\n");

  /* Check support for buffered memory access, ignore if not available */

  if (PDATA(pgm)->test_blockmode == 1) {
    avr910_send(pgm, "b", 1);
    avr910_recv(pgm, &c, 1);
    if (c == 'Y') {
      avr910_recv(pgm, &c, 1);
      PDATA(pgm)->buffersize = (unsigned int)(unsigned char)c<<8;
      avr910_recv(pgm, &c, 1);
      PDATA(pgm)->buffersize += (unsigned int)(unsigned char)c;
      avrdude_message(MSG_INFO, "Programmer supports buffered memory access with "
                      "buffersize = %u bytes.\n",
                      PDATA(pgm)->buffersize);
      PDATA(pgm)->use_blockmode = 1;
    } else {
      PDATA(pgm)->use_blockmode = 0;
    }
  } else {
    PDATA(pgm)->use_blockmode = 0;
  }

  if (PDATA(pgm)->devcode == 0) {
    char devtype_1st;
    int dev_supported = 0;

    /* Get list of devices that the programmer supports. */

    avr910_send(pgm, "t", 1);
    avrdude_message(MSG_INFO, "\nProgrammer supports the following devices:\n");
    devtype_1st = 0;
    while (1) {
      avr910_recv(pgm, &c, 1);
      if (devtype_1st == 0)
	devtype_1st = c;
      if (c == 0)
	break;
      part = locate_part_by_avr910_devcode(part_list, c);

      avrdude_message(MSG_INFO, "    Device code: 0x%02x = %s\n", c, part ?  part->desc : "(unknown)");

      /* FIXME: Need to lookup devcode and report the device. */

      if (p->avr910_devcode == c)
	dev_supported = 1;
    };
    avrdude_message(MSG_INFO, "\n");

    if (!dev_supported) {
      avrdude_message(MSG_INFO, "%s: %s: selected device is not supported by programmer: %s\n",
                      progname, ovsigck? "warning": "error", p->id);
      if (!ovsigck)
	return -1;
    }
    /* If the user forced the selection, use the first device
       type that is supported by the programmer. */
    buf[1] = ovsigck? devtype_1st: p->avr910_devcode;
  } else {
    /* devcode overridden by -x devcode= option */
    buf[1] = (char)(PDATA(pgm)->devcode);
  }

  /* Tell the programmer which part we selected. */
  buf[0] = 'T';
  /* buf[1] has been set up above */

  avr910_send(pgm, buf, 2);
  avr910_vfy_cmd_sent(pgm, "select device");

  avrdude_message(MSG_NOTICE, "%s: avr910_devcode selected: 0x%02x\n",
                  progname, (unsigned)buf[1]);

  avr910_enter_prog_mode(pgm);

  return 0;
}


static void avr910_disable(PROGRAMMER * pgm)
{
  /* Do nothing. */

  return;
}


static void avr910_enable(PROGRAMMER * pgm)
{
  /* Do nothing. */

  return;
}


/*
 * transmit an AVR device command and return the results; 'cmd' and
 * 'res' must point to at least a 4 byte data buffer
 */
static int avr910_cmd(PROGRAMMER * pgm, const unsigned char *cmd,
                      unsigned char *res)
{
  char buf[5];

  /* FIXME: Insert version check here */

  buf[0] = '.';                 /* New Universal Command */
  buf[1] = cmd[0];
  buf[2] = cmd[1];
  buf[3] = cmd[2];
  buf[4] = cmd[3];

  avr910_send (pgm, buf, 5);
  avr910_recv (pgm, buf, 2);

  res[0] = 0x00;                /* Dummy value */
  res[1] = cmd[0];
  res[2] = cmd[1];
  res[3] = buf[0];

  return 0;
}


static int avr910_parseextparms(PROGRAMMER * pgm, LISTID extparms)
{
  LNODEID ln;
  const char *extended_param;
  int rv = 0;

  for (ln = lfirst(extparms); ln; ln = lnext(ln)) {
    extended_param = ldata(ln);

    if (strncmp(extended_param, "devcode=", strlen("devcode=")) == 0) {
      int devcode;
      if (sscanf(extended_param, "devcode=%i", &devcode) != 1 ||
	  devcode <= 0 || devcode > 255) {
        avrdude_message(MSG_INFO, "%s: avr910_parseextparms(): invalid devcode '%s'\n",
                        progname, extended_param);
        rv = -1;
        continue;
      }
      avrdude_message(MSG_NOTICE2, "%s: avr910_parseextparms(): devcode overwritten as 0x%02x\n",
                      progname, devcode);
      PDATA(pgm)->devcode = devcode;

      continue;
    }
    if (strncmp(extended_param, "no_blockmode", strlen("no_blockmode")) == 0) {
      avrdude_message(MSG_NOTICE2, "%s: avr910_parseextparms(-x): no testing for Blockmode\n",
                      progname);
      PDATA(pgm)->test_blockmode = 0;

      continue;
    }

    avrdude_message(MSG_INFO, "%s: avr910_parseextparms(): invalid extended parameter '%s'\n",
                    progname, extended_param);
    rv = -1;
  }

  return rv;
}


static int avr910_open(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;
  /*
   *  If baudrate was not specified use 19.200 Baud
   */
  if(pgm->baudrate == 0) {
    pgm->baudrate = 19200;
  }

  strcpy(pgm->port, port);
  pinfo.baud = pgm->baudrate;
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  avr910_drain (pgm, 0);
	
  return 0;
}

static void avr910_close(PROGRAMMER * pgm)
{
  avr910_leave_prog_mode(pgm);

  serial_close(&pgm->fd);
  pgm->fd.ifd = -1;
}


static void avr910_display(PROGRAMMER * pgm, const char * p)
{
  return;
}


static void avr910_set_addr(PROGRAMMER * pgm, unsigned long addr)
{
  char cmd[3];

  cmd[0] = 'A';
  cmd[1] = (addr >> 8) & 0xff;
  cmd[2] = addr & 0xff;
  
  avr910_send(pgm, cmd, sizeof(cmd));
  avr910_vfy_cmd_sent(pgm, "set addr");
}


static int avr910_write_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                             unsigned long addr, unsigned char value)
{
  char cmd[2];

  if (strcmp(m->desc, "flash") == 0) {
    if (addr & 0x01) {
      cmd[0] = 'C';             /* Write Program Mem high byte */
    }
    else {
      cmd[0] = 'c';
    }

    addr >>= 1;
  }
  else if (strcmp(m->desc, "eeprom") == 0) {
    cmd[0] = 'D';
  }
  else {
    return avr_write_byte_default(pgm, p, m, addr, value);
  }

  cmd[1] = value;

  avr910_set_addr(pgm, addr);

  avr910_send(pgm, cmd, sizeof(cmd));
  avr910_vfy_cmd_sent(pgm, "write byte");

  return 0;
}


static int avr910_read_byte_flash(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned long addr, unsigned char * value)
{
  char buf[2];

  avr910_set_addr(pgm, addr >> 1);

  avr910_send(pgm, "R", 1);

  /* Read back the program mem word (MSB first) */
  avr910_recv(pgm, buf, sizeof(buf));

  if ((addr & 0x01) == 0) {
    *value = buf[1];
  }
  else {
    *value = buf[0];
  }

  return 0;
}


static int avr910_read_byte_eeprom(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                   unsigned long addr, unsigned char * value)
{
  avr910_set_addr(pgm, addr);
  avr910_send(pgm, "d", 1);
  avr910_recv(pgm, (char *)value, 1);

  return 0;
}


static int avr910_read_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                            unsigned long addr, unsigned char * value)
{
  if (strcmp(m->desc, "flash") == 0) {
    return avr910_read_byte_flash(pgm, p, m, addr, value);
  }

  if (strcmp(m->desc, "eeprom") == 0) {
    return avr910_read_byte_eeprom(pgm, p, m, addr, value);
  }

  return avr_read_byte_default(pgm, p, m, addr, value);
}


static int avr910_paged_write_flash(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, 
                                    unsigned int page_size,
                                    unsigned int addr, unsigned int n_bytes)
{
  unsigned char cmd[] = {'c', 'C'};
  char buf[2];
  unsigned int max_addr = addr + n_bytes;
  unsigned int page_addr;
  int page_bytes = page_size;
  int page_wr_cmd_pending = 0;

  page_addr = addr;
  avr910_set_addr(pgm, addr>>1);

  while (addr < max_addr) {
    page_wr_cmd_pending = 1;
    buf[0] = cmd[addr & 0x01];
    buf[1] = m->buf[addr];
    avr910_send(pgm, buf, sizeof(buf));
    avr910_vfy_cmd_sent(pgm, "write byte");

    addr++;
    page_bytes--;

    if (m->paged && (page_bytes == 0)) {
      /* Send the "Issue Page Write" if we have sent a whole page. */

      avr910_set_addr(pgm, page_addr>>1);
      avr910_send(pgm, "m", 1);
      avr910_vfy_cmd_sent(pgm, "flush page");

      page_wr_cmd_pending = 0;
      usleep(m->max_write_delay);
      avr910_set_addr(pgm, addr>>1);

      /* Set page address for next page. */

      page_addr = addr;
      page_bytes = page_size;
    }
    else if ((PDATA(pgm)->has_auto_incr_addr != 'Y') && ((addr & 0x01) == 0)) {
      avr910_set_addr(pgm, addr>>1);
    }
  }

  /* If we didn't send the page wr cmd after the last byte written in the
     loop, send it now. */

  if (page_wr_cmd_pending) {
    avr910_set_addr(pgm, page_addr>>1);
    avr910_send(pgm, "m", 1);
    avr910_vfy_cmd_sent(pgm, "flush final page");
    usleep(m->max_write_delay);
  }

  return addr;
}


static int avr910_paged_write_eeprom(PROGRAMMER * pgm, AVRPART * p,
                                     AVRMEM * m,
                                     unsigned int page_size,
                                     unsigned int addr, unsigned int n_bytes)
{
  char cmd[2];
  unsigned int max_addr = addr + n_bytes;

  avr910_set_addr(pgm, addr);

  cmd[0] = 'D';

  while (addr < max_addr) {
    cmd[1] = m->buf[addr];
    avr910_send(pgm, cmd, sizeof(cmd));
    avr910_vfy_cmd_sent(pgm, "write byte");
    usleep(m->max_write_delay);

    addr++;

    if (PDATA(pgm)->has_auto_incr_addr != 'Y') {
      avr910_set_addr(pgm, addr);
    }
  }

  return addr;
}


static int avr910_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                              unsigned int page_size,
                              unsigned int addr, unsigned int n_bytes)
{
  int rval = 0;
  if (PDATA(pgm)->use_blockmode == 0) {
    if (strcmp(m->desc, "flash") == 0) {
      rval = avr910_paged_write_flash(pgm, p, m, page_size, addr, n_bytes);
    } else if (strcmp(m->desc, "eeprom") == 0) {
      rval = avr910_paged_write_eeprom(pgm, p, m, page_size, addr, n_bytes);
    } else {
      rval = -2;
    }
  }

  if (PDATA(pgm)->use_blockmode == 1) {
    unsigned int max_addr = addr + n_bytes;
    char *cmd;
    unsigned int blocksize = PDATA(pgm)->buffersize;
    int wr_size;

    if (strcmp(m->desc, "flash") && strcmp(m->desc, "eeprom"))
      return -2;

    if (m->desc[0] == 'e') {
      blocksize = 1;		/* Write to eeprom single bytes only */
      wr_size = 1;
    } else {
      wr_size = 2;
    }

    avr910_set_addr(pgm, addr / wr_size);

    cmd = malloc(4 + blocksize);
    if (!cmd) return -1;
     
    cmd[0] = 'B';
    cmd[3] = toupper((int)(m->desc[0]));

    while (addr < max_addr) {
      if ((max_addr - addr) < blocksize) {
        blocksize = max_addr - addr;
      };
      memcpy(&cmd[4], &m->buf[addr], blocksize);
      cmd[1] = (blocksize >> 8) & 0xff;
      cmd[2] = blocksize & 0xff;

      avr910_send(pgm, cmd, 4 + blocksize);
      avr910_vfy_cmd_sent(pgm, "write block");

      addr += blocksize;
    } /* while */
    free(cmd);

    rval = addr;
  }
  return rval;
}


static int avr910_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                             unsigned int page_size,
                             unsigned int addr, unsigned int n_bytes)
{
  char cmd[4];
  int rd_size;
  unsigned int max_addr;
  char buf[2];
  int rval=0;

  max_addr = addr + n_bytes;

  if (strcmp(m->desc, "flash") == 0) {
    cmd[0] = 'R';
    rd_size = 2;                /* read two bytes per addr */
  } else if (strcmp(m->desc, "eeprom") == 0) {
    cmd[0] = 'd';
    rd_size = 1;
  } else {
    return -2;
  }

  if (PDATA(pgm)->use_blockmode) {
    /* use buffered mode */
    int blocksize = PDATA(pgm)->buffersize;

    cmd[0] = 'g';
    cmd[3] = toupper((int)(m->desc[0]));

    avr910_set_addr(pgm, addr / rd_size);

    while (addr < max_addr) {
      if ((max_addr - addr) < blocksize) {
        blocksize = max_addr - addr;
      }
      cmd[1] = (blocksize >> 8) & 0xff;
      cmd[2] = blocksize & 0xff;

      avr910_send(pgm, cmd, 4);
      avr910_recv(pgm, (char *)&m->buf[addr], blocksize);

      addr += blocksize;
    }

    rval = addr;
  } else {

    avr910_set_addr(pgm, addr / rd_size);

    while (addr < max_addr) {
      avr910_send(pgm, cmd, 1);
      if (rd_size == 2) {
        /* The 'R' command returns two bytes, MSB first, we need to put the data
           into the memory buffer LSB first. */
        avr910_recv(pgm, buf, 2);
        m->buf[addr]   = buf[1];  /* LSB */
        m->buf[addr + 1] = buf[0];  /* MSB */
      }
      else {
        avr910_recv(pgm, (char *)&m->buf[addr], 1);
      }

      addr += rd_size;

      if (PDATA(pgm)->has_auto_incr_addr != 'Y') {
        avr910_set_addr(pgm, addr / rd_size);
      }
    }

    rval = addr;
  }

  return rval;
}

/* Signature byte reads are always 3 bytes. */

static int avr910_read_sig_bytes(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m)
{
  unsigned char tmp;

  if (m->size < 3) {
    avrdude_message(MSG_INFO, "%s: memsize too small for sig byte read", progname);
    return -1;
  }

  avr910_send(pgm, "s", 1);
  avr910_recv(pgm, (char *)m->buf, 3);
  /* Returned signature has wrong order. */
  tmp = m->buf[2];
  m->buf[2] = m->buf[0];
  m->buf[0] = tmp;

  return 3;
}

const char avr910_desc[] = "Serial programmers using protocol described in application note AVR910";

void avr910_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "avr910");

  /*
   * mandatory functions
   */
  pgm->initialize     = avr910_initialize;
  pgm->display        = avr910_display;
  pgm->enable         = avr910_enable;
  pgm->disable        = avr910_disable;
  pgm->program_enable = avr910_program_enable;
  pgm->chip_erase     = avr910_chip_erase;
  pgm->cmd            = avr910_cmd;
  pgm->open           = avr910_open;
  pgm->close          = avr910_close;

  /*
   * optional functions
   */

  pgm->write_byte = avr910_write_byte;
  pgm->read_byte = avr910_read_byte;

  pgm->paged_write = avr910_paged_write;
  pgm->paged_load = avr910_paged_load;

  pgm->read_sig_bytes = avr910_read_sig_bytes;

  pgm->parseextparams = avr910_parseextparms;
  pgm->setup          = avr910_setup;
  pgm->teardown       = avr910_teardown;
}
