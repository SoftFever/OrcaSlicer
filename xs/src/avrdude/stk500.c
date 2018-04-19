/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2002-2004 Brian S. Dean <bsd@bsdhome.com>
 * Copyright (C) 2008,2014 Joerg Wunsch
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
 * avrdude interface for Atmel STK500 programmer
 *
 * Note: most commands use the "universal command" feature of the
 * programmer in a "pass through" mode, exceptions are "program
 * enable", "paged read", and "paged write".
 *
 */

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "stk500.h"
#include "stk500_private.h"

#define STK500_XTAL 7372800U
#define MAX_SYNC_ATTEMPTS 10

struct pdata
{
  unsigned char ext_addr_byte; /* Record ext-addr byte set in the
				* target device (if used) */
};

#define PDATA(pgm) ((struct pdata *)(pgm->cookie))


static int stk500_getparm(PROGRAMMER * pgm, unsigned parm, unsigned * value);
static int stk500_setparm(PROGRAMMER * pgm, unsigned parm, unsigned value);
static void stk500_print_parms1(PROGRAMMER * pgm, const char * p);


static int stk500_send(PROGRAMMER * pgm, unsigned char * buf, size_t len)
{
  return serial_send(&pgm->fd, buf, len);
}


static int stk500_recv(PROGRAMMER * pgm, unsigned char * buf, size_t len)
{
  int rv;

  rv = serial_recv(&pgm->fd, buf, len);
  if (rv < 0) {
    avrdude_message(MSG_INFO, "%s: stk500_recv(): programmer is not responding\n",
                    progname);
    return -1;
  }
  return 0;
}


int stk500_drain(PROGRAMMER * pgm, int display)
{
  return serial_drain(&pgm->fd, display);
}


int stk500_getsync(PROGRAMMER * pgm)
{
  unsigned char buf[32], resp[32];
  int attempt;

  /*
   * get in sync */
  buf[0] = Cmnd_STK_GET_SYNC;
  buf[1] = Sync_CRC_EOP;
  
  /*
   * First send and drain a few times to get rid of line noise 
   */
   
  stk500_send(pgm, buf, 2);
  stk500_drain(pgm, 0);
  stk500_send(pgm, buf, 2);
  stk500_drain(pgm, 0);

  for (attempt = 0; attempt < MAX_SYNC_ATTEMPTS; attempt++) {
    stk500_send(pgm, buf, 2);
    stk500_recv(pgm, resp, 1);
    if (resp[0] == Resp_STK_INSYNC){
      break;
    }
    avrdude_message(MSG_INFO, "%s: stk500_getsync() attempt %d of %d: not in sync: resp=0x%02x\n",
                    progname, attempt + 1, MAX_SYNC_ATTEMPTS, resp[0]);
  }
  if (attempt == MAX_SYNC_ATTEMPTS) {
    stk500_drain(pgm, 0);
    return -1;
  }

  if (stk500_recv(pgm, resp, 1) < 0)
    return -1;
  if (resp[0] != Resp_STK_OK) {
    avrdude_message(MSG_INFO, "%s: stk500_getsync(): can't communicate with device: "
                    "resp=0x%02x\n",
                    progname, resp[0]);
    return -1;
  }

  return 0;
}


/*
 * transmit an AVR device command and return the results; 'cmd' and
 * 'res' must point to at least a 4 byte data buffer
 */
static int stk500_cmd(PROGRAMMER * pgm, const unsigned char *cmd,
                      unsigned char *res)
{
  unsigned char buf[32];

  buf[0] = Cmnd_STK_UNIVERSAL;
  buf[1] = cmd[0];
  buf[2] = cmd[1];
  buf[3] = cmd[2];
  buf[4] = cmd[3];
  buf[5] = Sync_CRC_EOP;

  stk500_send(pgm, buf, 6);

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] != Resp_STK_INSYNC) {
    avrdude_message(MSG_INFO, "%s: stk500_cmd(): programmer is out of sync\n", progname);
    return -1;
  }

  res[0] = cmd[1];
  res[1] = cmd[2];
  res[2] = cmd[3];
  if (stk500_recv(pgm, &res[3], 1) < 0)
    return -1;

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] != Resp_STK_OK) {
    avrdude_message(MSG_INFO, "%s: stk500_cmd(): protocol error\n", progname);
    return -1;
  }

  return 0;
}



/*
 * issue the 'chip erase' command to the AVR device
 */
