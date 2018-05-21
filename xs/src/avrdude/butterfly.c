/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2003-2004  Theodore A. Roth  <troth@openavr.org>
 * Copyright (C) 2005, 2007 Joerg Wunsch <j@uriah.heep.sax.de>
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
 * avrdude interface for the serial programming mode of the Atmel butterfly
 * evaluation board. This board features a bootloader which uses a protocol
 * very similar, but not identical, to the one described in application note
 * avr910.
 *
 * Actually, the butterfly uses a predecessor of the avr910 protocol
 * which is described in application notes avr109 (generic AVR
 * bootloader) and avr911 (opensource programmer).  This file now
 * fully handles the features present in avr109.  It should probably
 * be renamed to avr109, but we rather stick with the old name inside
 * the file.  We'll provide aliases for "avr109" and "avr911" in
 * avrdude.conf so users could call it by these name as well.
 */


#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "butterfly.h"

/*
 * Private data for this programmer.
 */
struct pdata
{
  char has_auto_incr_addr;
  unsigned int buffersize;
};

#define PDATA(pgm) ((struct pdata *)(pgm->cookie))

static void butterfly_setup(PROGRAMMER * pgm)
{
  if ((pgm->cookie = malloc(sizeof(struct pdata))) == 0) {
    avrdude_message(MSG_INFO, "%s: butterfly_setup(): Out of memory allocating private data\n",
                    progname);
    exit(1);
  }
  memset(pgm->cookie, 0, sizeof(struct pdata));
}

static void butterfly_teardown(PROGRAMMER * pgm)
{
  free(pgm->cookie);
}

static int butterfly_send(PROGRAMMER * pgm, char * buf, size_t len)
{
  return serial_send(&pgm->fd, (unsigned char *)buf, len);
}


static int butterfly_recv(PROGRAMMER * pgm, char * buf, size_t len)
{
  int rv;

  rv = serial_recv(&pgm->fd, (unsigned char *)buf, len);
  if (rv < 0) {
    avrdude_message(MSG_INFO, "%s: butterfly_recv(): programmer is not responding\n",
                    progname);
    return -1;
  }
  return 0;
}


static int butterfly_drain(PROGRAMMER * pgm, int display)
{
  return serial_drain(&pgm->fd, display);
}


static int butterfly_vfy_cmd_sent(PROGRAMMER * pgm, char * errmsg)
{
  char c;

  butterfly_recv(pgm, &c, 1);
  if (c != '\r') {
    avrdude_message(MSG_INFO, "%s: error: programmer did not respond to command: %s\n",
            progname, errmsg);
    return -1;
  }
  return 0;
}


static int butterfly_rdy_led(PROGRAMMER * pgm, int value)
{
  /* Do nothing. */

  return 0;
}


static int butterfly_err_led(PROGRAMMER * pgm, int value)
{
  /* Do nothing. */

  return 0;
}


static int butterfly_pgm_led(PROGRAMMER * pgm, int value)
{
  /* Do nothing. */

  return 0;
}


static int butterfly_vfy_led(PROGRAMMER * pgm, int value)
{
  /* Do nothing. */

  return 0;
}


/*
 * issue the 'chip erase' command to the butterfly board
 */
static int butterfly_chip_erase(PROGRAMMER * pgm, AVRPART * p)
{
  butterfly_send(pgm, "e", 1);
  if (butterfly_vfy_cmd_sent(pgm, "chip erase") < 0)
      return -1;

  return 0;
}


static void butterfly_enter_prog_mode(PROGRAMMER * pgm)
{
  butterfly_send(pgm, "P", 1);
  butterfly_vfy_cmd_sent(pgm, "enter prog mode");
}


static void butterfly_leave_prog_mode(PROGRAMMER * pgm)
{
  butterfly_send(pgm, "L", 1);
  butterfly_vfy_cmd_sent(pgm, "leave prog mode");
}


/*
 * issue the 'program enable' command to the AVR device
 */
static int butterfly_program_enable(PROGRAMMER * pgm, AVRPART * p)
{
  return -1;
}


/*
 * apply power to the AVR processor
 */
static void butterfly_powerup(PROGRAMMER * pgm)
{
  /* Do nothing. */

  return;
}


