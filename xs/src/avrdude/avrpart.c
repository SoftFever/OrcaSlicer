
/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000-2004  Brian S. Dean <bsd@bsdhome.com>
 * Copyright (C) 2006 Joerg Wunsch <j@uriah.heep.sax.de>
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

#include <stdlib.h>
#include <string.h>

#include "avrdude.h"
#include "libavrdude.h"

/***
 *** Elementary functions dealing with OPCODE structures
 ***/

OPCODE * avr_new_opcode(void)
{
  OPCODE * m;

  m = (OPCODE *)malloc(sizeof(*m));
  if (m == NULL) {
    // avrdude_message(MSG_INFO, "avr_new_opcode(): out of memory\n");
    // exit(1);
    avrdude_oom("avr_new_opcode(): out of memory\n");
  }

  memset(m, 0, sizeof(*m));

  return m;
}

static OPCODE * avr_dup_opcode(OPCODE * op)
{
  OPCODE * m;
  
  /* this makes life easier */
  if (op == NULL) {
    return NULL;
  }

  m = (OPCODE *)malloc(sizeof(*m));
  if (m == NULL) {
    // avrdude_message(MSG_INFO, "avr_dup_opcode(): out of memory\n");
    // exit(1);
    avrdude_oom("avr_dup_opcode(): out of memory\n");
  }

  memcpy(m, op, sizeof(*m));

  return m;
}

void avr_free_opcode(OPCODE * op)
{
  free(op);
}

/*
 * avr_set_bits()
 *
 * Set instruction bits in the specified command based on the opcode.
 */
int avr_set_bits(OPCODE * op, unsigned char * cmd)
{
  int i, j, bit;
  unsigned char mask;

  for (i=0; i<32; i++) {
    if (op->bit[i].type == AVR_CMDBIT_VALUE) {
      j = 3 - i / 8;
      bit = i % 8;
      mask = 1 << bit;
      if (op->bit[i].value)
        cmd[j] = cmd[j] | mask;
      else
        cmd[j] = cmd[j] & ~mask;
    }
  }

  return 0;
}


/*
 * avr_set_addr()
 *
 * Set address bits in the specified command based on the opcode, and
 * the address.
 */
int avr_set_addr(OPCODE * op, unsigned char * cmd, unsigned long addr)
{
  int i, j, bit;
  unsigned long value;
  unsigned char mask;

  for (i=0; i<32; i++) {
    if (op->bit[i].type == AVR_CMDBIT_ADDRESS) {
      j = 3 - i / 8;
      bit = i % 8;
      mask = 1 << bit;
      value = addr >> op->bit[i].bitno & 0x01;
      if (value)
        cmd[j] = cmd[j] | mask;
      else
        cmd[j] = cmd[j] & ~mask;
    }
  }

  return 0;
}


/*
 * avr_set_input()
 *
 * Set input data bits in the specified command based on the opcode,
 * and the data byte.
 */
int avr_set_input(OPCODE * op, unsigned char * cmd, unsigned char data)
{
  int i, j, bit;
  unsigned char value;
  unsigned char mask;

  for (i=0; i<32; i++) {
    if (op->bit[i].type == AVR_CMDBIT_INPUT) {
      j = 3 - i / 8;
      bit = i % 8;
      mask = 1 << bit;
      value = data >> op->bit[i].bitno & 0x01;
      if (value)
        cmd[j] = cmd[j] | mask;
      else
        cmd[j] = cmd[j] & ~mask;
    }
  }

  return 0;
}


/*
 * avr_get_output()
 *
 * Retreive output data bits from the command results based on the
 * opcode data.
 */
int avr_get_output(OPCODE * op, unsigned char * res, unsigned char * data)
{
  int i, j, bit;
  unsigned char value;
  unsigned char mask;

  for (i=0; i<32; i++) {
    if (op->bit[i].type == AVR_CMDBIT_OUTPUT) {
      j = 3 - i / 8;
      bit = i % 8;
      mask = 1 << bit;
      value = ((res[j] & mask) >> bit) & 0x01;
      value = value << op->bit[i].bitno;
      if (value)
        *data = *data | value;
      else
        *data = *data & ~value;
    }
  }

  return 0;
}


/*
 * avr_get_output_index()
 *
 * Calculate the byte number of the output data based on the
 * opcode data.
 */
int avr_get_output_index(OPCODE * op)
{
  int i, j;

  for (i=0; i<32; i++) {
    if (op->bit[i].type == AVR_CMDBIT_OUTPUT) {
      j = 3 - i / 8;
      return j;
    }
  }

  return -1;
}