static int stk500_chip_erase(PROGRAMMER * pgm, AVRPART * p)
{
  unsigned char cmd[4];
  unsigned char res[4];

  if (pgm->cmd == NULL) {
    avrdude_message(MSG_INFO, "%s: Error: %s programmer uses stk500_chip_erase() but does not\n"
                    "provide a cmd() method.\n",
                    progname, pgm->type);
    return -1;
  }

  if (p->op[AVR_OP_CHIP_ERASE] == NULL) {
    avrdude_message(MSG_INFO, "chip erase instruction not defined for part \"%s\"\n",
            p->desc);
    return -1;
  }

  pgm->pgm_led(pgm, ON);

  memset(cmd, 0, sizeof(cmd));

  avr_set_bits(p->op[AVR_OP_CHIP_ERASE], cmd);
  pgm->cmd(pgm, cmd, res);
  usleep(p->chip_erase_delay);
  pgm->initialize(pgm, p);

  pgm->pgm_led(pgm, OFF);

  return 0;
}

/*
 * issue the 'program enable' command to the AVR device
 */
static int stk500_program_enable(PROGRAMMER * pgm, AVRPART * p)
{
  unsigned char buf[16];
  int tries=0;

 retry:
  
  tries++;

  buf[0] = Cmnd_STK_ENTER_PROGMODE;
  buf[1] = Sync_CRC_EOP;

  stk500_send(pgm, buf, 2);
  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      avrdude_message(MSG_INFO, "%s: stk500_program_enable(): can't get into sync\n",
              progname);
      return -1;
    }
    if (stk500_getsync(pgm) < 0)
      return -1;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    avrdude_message(MSG_INFO, "%s: stk500_program_enable(): protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return -1;
  }

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_OK) {
    return 0;
  }
  else if (buf[0] == Resp_STK_NODEVICE) {
    avrdude_message(MSG_INFO, "%s: stk500_program_enable(): no device\n",
            progname);
    return -1;
  }

  if(buf[0] == Resp_STK_FAILED)
  {
      avrdude_message(MSG_INFO, "%s: stk500_program_enable(): failed to enter programming mode\n",
                      progname);
	  return -1;
  }


  avrdude_message(MSG_INFO, "%s: stk500_program_enable(): unknown response=0x%02x\n",
          progname, buf[0]);

  return -1;
}



static int stk500_set_extended_parms(PROGRAMMER * pgm, int n,
                                     unsigned char * cmd)
{
  unsigned char buf[16];
  int tries=0;
  int i;

 retry:
  
  tries++;

  buf[0] = Cmnd_STK_SET_DEVICE_EXT;
  for (i=0; i<n; i++) {
    buf[1+i] = cmd[i];
  }
  i++;
  buf[i] = Sync_CRC_EOP;

  stk500_send(pgm, buf, i+1);
  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      avrdude_message(MSG_INFO, "%s: stk500_set_extended_parms(): can't get into sync\n",
              progname);
      return -1;
    }
    if (stk500_getsync(pgm) < 0)
      return -1;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    avrdude_message(MSG_INFO, "%s: stk500_set_extended_parms(): protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return -1;
  }

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_OK) {
    return 0;
  }
  else if (buf[0] == Resp_STK_NODEVICE) {
    avrdude_message(MSG_INFO, "%s: stk500_set_extended_parms(): no device\n",
            progname);
    return -1;
  }

  if(buf[0] == Resp_STK_FAILED)
  {
      avrdude_message(MSG_INFO, "%s: stk500_set_extended_parms(): failed to set extended "
                      "device programming parameters\n",
                      progname);
	  return -1;
  }


  avrdude_message(MSG_INFO, "%s: stk500_set_extended_parms(): unknown response=0x%02x\n",
          progname, buf[0]);

  return -1;
}

/*
 * Crossbow MIB510 initialization and shutdown.  Use cmd = 1 to
 * initialize, cmd = 0 to close.
 */
static int mib510_isp(PROGRAMMER * pgm, unsigned char cmd)
{
  unsigned char buf[9];
  int tries = 0;

  buf[0] = 0xaa;
  buf[1] = 0x55;
  buf[2] = 0x55;
  buf[3] = 0xaa;
  buf[4] = 0x17;
  buf[5] = 0x51;
  buf[6] = 0x31;
  buf[7] = 0x13;
  buf[8] = cmd;


 retry:

  tries++;

  stk500_send(pgm, buf, 9);
  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      avrdude_message(MSG_INFO, "%s: mib510_isp(): can't get into sync\n",
              progname);
      return -1;
    }
    if (stk500_getsync(pgm) < 0)
      return -1;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    avrdude_message(MSG_INFO, "%s: mib510_isp(): protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return -1;
  }

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_OK) {
    return 0;
  }
  else if (buf[0] == Resp_STK_NODEVICE) {
    avrdude_message(MSG_INFO, "%s: mib510_isp(): no device\n",
            progname);
    return -1;
  }

  if (buf[0] == Resp_STK_FAILED)
  {
      avrdude_message(MSG_INFO, "%s: mib510_isp(): command %d failed\n",
                      progname, cmd);
      return -1;
  }


  avrdude_message(MSG_INFO, "%s: mib510_isp(): unknown response=0x%02x\n",
          progname, buf[0]);

  return -1;
}