/*
 * remove power from the AVR processor
 */
static void butterfly_powerdown(PROGRAMMER * pgm)
{
  /* Do nothing. */

  return;
}

#define IS_BUTTERFLY_MK 0x0001

/*
 * initialize the AVR device and prepare it to accept commands
 */
static int butterfly_initialize(PROGRAMMER * pgm, AVRPART * p)
{
  char id[8];
  char sw[2];
  char hw[2];
  char buf[10];
  char type;
  char c, devtype_1st;

  /*
   * Send some ESC to activate butterfly bootloader.  This is not needed
   * for plain avr109 bootloaders but does not harm there either.
   */
  avrdude_message(MSG_INFO, "Connecting to programmer: ");
  if (pgm->flag & IS_BUTTERFLY_MK)
    {
      char mk_reset_cmd[6] = {"#aR@S\r"};
      unsigned char mk_timeout = 0;

      putc('.', stderr);
      butterfly_send(pgm, mk_reset_cmd, sizeof(mk_reset_cmd));
      usleep(20000); 

      do
	{
	  c = 27; 
	  butterfly_send(pgm, &c, 1);
	  usleep(20000);
	  c = 0xaa;
	  usleep(80000);
	  butterfly_send(pgm, &c, 1);
	  if (mk_timeout % 10 == 0) putc('.', stderr);
	} while (mk_timeout++ < 10);

      butterfly_recv(pgm, &c, 1);
      if ( c != 'M' && c != '?')
        {
          avrdude_message(MSG_INFO, "\nConnection FAILED.");
          return -1;
        }
      else
        {
	  id[0] = 'M'; id[1] = 'K'; id[2] = '2'; id[3] = 0;
	}
    }
  else
    {
      do {
	putc('.', stderr);
	butterfly_send(pgm, "\033", 1);
	butterfly_drain(pgm, 0);
	butterfly_send(pgm, "S", 1);
	butterfly_recv(pgm, &c, 1);
	if (c != '?') {
	    putc('\n', stderr);
	    /*
	     * Got a useful response, continue getting the programmer
	     * identifier. Programmer returns exactly 7 chars _without_
	     * the null.
	     */
	  id[0] = c;
	  butterfly_recv(pgm, &id[1], sizeof(id)-2);
	  id[sizeof(id)-1] = '\0';
	}
      } while (c == '?');
    }

  /* Get the HW and SW versions to see if the programmer is present. */
  butterfly_drain(pgm, 0);

  butterfly_send(pgm, "V", 1);
  butterfly_recv(pgm, sw, sizeof(sw));

  butterfly_send(pgm, "v", 1);
  butterfly_recv(pgm, hw, 1);	/* first, read only _one_ byte */
  if (hw[0]!='?') {
    butterfly_recv(pgm, &hw[1], 1);/* now, read second byte */
  };

  /* Get the programmer type (serial or parallel). Expect serial. */

  butterfly_send(pgm, "p", 1);
  butterfly_recv(pgm, &type, 1);

  avrdude_message(MSG_INFO, "Found programmer: Id = \"%s\"; type = %c\n", id, type);
  avrdude_message(MSG_INFO, "    Software Version = %c.%c; ", sw[0], sw[1]);
  if (hw[0]=='?') {
    avrdude_message(MSG_INFO, "No Hardware Version given.\n");
  } else {
    avrdude_message(MSG_INFO, "Hardware Version = %c.%c\n", hw[0], hw[1]);
  };

  /* See if programmer supports autoincrement of address. */

  butterfly_send(pgm, "a", 1);
  butterfly_recv(pgm, &PDATA(pgm)->has_auto_incr_addr, 1);
  if (PDATA(pgm)->has_auto_incr_addr == 'Y')
      avrdude_message(MSG_INFO, "Programmer supports auto addr increment.\n");

  /* Check support for buffered memory access, abort if not available */

  butterfly_send(pgm, "b", 1);
  butterfly_recv(pgm, &c, 1);
  if (c != 'Y') {
    avrdude_message(MSG_INFO, "%s: error: buffered memory access not supported. Maybe it isn't\n"\
                    "a butterfly/AVR109 but a AVR910 device?\n", progname);
    return -1;
  };
  butterfly_recv(pgm, &c, 1);
  PDATA(pgm)->buffersize = (unsigned int)(unsigned char)c<<8;
  butterfly_recv(pgm, &c, 1);
  PDATA(pgm)->buffersize += (unsigned int)(unsigned char)c;
  avrdude_message(MSG_INFO, "Programmer supports buffered memory access with buffersize=%i bytes.\n",
                  PDATA(pgm)->buffersize);

  /* Get list of devices that the programmer supports. */

  butterfly_send(pgm, "t", 1);
  avrdude_message(MSG_INFO, "\nProgrammer supports the following devices:\n");
  devtype_1st = 0;
  while (1) {
    butterfly_recv(pgm, &c, 1);
    if (devtype_1st == 0)
      devtype_1st = c;

    if (c == 0)
      break;
    avrdude_message(MSG_INFO, "    Device code: 0x%02x\n", (unsigned int)(unsigned char)c);
  };
  avrdude_message(MSG_INFO, "\n");

  /* Tell the programmer which part we selected.
     According to the AVR109 code, this is ignored by the bootloader.  As
     some early versions might not properly ignore it, rather pick up the
     first device type as reported above than anything out of avrdude.conf,
     so to avoid a potential conflict.  There appears to be no general
     agreement on AVR910 device IDs beyond the ones from the original
     appnote 910. */

  buf[0] = 'T';
  buf[1] = devtype_1st;

  butterfly_send(pgm, buf, 2);
  if (butterfly_vfy_cmd_sent(pgm, "select device") < 0)
      return -1;

  if (verbose)
    avrdude_message(MSG_INFO, "%s: devcode selected: 0x%02x\n",
                    progname, (unsigned)buf[1]);

  butterfly_enter_prog_mode(pgm);
  butterfly_drain(pgm, 0);

  return 0;
}