static char * avr_op_str(int op)
{
  switch (op) {
    case AVR_OP_READ        : return "READ"; break;
    case AVR_OP_WRITE       : return "WRITE"; break;
    case AVR_OP_READ_LO     : return "READ_LO"; break;
    case AVR_OP_READ_HI     : return "READ_HI"; break;
    case AVR_OP_WRITE_LO    : return "WRITE_LO"; break;
    case AVR_OP_WRITE_HI    : return "WRITE_HI"; break;
    case AVR_OP_LOADPAGE_LO : return "LOADPAGE_LO"; break;
    case AVR_OP_LOADPAGE_HI : return "LOADPAGE_HI"; break;
    case AVR_OP_LOAD_EXT_ADDR : return "LOAD_EXT_ADDR"; break;
    case AVR_OP_WRITEPAGE   : return "WRITEPAGE"; break;
    case AVR_OP_CHIP_ERASE  : return "CHIP_ERASE"; break;
    case AVR_OP_PGM_ENABLE  : return "PGM_ENABLE"; break;
    default : return "<unknown opcode>"; break;
  }
}


static char * bittype(int type)
{
  switch (type) {
    case AVR_CMDBIT_IGNORE  : return "IGNORE"; break;
    case AVR_CMDBIT_VALUE   : return "VALUE"; break;
    case AVR_CMDBIT_ADDRESS : return "ADDRESS"; break;
    case AVR_CMDBIT_INPUT   : return "INPUT"; break;
    case AVR_CMDBIT_OUTPUT  : return "OUTPUT"; break;
    default : return "<unknown bit type>"; break;
  }
}



/***
 *** Elementary functions dealing with AVRMEM structures
 ***/

AVRMEM * avr_new_memtype(void)
{
  AVRMEM * m;

  m = (AVRMEM *)malloc(sizeof(*m));
  if (m == NULL) {
    // avrdude_message(MSG_INFO, "avr_new_memtype(): out of memory\n");
    // exit(1);
    avrdude_oom("avr_new_memtype(): out of memory\n");
  }

  memset(m, 0, sizeof(*m));

  return m;
}


/*
 * Allocate and initialize memory buffers for each of the device's
 * defined memory regions.
 */
int avr_initmem(AVRPART * p)
{
  LNODEID ln;
  AVRMEM * m;

  for (ln=lfirst(p->mem); ln; ln=lnext(ln)) {
    m = ldata(ln);
    m->buf = (unsigned char *) malloc(m->size);
    if (m->buf == NULL) {
      avrdude_message(MSG_INFO, "%s: can't alloc buffer for %s size of %d bytes\n",
              progname, m->desc, m->size);
      return -1;
    }
    m->tags = (unsigned char *) malloc(m->size);
    if (m->tags == NULL) {
      avrdude_message(MSG_INFO, "%s: can't alloc buffer for %s size of %d bytes\n",
              progname, m->desc, m->size);
      return -1;
    }
  }

  return 0;
}


AVRMEM * avr_dup_mem(AVRMEM * m)
{
  AVRMEM * n;
  int i;

  n = avr_new_memtype();

  *n = *m;

  if (m->buf != NULL) {
    n->buf = (unsigned char *)malloc(n->size);
    if (n->buf == NULL) {
      // avrdude_message(MSG_INFO, "avr_dup_mem(): out of memory (memsize=%d)\n",
      //                 n->size);
      // exit(1);
      avrdude_oom("avr_dup_mem(): out of memory");
    }
    memcpy(n->buf, m->buf, n->size);
  }

  if (m->tags != NULL) {
    n->tags = (unsigned char *)malloc(n->size);
    if (n->tags == NULL) {
      // avrdude_message(MSG_INFO, "avr_dup_mem(): out of memory (memsize=%d)\n",
      //                 n->size);
      // exit(1);
      avrdude_oom("avr_dup_mem(): out of memory");
    }
    memcpy(n->tags, m->tags, n->size);
  }

  for (i = 0; i < AVR_OP_MAX; i++) {
    n->op[i] = avr_dup_opcode(n->op[i]);
  }

  return n;
}

void avr_free_mem(AVRMEM * m)
{
    int i;
    if (m->buf != NULL) {
      free(m->buf);
      m->buf = NULL;
    }
    if (m->tags != NULL) {
      free(m->tags);
      m->tags = NULL;
    }
    for(i=0;i<sizeof(m->op)/sizeof(m->op[0]);i++)
    {
      if (m->op[i] != NULL)
      {
        avr_free_opcode(m->op[i]);
        m->op[i] = NULL;
      }
    }
    free(m);
}

AVRMEM * avr_locate_mem(AVRPART * p, char * desc)
{
  AVRMEM * m, * match;
  LNODEID ln;
  int matches;
  int l;

  l = strlen(desc);
  matches = 0;
  match = NULL;
  for (ln=lfirst(p->mem); ln; ln=lnext(ln)) {
    m = ldata(ln);
    if (strncmp(desc, m->desc, l) == 0) {
      match = m;
      matches++;
    }
  }

  if (matches == 1)
    return match;

  return NULL;
}