/*
 * initialize the AVR device and prepare it to accept commands
 */
static int stk500_initialize(PROGRAMMER * pgm, AVRPART * p)
{
  unsigned char buf[32];
  AVRMEM * m;
  int tries;
  unsigned maj, min;
  int rc;
  int n_extparms;

  stk500_getparm(pgm, Parm_STK_SW_MAJOR, &maj);
  stk500_getparm(pgm, Parm_STK_SW_MINOR, &min);

  // MIB510 does not need extparams
  if (strcmp(ldata(lfirst(pgm->id)), "mib510") == 0)
    n_extparms = 0;
  else if ((maj > 1) || ((maj == 1) && (min > 10)))
    n_extparms = 4;
  else
    n_extparms = 3;

  tries = 0;

 retry:
  tries++;

  memset(buf, 0, sizeof(buf));

  /*
   * set device programming parameters
   */
  buf[0] = Cmnd_STK_SET_DEVICE;

  buf[1] = p->stk500_devcode;
  buf[2] = 0; /* device revision */

  if ((p->flags & AVRPART_SERIALOK) && (p->flags & AVRPART_PARALLELOK))
    buf[3] = 0; /* device supports parallel and serial programming */
  else
    buf[3] = 1; /* device supports parallel only */

  if (p->flags & AVRPART_PARALLELOK) {
    if (p->flags & AVRPART_PSEUDOPARALLEL) {
      buf[4] = 0; /* pseudo parallel interface */
      n_extparms = 0;
    }
    else {
      buf[4] = 1; /* full parallel interface */
    }
  }

#if 0
  avrdude_message(MSG_INFO, "%s: stk500_initialize(): n_extparms = %d\n",
          progname, n_extparms);
#endif
    
  buf[5] = 1; /* polling supported - XXX need this in config file */
  buf[6] = 1; /* programming is self-timed - XXX need in config file */

  m = avr_locate_mem(p, "lock");
  if (m)
    buf[7] = m->size;
  else
    buf[7] = 0;

  /*
   * number of fuse bytes
   */
  buf[8] = 0;
  m = avr_locate_mem(p, "fuse");
  if (m)
    buf[8] += m->size;
  m = avr_locate_mem(p, "lfuse");
  if (m)
    buf[8] += m->size;
  m = avr_locate_mem(p, "hfuse");
  if (m)
    buf[8] += m->size;
  m = avr_locate_mem(p, "efuse");
  if (m)
    buf[8] += m->size;

  m = avr_locate_mem(p, "flash");
  if (m) {
    buf[9] = m->readback[0];
    buf[10] = m->readback[1];
    if (m->paged) {
      buf[13] = (m->page_size >> 8) & 0x00ff;
      buf[14] = m->page_size & 0x00ff;
    }
    buf[17] = (m->size >> 24) & 0xff;
    buf[18] = (m->size >> 16) & 0xff;
    buf[19] = (m->size >> 8) & 0xff;
    buf[20] = m->size & 0xff;
  }
  else {
    buf[9]  = 0xff;
    buf[10]  = 0xff;
    buf[13] = 0;
    buf[14] = 0;
    buf[17] = 0;
    buf[18] = 0;
    buf[19] = 0;
    buf[20] = 0;
  }

  m = avr_locate_mem(p, "eeprom");
  if (m) {
    buf[11] = m->readback[0];
    buf[12] = m->readback[1];
    buf[15] = (m->size >> 8) & 0x00ff;
    buf[16] = m->size & 0x00ff;
  }
  else {
    buf[11] = 0xff;
    buf[12] = 0xff;
    buf[15] = 0;
    buf[16] = 0;
  }

  buf[21] = Sync_CRC_EOP;

  stk500_send(pgm, buf, 22);
  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_NOSYNC) {
    avrdude_message(MSG_INFO, "%s: stk500_initialize(): programmer not in sync, resp=0x%02x\n",
                    progname, buf[0]);
    if (tries > 33)
      return -1;
    if (stk500_getsync(pgm) < 0)
      return -1;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    avrdude_message(MSG_INFO, "%s: stk500_initialize(): (a) protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return -1;
  }

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] != Resp_STK_OK) {
    avrdude_message(MSG_INFO, "%s: stk500_initialize(): (b) protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_OK, buf[0]);
    return -1;
  }

  if (n_extparms) {
    if ((p->pagel == 0) || (p->bs2 == 0)) {
      avrdude_message(MSG_NOTICE2, "%s: PAGEL and BS2 signals not defined in the configuration "
                          "file for part %s, using dummy values\n",
                          progname, p->desc);
      buf[2] = 0xD7;            /* they look somehow possible, */
      buf[3] = 0xA0;            /* don't they? ;) */
    }
    else {
      buf[2] = p->pagel;
      buf[3] = p->bs2;
    }
    buf[0] = n_extparms+1;

    /*
     * m is currently pointing to eeprom memory if the part has it
     */
    if (m)
      buf[1] = m->page_size;
    else
      buf[1] = 0;


    if (n_extparms == 4) {
      if (p->reset_disposition == RESET_DEDICATED)
        buf[4] = 0;
      else
        buf[4] = 1;
    }

    rc = stk500_set_extended_parms(pgm, n_extparms+1, buf);
    if (rc) {
      avrdude_message(MSG_INFO, "%s: stk500_initialize(): failed\n", progname);
      return -1;
    }
  }

  return pgm->program_enable(pgm, p);
}