static void butterfly_disable(PROGRAMMER * pgm)
{
  butterfly_leave_prog_mode(pgm);

  return;
}


static void butterfly_enable(PROGRAMMER * pgm)
{
  return;
}


static int butterfly_open(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;
  strcpy(pgm->port, port);
  /*
   *  If baudrate was not specified use 19200 Baud
   */
  if(pgm->baudrate == 0) {
    pgm->baudrate = 19200;
  }
  pinfo.baud = pgm->baudrate;
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  butterfly_drain (pgm, 0);

  return 0;
}


static void butterfly_close(PROGRAMMER * pgm)
{
  /* "exit programmer" */
  butterfly_send(pgm, "E", 1);
  butterfly_vfy_cmd_sent(pgm, "exit bootloader");

  serial_close(&pgm->fd);
  pgm->fd.ifd = -1;
}


static void butterfly_display(PROGRAMMER * pgm, const char * p)
{
  return;
}


static void butterfly_set_addr(PROGRAMMER * pgm, unsigned long addr)
{
  char cmd[3];

  cmd[0] = 'A';
  cmd[1] = (addr >> 8) & 0xff;
  cmd[2] = addr & 0xff;
  
  butterfly_send(pgm, cmd, sizeof(cmd));
  butterfly_vfy_cmd_sent(pgm, "set addr");
}


static void butterfly_set_extaddr(PROGRAMMER * pgm, unsigned long addr)
{
  char cmd[4];

  cmd[0] = 'H';
  cmd[1] = (addr >> 16) & 0xff;
  cmd[2] = (addr >> 8) & 0xff;
  cmd[3] = addr & 0xff;

  butterfly_send(pgm, cmd, sizeof(cmd));
  butterfly_vfy_cmd_sent(pgm, "set extaddr");
}