void avr_mem_display(const char * prefix, FILE * f, AVRMEM * m, int type,
                     int verbose)
{
  int i, j;
  char * optr;

  if (m == NULL) {
      avrdude_message(MSG_INFO,
              "%s                       Block Poll               Page                       Polled\n"
              "%sMemory Type Mode Delay Size  Indx Paged  Size   Size #Pages MinW  MaxW   ReadBack\n"
              "%s----------- ---- ----- ----- ---- ------ ------ ---- ------ ----- ----- ---------\n",
            prefix, prefix, prefix);
  }
  else {
    if (verbose > 2) {
      avrdude_message(MSG_INFO,
              "%s                       Block Poll               Page                       Polled\n"
              "%sMemory Type Mode Delay Size  Indx Paged  Size   Size #Pages MinW  MaxW   ReadBack\n"
              "%s----------- ---- ----- ----- ---- ------ ------ ---- ------ ----- ----- ---------\n",
              prefix, prefix, prefix);
    }
    avrdude_message(MSG_INFO,
            "%s%-11s %4d %5d %5d %4d %-6s %6d %4d %6d %5d %5d 0x%02x 0x%02x\n",
            prefix, m->desc, m->mode, m->delay, m->blocksize, m->pollindex,
            m->paged ? "yes" : "no",
            m->size,
            m->page_size,
            m->num_pages,
            m->min_write_delay,
            m->max_write_delay,
            m->readback[0],
            m->readback[1]);
    if (verbose > 4) {
      avrdude_message(MSG_TRACE2, "%s  Memory Ops:\n"
                      "%s    Oeration     Inst Bit  Bit Type  Bitno  Value\n"
                      "%s    -----------  --------  --------  -----  -----\n",
                      prefix, prefix, prefix);
      for (i=0; i<AVR_OP_MAX; i++) {
        if (m->op[i]) {
          for (j=31; j>=0; j--) {
            if (j==31)
              optr = avr_op_str(i);
            else
              optr = " ";
          avrdude_message(MSG_INFO,
                  "%s    %-11s  %8d  %8s  %5d  %5d\n",
                  prefix, optr, j,
                  bittype(m->op[i]->bit[j].type),
                  m->op[i]->bit[j].bitno,
                  m->op[i]->bit[j].value);
          }
        }
      }
    }
  }
}



/*
 * Elementary functions dealing with AVRPART structures
 */


AVRPART * avr_new_part(void)
{
  AVRPART * p;

  p = (AVRPART *)malloc(sizeof(AVRPART));
  if (p == NULL) {
    // avrdude_message(MSG_INFO, "new_part(): out of memory\n");
    // exit(1);
    avrdude_oom("new_part(): out of memory\n");
  }

  memset(p, 0, sizeof(*p));

  p->id[0]   = 0;
  p->desc[0] = 0;
  p->reset_disposition = RESET_DEDICATED;
  p->retry_pulse = PIN_AVR_SCK;
  p->flags = AVRPART_SERIALOK | AVRPART_PARALLELOK | AVRPART_ENABLEPAGEPROGRAMMING;
  p->config_file[0] = 0;
  p->lineno = 0;
  memset(p->signature, 0xFF, 3);
  p->ctl_stack_type = CTL_STACK_NONE;
  p->ocdrev = -1;

  p->mem = lcreat(NULL, 0);

  return p;
}


AVRPART * avr_dup_part(AVRPART * d)
{
  AVRPART * p;
  LISTID save;
  LNODEID ln;
  int i;

  p = avr_new_part();
  save = p->mem;

  *p = *d;

  p->mem = save;

  for (ln=lfirst(d->mem); ln; ln=lnext(ln)) {
    ladd(p->mem, avr_dup_mem(ldata(ln)));
  }

  for (i = 0; i < AVR_OP_MAX; i++) {
    p->op[i] = avr_dup_opcode(p->op[i]);
  }

  return p;
}

void avr_free_part(AVRPART * d)
{
int i;
	ldestroy_cb(d->mem, (void(*)(void *))avr_free_mem);
	d->mem = NULL;
    for(i=0;i<sizeof(d->op)/sizeof(d->op[0]);i++)
    {
    	if (d->op[i] != NULL)
    	{
    		avr_free_opcode(d->op[i]);
    		d->op[i] = NULL;
    	}
    }
	free(d);
}

AVRPART * locate_part(LISTID parts, char * partdesc)
{
  LNODEID ln1;
  AVRPART * p = NULL;
  int found;

  found = 0;

  for (ln1=lfirst(parts); ln1 && !found; ln1=lnext(ln1)) {
    p = ldata(ln1);
    if ((strcasecmp(partdesc, p->id) == 0) ||
        (strcasecmp(partdesc, p->desc) == 0))
      found = 1;
  }

  if (found)
    return p;

  return NULL;
}