static void stk500_disable(PROGRAMMER * pgm)
{
  unsigned char buf[16];
  int tries=0;

 retry:
  
  tries++;

  buf[0] = Cmnd_STK_LEAVE_PROGMODE;
  buf[1] = Sync_CRC_EOP;

  stk500_send(pgm, buf, 2);
  if (stk500_recv(pgm, buf, 1) < 0)
    return;
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      avrdude_message(MSG_INFO, "%s: stk500_disable(): can't get into sync\n",
              progname);
      return;
    }
    if (stk500_getsync(pgm) < 0)
      return;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    avrdude_message(MSG_INFO, "%s: stk500_disable(): protocol error, expect=0x%02x, "
                    "resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return;
  }

  if (stk500_recv(pgm, buf, 1) < 0)
    return;
  if (buf[0] == Resp_STK_OK) {
    return;
  }
  else if (buf[0] == Resp_STK_NODEVICE) {
    avrdude_message(MSG_INFO, "%s: stk500_disable(): no device\n",
            progname);
    return;
  }

  avrdude_message(MSG_INFO, "%s: stk500_disable(): unknown response=0x%02x\n",
          progname, buf[0]);

  return;
}

static void stk500_enable(PROGRAMMER * pgm)
{
  return;
}


static int stk500_open(PROGRAMMER * pgm, char * port)
{
  union pinfo pinfo;
  strcpy(pgm->port, port);
  pinfo.baud = pgm->baudrate? pgm->baudrate: 115200;
  if (serial_open(port, pinfo, &pgm->fd)==-1) {
    return -1;
  }

  /*
   * drain any extraneous input
   */
  stk500_drain(pgm, 0);

  // MIB510 init
  if (strcmp(ldata(lfirst(pgm->id)), "mib510") == 0 &&
      mib510_isp(pgm, 1) != 0)
    return -1;

  if (stk500_getsync(pgm) < 0)
    return -1;

  return 0;
}


static void stk500_close(PROGRAMMER * pgm)
{
  // MIB510 close
  if (strcmp(ldata(lfirst(pgm->id)), "mib510") == 0)
    (void)mib510_isp(pgm, 0);

  serial_close(&pgm->fd);
  pgm->fd.ifd = -1;
}


static int stk500_loadaddr(PROGRAMMER * pgm, AVRMEM * mem, unsigned int addr)
{
  unsigned char buf[16];
  int tries;
  unsigned char ext_byte;
  OPCODE * lext;

  tries = 0;
 retry:
  tries++;

  /* To support flash > 64K words the correct Extended Address Byte is needed */
  lext = mem->op[AVR_OP_LOAD_EXT_ADDR];
  if (lext != NULL) {
    ext_byte = (addr >> 16) & 0xff;
    if (ext_byte != PDATA(pgm)->ext_addr_byte) {
      /* Either this is the first addr load, or a 64K word boundary is
       * crossed, so set the ext addr byte */
      avr_set_bits(lext, buf);
      avr_set_addr(lext, buf, addr);
      stk500_cmd(pgm, buf, buf);
      PDATA(pgm)->ext_addr_byte = ext_byte;
    }
  }

  buf[0] = Cmnd_STK_LOAD_ADDRESS;
  buf[1] = addr & 0xff;
  buf[2] = (addr >> 8) & 0xff;
  buf[3] = Sync_CRC_EOP;

  stk500_send(pgm, buf, 4);

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      avrdude_message(MSG_INFO, "%s: stk500_loadaddr(): can't get into sync\n",
              progname);
      return -1;
    }
    if (stk500_getsync(pgm) < 0)
      return -1;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    avrdude_message(MSG_INFO, "%s: stk500_loadaddr(): (a) protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return -1;
  }

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_OK) {
    return 0;
  }

  avrdude_message(MSG_INFO, "%s: loadaddr(): (b) protocol error, "
                  "expect=0x%02x, resp=0x%02x\n",
                  progname, Resp_STK_INSYNC, buf[0]);

  return -1;
}