static int butterfly_write_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                             unsigned long addr, unsigned char value)
{
  char cmd[6];
  int size;
  int use_ext_addr = m->op[AVR_OP_LOAD_EXT_ADDR] != NULL;

  if ((strcmp(m->desc, "flash") == 0) || (strcmp(m->desc, "eeprom") == 0))
  {
    cmd[0] = 'B';
    cmd[1] = 0;
    if ((cmd[3] = toupper((int)(m->desc[0]))) == 'E') {	/* write to eeprom */
      cmd[2] = 1;
      cmd[4] = value;
      size = 5;
    } else {						/* write to flash */
      /* @@@ not yet implemented */
      cmd[2] = 2;
      size = 6;
      return -1;
    }
    if (use_ext_addr) {
      butterfly_set_extaddr(pgm, addr);
    } else {
      butterfly_set_addr(pgm, addr);
    }
  }
  else if (strcmp(m->desc, "lock") == 0)
  {
    cmd[0] = 'l';
    cmd[1] = value;
    size = 2;
  }
  else
    return -1;

  butterfly_send(pgm, cmd, size);
  if (butterfly_vfy_cmd_sent(pgm, "write byte") < 0)
      return -1;

  return 0;
}


static int butterfly_read_byte_flash(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned long addr, unsigned char * value)
{
  static int cached = 0;
  static unsigned char cvalue;
  static unsigned long caddr;
  int use_ext_addr = m->op[AVR_OP_LOAD_EXT_ADDR] != NULL;

  if (cached && ((caddr + 1) == addr)) {
    *value = cvalue;
    cached = 0;
  }
  else {
    char buf[2];

    if (use_ext_addr) {
      butterfly_set_extaddr(pgm, addr >> 1);
    } else {
      butterfly_set_addr(pgm, addr >> 1);
    }

    butterfly_send(pgm, "g\000\002F", 4);

    /* Read back the program mem word (MSB first) */
    butterfly_recv(pgm, buf, sizeof(buf));

    if ((addr & 0x01) == 0) {
      *value = buf[0];
      cached = 1;
      cvalue = buf[1];
      caddr = addr;
    }
    else {
      *value = buf[1];
    }
  }

  return 0;
}


static int butterfly_read_byte_eeprom(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                   unsigned long addr, unsigned char * value)
{
  butterfly_set_addr(pgm, addr);
  butterfly_send(pgm, "g\000\001E", 4);
  butterfly_recv(pgm, (char *)value, 1);
  return 0;
}

static int butterfly_page_erase(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, unsigned int addr)
{
  if (strcmp(m->desc, "flash") == 0)
    return -1;            /* not supported */
  if (strcmp(m->desc, "eeprom") == 0)
    return 0;             /* nothing to do */
  avrdude_message(MSG_INFO, "%s: butterfly_page_erase() called on memory type \"%s\"\n",
                  progname, m->desc);
  return -1;
}

static int butterfly_read_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                            unsigned long addr, unsigned char * value)
{
  char cmd;

  if (strcmp(m->desc, "flash") == 0) {
    return butterfly_read_byte_flash(pgm, p, m, addr, value);
  }

  if (strcmp(m->desc, "eeprom") == 0) {
    return butterfly_read_byte_eeprom(pgm, p, m, addr, value);
  }

  if (strcmp(m->desc, "lfuse") == 0) {
    cmd = 'F';
  }
  else if (strcmp(m->desc, "hfuse") == 0) {
    cmd = 'N';
  }
  else if (strcmp(m->desc, "efuse") == 0) {
    cmd = 'Q';
  }
  else if (strcmp(m->desc, "lock") == 0) {
    cmd = 'r';
  }
  else
    return -1;

  butterfly_send(pgm, &cmd, 1);
  butterfly_recv(pgm, (char *)value, 1);

  return *value == '?'? -1: 0;
}



static int butterfly_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                 unsigned int page_size,
                                 unsigned int addr, unsigned int n_bytes)
{
  unsigned int max_addr = addr + n_bytes;
  char *cmd;
  unsigned int blocksize = PDATA(pgm)->buffersize;
  int use_ext_addr = m->op[AVR_OP_LOAD_EXT_ADDR] != NULL;
  unsigned int wr_size = 2;

  if (strcmp(m->desc, "flash") && strcmp(m->desc, "eeprom"))
    return -2;

  if (m->desc[0] == 'e')
    wr_size = blocksize = 1;		/* Write to eeprom single bytes only */

  if (use_ext_addr) {
    butterfly_set_extaddr(pgm, addr / wr_size);
  } else {
    butterfly_set_addr(pgm, addr / wr_size);
  }

#if 0
  usleep(1000000);
  butterfly_send(pgm, "y", 1);
  if (butterfly_vfy_cmd_sent(pgm, "clear LED") < 0)
    return -1;
#endif

  cmd = malloc(4+blocksize);
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

    butterfly_send(pgm, cmd, 4+blocksize);
    if (butterfly_vfy_cmd_sent(pgm, "write block") < 0)
      return -1;

    addr += blocksize;
  } /* while */
  free(cmd);

  return addr;
}