AVRPART * locate_part_by_avr910_devcode(LISTID parts, int devcode)
{
  LNODEID ln1;
  AVRPART * p = NULL;

  for (ln1=lfirst(parts); ln1; ln1=lnext(ln1)) {
    p = ldata(ln1);
    if (p->avr910_devcode == devcode)
      return p;
  }

  return NULL;
}

AVRPART * locate_part_by_signature(LISTID parts, unsigned char * sig,
                                   int sigsize)
{
  LNODEID ln1;
  AVRPART * p = NULL;
  int i;

  if (sigsize == 3) {
    for (ln1=lfirst(parts); ln1; ln1=lnext(ln1)) {
      p = ldata(ln1);
      for (i=0; i<3; i++)
        if (p->signature[i] != sig[i])
          break;
      if (i == 3)
        return p;
    }
  }

  return NULL;
}

/*
 * Iterate over the list of avrparts given as "avrparts", and
 * call the callback function cb for each entry found.  cb is being
 * passed the following arguments:
 * . the name of the avrpart (for -p)
 * . the descriptive text given in the config file
 * . the name of the config file this avrpart has been defined in
 * . the line number of the config file this avrpart has been defined at
 * . the "cookie" passed into walk_avrparts() (opaque client data)
 */
void walk_avrparts(LISTID avrparts, walk_avrparts_cb cb, void *cookie)
{
  LNODEID ln1;
  AVRPART * p;

  for (ln1 = lfirst(avrparts); ln1; ln1 = lnext(ln1)) {
    p = ldata(ln1);
    cb(p->id, p->desc, p->config_file, p->lineno, cookie);
  }
}

/*
 * Compare function to sort the list of programmers
 */
static int sort_avrparts_compare(AVRPART * p1,AVRPART * p2)
{
  if(p1 == NULL || p2 == NULL) {
    return 0;
  }
  return strncasecmp(p1->desc,p2->desc,AVR_DESCLEN);
}

/*
 * Sort the list of programmers given as "programmers"
 */
void sort_avrparts(LISTID avrparts)
{
  lsort(avrparts,(int (*)(void*, void*)) sort_avrparts_compare);
}


static char * reset_disp_str(int r)
{
  switch (r) {
    case RESET_DEDICATED : return "dedicated";
    case RESET_IO        : return "possible i/o";
    default              : return "<invalid>";
  }
}


void avr_display(FILE * f, AVRPART * p, const char * prefix, int verbose)
{
  int i;
  char * buf;
  const char * px;
  LNODEID ln;
  AVRMEM * m;

  avrdude_message(MSG_INFO,
          "%sAVR Part                      : %s\n"
          "%sChip Erase delay              : %d us\n"
          "%sPAGEL                         : P%02X\n"
          "%sBS2                           : P%02X\n"
          "%sRESET disposition             : %s\n"
          "%sRETRY pulse                   : %s\n"
          "%sserial program mode           : %s\n"
          "%sparallel program mode         : %s\n"
          "%sTimeout                       : %d\n"
          "%sStabDelay                     : %d\n"
          "%sCmdexeDelay                   : %d\n"
          "%sSyncLoops                     : %d\n"
          "%sByteDelay                     : %d\n"
          "%sPollIndex                     : %d\n"
          "%sPollValue                     : 0x%02x\n"
          "%sMemory Detail                 :\n\n",
          prefix, p->desc,
          prefix, p->chip_erase_delay,
          prefix, p->pagel,
          prefix, p->bs2,
          prefix, reset_disp_str(p->reset_disposition),
          prefix, avr_pin_name(p->retry_pulse),
          prefix, (p->flags & AVRPART_SERIALOK) ? "yes" : "no",
          prefix, (p->flags & AVRPART_PARALLELOK) ?
            ((p->flags & AVRPART_PSEUDOPARALLEL) ? "psuedo" : "yes") : "no",
          prefix, p->timeout,
          prefix, p->stabdelay,
          prefix, p->cmdexedelay,
          prefix, p->synchloops,
          prefix, p->bytedelay,
          prefix, p->pollindex,
          prefix, p->pollvalue,
          prefix);

  px = prefix;
  i = strlen(prefix) + 5;
  buf = (char *)malloc(i);
  if (buf == NULL) {
    /* ugh, this is not important enough to bail, just ignore it */
  }
  else {
    strcpy(buf, prefix);
    strcat(buf, "  ");
    px = buf;
  }

  if (verbose <= 2) {
    avr_mem_display(px, f, NULL, 0, verbose);
  }
  for (ln=lfirst(p->mem); ln; ln=lnext(ln)) {
    m = ldata(ln);
    avr_mem_display(px, f, m, i, verbose);
  }

  if (buf)
    free(buf);
}