static int stk500_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                              unsigned int page_size,
                              unsigned int addr, unsigned int n_bytes)
{
  unsigned char buf[page_size + 16];
  int memtype;
  int a_div;
  int block_size;
  int tries;
  unsigned int n;
  unsigned int i;

  if (strcmp(m->desc, "flash") == 0) {
    memtype = 'F';
  }
  else if (strcmp(m->desc, "eeprom") == 0) {
    memtype = 'E';
  }
  else {
    return -2;
  }

  if ((m->op[AVR_OP_LOADPAGE_LO]) || (m->op[AVR_OP_READ_LO]))
    a_div = 2;
  else
    a_div = 1;

  n = addr + n_bytes;
#if 0
  avrdude_message(MSG_INFO, "n_bytes   = %d\n"
                  "n         = %u\n"
                  "a_div     = %d\n"
                  "page_size = %d\n",
                  n_bytes, n, a_div, page_size);
#endif     

  for (; addr < n; addr += block_size) {
    // MIB510 uses fixed blocks size of 256 bytes
    if (strcmp(ldata(lfirst(pgm->id)), "mib510") == 0) {
      block_size = 256;
    } else {
      if (n - addr < page_size)
        block_size = n - addr;
      else
        block_size = page_size;
    }
    tries = 0;
  retry:
    tries++;
    stk500_loadaddr(pgm, m, addr/a_div);

    /* build command block and avoid multiple send commands as it leads to a crash
        of the silabs usb serial driver on mac os x */
    i = 0;
    buf[i++] = Cmnd_STK_PROG_PAGE;
    buf[i++] = (block_size >> 8) & 0xff;
    buf[i++] = block_size & 0xff;
    buf[i++] = memtype;
    memcpy(&buf[i], &m->buf[addr], block_size);
    i += block_size;
    buf[i++] = Sync_CRC_EOP;
    stk500_send( pgm, buf, i);

    if (stk500_recv(pgm, buf, 1) < 0)
      return -1;
    if (buf[0] == Resp_STK_NOSYNC) {
      if (tries > 33) {
        avrdude_message(MSG_INFO, "\n%s: stk500_paged_write(): can't get into sync\n",
                progname);
        return -3;
      }
      if (stk500_getsync(pgm) < 0)
	return -1;
      goto retry;
    }
    else if (buf[0] != Resp_STK_INSYNC) {
      avrdude_message(MSG_INFO, "\n%s: stk500_paged_write(): (a) protocol error, "
                      "expect=0x%02x, resp=0x%02x\n",
                      progname, Resp_STK_INSYNC, buf[0]);
      return -4;
    }
    
    if (stk500_recv(pgm, buf, 1) < 0)
      return -1;
    if (buf[0] != Resp_STK_OK) {
      avrdude_message(MSG_INFO, "\n%s: stk500_paged_write(): (a) protocol error, "
                      "expect=0x%02x, resp=0x%02x\n",
                      progname, Resp_STK_INSYNC, buf[0]);
      return -5;
    }
  }

  return n_bytes;
}