static int butterfly_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                unsigned int page_size,
                                unsigned int addr, unsigned int n_bytes)
{
  unsigned int max_addr = addr + n_bytes;
  int rd_size = 2;
  int blocksize = PDATA(pgm)->buffersize;
  int use_ext_addr = m->op[AVR_OP_LOAD_EXT_ADDR] != NULL;

  /* check parameter syntax: only "flash" or "eeprom" is allowed */
  if (strcmp(m->desc, "flash") && strcmp(m->desc, "eeprom"))
    return -2;

  if (m->desc[0] == 'e')
    rd_size = blocksize = 1;		/* Read from eeprom single bytes only */

  {		/* use buffered mode */
    char cmd[4];

    cmd[0] = 'g';
    cmd[3] = toupper((int)(m->desc[0]));

    if (use_ext_addr) {
      butterfly_set_extaddr(pgm, addr / rd_size);
    } else {
      butterfly_set_addr(pgm, addr / rd_size);
    }
    while (addr < max_addr) {
      if ((max_addr - addr) < blocksize) {
        blocksize = max_addr - addr;
      };
      cmd[1] = (blocksize >> 8) & 0xff;
      cmd[2] = blocksize & 0xff;

      butterfly_send(pgm, cmd, 4);
      butterfly_recv(pgm, (char *)&m->buf[addr], blocksize);

      addr += blocksize;
    } /* while */
  }

  return addr * rd_size;
}


/* Signature byte reads are always 3 bytes. */
static int butterfly_read_sig_bytes(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m)
{
  unsigned char tmp;

  if (m->size < 3) {
    avrdude_message(MSG_INFO, "%s: memsize too small for sig byte read", progname);
    return -1;
  }

  butterfly_send(pgm, "s", 1);
  butterfly_recv(pgm, (char *)m->buf, 3);
  /* Returned signature has wrong order. */
  tmp = m->buf[2];
  m->buf[2] = m->buf[0];
  m->buf[0] = tmp;

  return 3;
}

const char butterfly_desc[] = "Atmel Butterfly evaluation board; Atmel AppNotes AVR109, AVR911";

void butterfly_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "butterfly");

  /*
   * mandatory functions
   */
  pgm->rdy_led        = butterfly_rdy_led;
  pgm->err_led        = butterfly_err_led;
  pgm->pgm_led        = butterfly_pgm_led;
  pgm->vfy_led        = butterfly_vfy_led;
  pgm->initialize     = butterfly_initialize;
  pgm->display        = butterfly_display;
  pgm->enable         = butterfly_enable;
  pgm->disable        = butterfly_disable;
  pgm->powerup        = butterfly_powerup;
  pgm->powerdown      = butterfly_powerdown;
  pgm->program_enable = butterfly_program_enable;
  pgm->chip_erase     = butterfly_chip_erase;
  pgm->open           = butterfly_open;
  pgm->close          = butterfly_close;
  pgm->read_byte      = butterfly_read_byte;
  pgm->write_byte     = butterfly_write_byte;

  /*
   * optional functions
   */

  pgm->page_erase = butterfly_page_erase;
  pgm->paged_write = butterfly_paged_write;
  pgm->paged_load = butterfly_paged_load;

  pgm->read_sig_bytes = butterfly_read_sig_bytes;

  pgm->setup          = butterfly_setup;
  pgm->teardown       = butterfly_teardown;
  pgm->flag = 0;
}

const char butterfly_mk_desc[] = "Mikrokopter.de Butterfly";

void butterfly_mk_initpgm(PROGRAMMER * pgm)
{
  butterfly_initpgm(pgm);
  strcpy(pgm->type, "butterfly_mk");
  pgm->flag = IS_BUTTERFLY_MK;
}