static int stk500_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                             unsigned int page_size,
                             unsigned int addr, unsigned int n_bytes)
{
  unsigned char buf[16];
  int memtype;
  int a_div;
  int tries;
  unsigned int n;
  int block_size;

  if (strcmp(m->desc, "flash") == 0) {
    memtype = 'F';
  }
  else if (strcmp(m->desc, "eeprom") == 0) {
    memtype = 'E';
  }
  else {
    return -2;
  }

  if ((m->op[AVR_OP_LOADPAGE_LO]) || (m->op[AVR_OP_READ_LO]))
    a_div = 2;
  else
    a_div = 1;

  n = addr + n_bytes;
  for (; addr < n; addr += block_size) {
    // MIB510 uses fixed blocks size of 256 bytes
    if (strcmp(ldata(lfirst(pgm->id)), "mib510") == 0) {
      block_size = 256;
    } else {
      if (n - addr < page_size)
        block_size = n - addr;
      else
        block_size = page_size;
    }

    tries = 0;
  retry:
    tries++;
    stk500_loadaddr(pgm, m, addr/a_div);
    buf[0] = Cmnd_STK_READ_PAGE;
    buf[1] = (block_size >> 8) & 0xff;
    buf[2] = block_size & 0xff;
    buf[3] = memtype;
    buf[4] = Sync_CRC_EOP;
    stk500_send(pgm, buf, 5);

    if (stk500_recv(pgm, buf, 1) < 0)
      return -1;
    if (buf[0] == Resp_STK_NOSYNC) {
      if (tries > 33) {
        avrdude_message(MSG_INFO, "\n%s: stk500_paged_load(): can't get into sync\n",
                progname);
        return -3;
      }
      if (stk500_getsync(pgm) < 0)
	return -1;
      goto retry;
    }
    else if (buf[0] != Resp_STK_INSYNC) {
      avrdude_message(MSG_INFO, "\n%s: stk500_paged_load(): (a) protocol error, "
                      "expect=0x%02x, resp=0x%02x\n",
                      progname, Resp_STK_INSYNC, buf[0]);
      return -4;
    }

    if (stk500_recv(pgm, &m->buf[addr], block_size) < 0)
      return -1;

    if (stk500_recv(pgm, buf, 1) < 0)
      return -1;

    if(strcmp(ldata(lfirst(pgm->id)), "mib510") == 0) {
      if (buf[0] != Resp_STK_INSYNC) {
      avrdude_message(MSG_INFO, "\n%s: stk500_paged_load(): (a) protocol error, "
                      "expect=0x%02x, resp=0x%02x\n",
                      progname, Resp_STK_INSYNC, buf[0]);
      return -5;
    }
  }
    else {
      if (buf[0] != Resp_STK_OK) {
        avrdude_message(MSG_INFO, "\n%s: stk500_paged_load(): (a) protocol error, "
                        "expect=0x%02x, resp=0x%02x\n",
                        progname, Resp_STK_OK, buf[0]);
        return -5;
      }
    }
  }

  return n_bytes;
}


static int stk500_set_vtarget(PROGRAMMER * pgm, double v)
{
  unsigned uaref, utarg;

  utarg = (unsigned)((v + 0.049) * 10);

  if (stk500_getparm(pgm, Parm_STK_VADJUST, &uaref) != 0) {
    avrdude_message(MSG_INFO, "%s: stk500_set_vtarget(): cannot obtain V[aref]\n",
                    progname);
    return -1;
  }

  if (uaref > utarg) {
    avrdude_message(MSG_INFO, "%s: stk500_set_vtarget(): reducing V[aref] from %.1f to %.1f\n",
                    progname, uaref / 10.0, v);
    if (stk500_setparm(pgm, Parm_STK_VADJUST, utarg)
	!= 0)
      return -1;
  }
  return stk500_setparm(pgm, Parm_STK_VTARGET, utarg);
}


static int stk500_set_varef(PROGRAMMER * pgm, unsigned int chan /* unused */,
                            double v)
{
  unsigned uaref, utarg;

  uaref = (unsigned)((v + 0.049) * 10);

  if (stk500_getparm(pgm, Parm_STK_VTARGET, &utarg) != 0) {
    avrdude_message(MSG_INFO, "%s: stk500_set_varef(): cannot obtain V[target]\n",
                    progname);
    return -1;
  }

  if (uaref > utarg) {
    avrdude_message(MSG_INFO, "%s: stk500_set_varef(): V[aref] must not be greater than "
                    "V[target] = %.1f\n",
                    progname, utarg / 10.0);
    return -1;
  }
  return stk500_setparm(pgm, Parm_STK_VADJUST, uaref);
}


static int stk500_set_fosc(PROGRAMMER * pgm, double v)
{
  unsigned prescale, cmatch, fosc;
  static unsigned ps[] = {
    1, 8, 32, 64, 128, 256, 1024
  };
  int idx, rc;

  prescale = cmatch = 0;
  if (v > 0.0) {
    if (v > STK500_XTAL / 2) {
      const char *unit;
      if (v > 1e6) {
        v /= 1e6;
        unit = "MHz";
      } else if (v > 1e3) {
        v /= 1e3;
        unit = "kHz";
      } else
        unit = "Hz";
      avrdude_message(MSG_INFO, "%s: stk500_set_fosc(): f = %.3f %s too high, using %.3f MHz\n",
                      progname, v, unit, STK500_XTAL / 2e6);
      fosc = STK500_XTAL / 2;
    } else
      fosc = (unsigned)v;
    
    for (idx = 0; idx < sizeof(ps) / sizeof(ps[0]); idx++) {
      if (fosc >= STK500_XTAL / (256 * ps[idx] * 2)) {
        /* this prescaler value can handle our frequency */
        prescale = idx + 1;
        cmatch = (unsigned)(STK500_XTAL / (2 * fosc * ps[idx])) - 1;
        break;
      }
    }
    if (idx == sizeof(ps) / sizeof(ps[0])) {
      avrdude_message(MSG_INFO, "%s: stk500_set_fosc(): f = %u Hz too low, %u Hz min\n",
          progname, fosc, STK500_XTAL / (256 * 1024 * 2));
      return -1;
    }
  }
  
  if ((rc = stk500_setparm(pgm, Parm_STK_OSC_PSCALE, prescale)) != 0
      || (rc = stk500_setparm(pgm, Parm_STK_OSC_CMATCH, cmatch)) != 0)
    return rc;
  
  return 0;
}


/* This code assumes that each count of the SCK duration parameter
   represents 8/f, where f is the clock frequency of the STK500 master
   processors (not the target).  This number comes from Atmel
   application note AVR061.  It appears that the STK500 bit bangs SCK.
   For small duration values, the actual SCK width is larger than
   expected.  As the duration value increases, the SCK width error
   diminishes. */
static int stk500_set_sck_period(PROGRAMMER * pgm, double v)
{
  int dur;
  double min, max;

  min = 8.0 / STK500_XTAL;
  max = 255 * min;
  dur = v / min + 0.5;
  
  if (v < min) {
      dur = 1;
      avrdude_message(MSG_INFO, "%s: stk500_set_sck_period(): p = %.1f us too small, using %.1f us\n",
                      progname, v / 1e-6, dur * min / 1e-6);
  } else if (v > max) {
      dur = 255;
      avrdude_message(MSG_INFO, "%s: stk500_set_sck_period(): p = %.1f us too large, using %.1f us\n",
                      progname, v / 1e-6, dur * min / 1e-6);
  }
  
  return stk500_setparm(pgm, Parm_STK_SCK_DURATION, dur);
}


static int stk500_getparm(PROGRAMMER * pgm, unsigned parm, unsigned * value)
{
  unsigned char buf[16];
  unsigned v;
  int tries = 0;

 retry:
  tries++;
  buf[0] = Cmnd_STK_GET_PARAMETER;
  buf[1] = parm;
  buf[2] = Sync_CRC_EOP;

  stk500_send(pgm, buf, 3);

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      avrdude_message(MSG_INFO, "\n%s: stk500_getparm(): can't get into sync\n",
              progname);
      return -1;
    }
    if (stk500_getsync(pgm) < 0)
      return -1;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    avrdude_message(MSG_INFO, "\n%s: stk500_getparm(): (a) protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return -2;
  }

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  v = buf[0];

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_FAILED) {
    avrdude_message(MSG_INFO, "\n%s: stk500_getparm(): parameter 0x%02x failed\n",
                    progname, v);
    return -3;
  }
  else if (buf[0] != Resp_STK_OK) {
    avrdude_message(MSG_INFO, "\n%s: stk500_getparm(): (a) protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return -3;
  }

  *value = v;

  return 0;
}

  
static int stk500_setparm(PROGRAMMER * pgm, unsigned parm, unsigned value)
{
  unsigned char buf[16];
  int tries = 0;

 retry:
  tries++;
  buf[0] = Cmnd_STK_SET_PARAMETER;
  buf[1] = parm;
  buf[2] = value;
  buf[3] = Sync_CRC_EOP;

  stk500_send(pgm, buf, 4);

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_NOSYNC) {
    if (tries > 33) {
      avrdude_message(MSG_INFO, "\n%s: stk500_setparm(): can't get into sync\n",
              progname);
      return -1;
    }
    if (stk500_getsync(pgm) < 0)
      return -1;
    goto retry;
  }
  else if (buf[0] != Resp_STK_INSYNC) {
    avrdude_message(MSG_INFO, "\n%s: stk500_setparm(): (a) protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return -2;
  }

  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_OK)
    return 0;

  parm = buf[0];	/* if not STK_OK, we've been echoed parm here */
  if (stk500_recv(pgm, buf, 1) < 0)
    return -1;
  if (buf[0] == Resp_STK_FAILED) {
    avrdude_message(MSG_INFO, "\n%s: stk500_setparm(): parameter 0x%02x failed\n",
                    progname, parm);
    return -3;
  }
  else {
    avrdude_message(MSG_INFO, "\n%s: stk500_setparm(): (a) protocol error, "
                    "expect=0x%02x, resp=0x%02x\n",
                    progname, Resp_STK_INSYNC, buf[0]);
    return -3;
  }
}

  
static void stk500_display(PROGRAMMER * pgm, const char * p)
{
  unsigned maj, min, hdw, topcard;

  stk500_getparm(pgm, Parm_STK_HW_VER, &hdw);
  stk500_getparm(pgm, Parm_STK_SW_MAJOR, &maj);
  stk500_getparm(pgm, Parm_STK_SW_MINOR, &min);
  stk500_getparm(pgm, Param_STK500_TOPCARD_DETECT, &topcard);

  avrdude_message(MSG_INFO, "%sHardware Version: %d\n", p, hdw);
  avrdude_message(MSG_INFO, "%sFirmware Version: %d.%d\n", p, maj, min);
  if (topcard < 3) {
    const char *n = "Unknown";

    switch (topcard) {
      case 1:
	n = "STK502";
	break;

      case 2:
	n = "STK501";
	break;
    }
    avrdude_message(MSG_INFO, "%sTopcard         : %s\n", p, n);
  }
  stk500_print_parms1(pgm, p);

  return;
}


static void stk500_print_parms1(PROGRAMMER * pgm, const char * p)
{
  unsigned vtarget, vadjust, osc_pscale, osc_cmatch, sck_duration;

  stk500_getparm(pgm, Parm_STK_VTARGET, &vtarget);
  stk500_getparm(pgm, Parm_STK_VADJUST, &vadjust);
  stk500_getparm(pgm, Parm_STK_OSC_PSCALE, &osc_pscale);
  stk500_getparm(pgm, Parm_STK_OSC_CMATCH, &osc_cmatch);
  stk500_getparm(pgm, Parm_STK_SCK_DURATION, &sck_duration);

  avrdude_message(MSG_INFO, "%sVtarget         : %.1f V\n", p, vtarget / 10.0);
  avrdude_message(MSG_INFO, "%sVaref           : %.1f V\n", p, vadjust / 10.0);
  avrdude_message(MSG_INFO, "%sOscillator      : ", p);
  if (osc_pscale == 0)
    avrdude_message(MSG_INFO, "Off\n");
  else {
    int prescale = 1;
    double f = STK500_XTAL / 2;
    const char *unit;

    switch (osc_pscale) {
      case 2: prescale = 8; break;
      case 3: prescale = 32; break;
      case 4: prescale = 64; break;
      case 5: prescale = 128; break;
      case 6: prescale = 256; break;
      case 7: prescale = 1024; break;
    }
    f /= prescale;
    f /= (osc_cmatch + 1);
    if (f > 1e6) {
      f /= 1e6;
      unit = "MHz";
    } else if (f > 1e3) {
      f /= 1000;
      unit = "kHz";
    } else
      unit = "Hz";
    avrdude_message(MSG_INFO, "%.3f %s\n", f, unit);
  }
  avrdude_message(MSG_INFO, "%sSCK period      : %.1f us\n", p,
	  sck_duration * 8.0e6 / STK500_XTAL + 0.05);

  return;
}


static void stk500_print_parms(PROGRAMMER * pgm)
{
  stk500_print_parms1(pgm, "");
}

static void stk500_setup(PROGRAMMER * pgm)
{
  if ((pgm->cookie = malloc(sizeof(struct pdata))) == 0) {
    avrdude_message(MSG_INFO, "%s: stk500_setup(): Out of memory allocating private data\n",
                    progname);
    return;
  }
  memset(pgm->cookie, 0, sizeof(struct pdata));
  PDATA(pgm)->ext_addr_byte = 0xff; /* Ensures it is programmed before
				     * first memory address */
}

static void stk500_teardown(PROGRAMMER * pgm)
{
  free(pgm->cookie);
}

const char stk500_desc[] = "Atmel STK500 Version 1.x firmware";

void stk500_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "STK500");

  /*
   * mandatory functions
   */
  pgm->initialize     = stk500_initialize;
  pgm->display        = stk500_display;
  pgm->enable         = stk500_enable;
  pgm->disable        = stk500_disable;
  pgm->program_enable = stk500_program_enable;
  pgm->chip_erase     = stk500_chip_erase;
  pgm->cmd            = stk500_cmd;
  pgm->open           = stk500_open;
  pgm->close          = stk500_close;
  pgm->read_byte      = avr_read_byte_default;
  pgm->write_byte     = avr_write_byte_default;

  /*
   * optional functions
   */
  pgm->paged_write    = stk500_paged_write;
  pgm->paged_load     = stk500_paged_load;
  pgm->print_parms    = stk500_print_parms;
  pgm->set_vtarget    = stk500_set_vtarget;
  pgm->set_varef      = stk500_set_varef;
  pgm->set_fosc       = stk500_set_fosc;
  pgm->set_sck_period = stk500_set_sck_period;
  pgm->setup          = stk500_setup;
  pgm->teardown       = stk500_teardown;
  pgm->page_size      = 256;
}
