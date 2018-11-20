/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000-2005  Brian S. Dean <bsd@bsdhome.com>
 * Copyright (C) 2007 Joerg Wunsch
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "avrdude.h"
#include "libavrdude.h"

UPDATE * parse_op(char * s)
{
  char buf[1024];
  char * p, * cp, c;
  UPDATE * upd;
  int i;
  size_t fnlen;

  upd = (UPDATE *)malloc(sizeof(UPDATE));
  if (upd == NULL) {
    // avrdude_message(MSG_INFO, "%s: out of memory\n", progname);
    // exit(1);
    avrdude_oom("parse_op: out of memory\n");
  }

  i = 0;
  p = s;
  while ((i < (sizeof(buf)-1) && *p && (*p != ':')))
    buf[i++] = *p++;
  buf[i] = 0;

  if (*p != ':') {
    upd->memtype = NULL;        /* default memtype, "flash", or "application" */
    upd->op = DEVICE_WRITE;
    upd->filename = (char *)malloc(strlen(buf) + 1);
    if (upd->filename == NULL) {
        // avrdude_message(MSG_INFO, "%s: out of memory\n", progname);
        // exit(1);
        avrdude_oom("parse_op: out of memory\n");
    }
    strcpy(upd->filename, buf);
    upd->format = FMT_AUTO;
    return upd;
  }

  upd->memtype = (char *)malloc(strlen(buf)+1);
  if (upd->memtype == NULL) {
    // avrdude_message(MSG_INFO, "%s: out of memory\n", progname);
    // exit(1);
    avrdude_oom("parse_op: out of memory\n");
  }
  strcpy(upd->memtype, buf);

  p++;
  if (*p == 'r') {
    upd->op = DEVICE_READ;
  }
  else if (*p == 'w') {
    upd->op = DEVICE_WRITE;
  }
  else if (*p == 'v') {
    upd->op = DEVICE_VERIFY;
  }
  else {
    avrdude_message(MSG_INFO, "%s: invalid I/O mode '%c' in update specification\n",
            progname, *p);
    avrdude_message(MSG_INFO, "  allowed values are:\n"
                    "    r = read device\n"
                    "    w = write device\n"
                    "    v = verify device\n");
    free(upd->memtype);
    free(upd);
    return NULL;
  }

  p++;

  if (*p != ':') {
    avrdude_message(MSG_INFO, "%s: invalid update specification\n", progname);
    free(upd->memtype);
    free(upd);
    return NULL;
  }

  p++;

  // Extension: Parse file section number
  unsigned section = 0;

  for (; *p != ':'; p++) {
    if (*p >= '0' && *p <= '9') {
      section *= 10;
      section += *p - 0x30;
    } else {
      avrdude_message(MSG_INFO, "%s: invalid update specification: <section> is not a number\n", progname);
      free(upd->memtype);
      free(upd);
      return NULL;
    }
  }

  upd->section = section;
  p++;

  /*
   * Now, parse the filename component.  Instead of looking for the
   * leftmost possible colon delimiter, we look for the rightmost one.
   * If we found one, we do have a trailing :format specifier, and
   * process it.  Otherwise, the remainder of the string is our file
   * name component.  That way, the file name itself is allowed to
   * contain a colon itself (e. g. C:/some/file.hex), except the
   * optional format specifier becomes mandatory then.
   */
  cp = p;
  p = strrchr(cp, ':');
  if (p == NULL) {
    upd->format = FMT_AUTO;
    fnlen = strlen(cp);
    upd->filename = (char *)malloc(fnlen + 1);
  } else {
    fnlen = p - cp;
    upd->filename = (char *)malloc(fnlen +1);
    c = *++p;
    if (c && p[1])
      /* More than one char - force failure below. */
      c = '?';
    switch (c) {
      case 'a': upd->format = FMT_AUTO; break;
      case 's': upd->format = FMT_SREC; break;
      case 'i': upd->format = FMT_IHEX; break;
      case 'r': upd->format = FMT_RBIN; break;
      case 'e': upd->format = FMT_ELF; break;
      case 'm': upd->format = FMT_IMM; break;
      case 'b': upd->format = FMT_BIN; break;
      case 'd': upd->format = FMT_DEC; break;
      case 'h': upd->format = FMT_HEX; break;
      case 'o': upd->format = FMT_OCT; break;
      default:
        avrdude_message(MSG_INFO, "%s: invalid file format '%s' in update specifier\n",
                progname, p);
        free(upd->memtype);
        free(upd);
        return NULL;
    }
  }

  if (upd->filename == NULL) {
    avrdude_message(MSG_INFO, "%s: out of memory\n", progname);
    free(upd->memtype);
    free(upd);
    return NULL;
  }
  memcpy(upd->filename, cp, fnlen);
  upd->filename[fnlen] = 0;

  return upd;
}

UPDATE * dup_update(UPDATE * upd)
{
  UPDATE * u;

  u = (UPDATE *)malloc(sizeof(UPDATE));
  if (u == NULL) {
    // avrdude_message(MSG_INFO, "%s: out of memory\n", progname);
    // exit(1);
    avrdude_oom("dup_update: out of memory\n");
  }

  memcpy(u, upd, sizeof(UPDATE));

  if (upd->memtype != NULL)
    u->memtype = strdup(upd->memtype);
  else
    u->memtype = NULL;
  u->filename = strdup(upd->filename);

  return u;
}

UPDATE * new_update(int op, char * memtype, int filefmt, char * filename, unsigned section)
{
  UPDATE * u;

  u = (UPDATE *)malloc(sizeof(UPDATE));
  if (u == NULL) {
    // avrdude_message(MSG_INFO, "%s: out of memory\n", progname);
    // exit(1);
    avrdude_oom("new_update: out of memory\n");
  }

  u->memtype = strdup(memtype);
  u->filename = strdup(filename);
  u->op = op;
  u->format = filefmt;
  u->section = section;

  return u;
}

void free_update(UPDATE * u)
{
    if (u != NULL) {
	if(u->memtype != NULL) {
	    free(u->memtype);
	    u->memtype = NULL;
	}
	if(u->filename != NULL) {
	    free(u->filename);
	    u->filename = NULL;
	}
	free(u);
    }
}


int do_op(PROGRAMMER * pgm, struct avrpart * p, UPDATE * upd, enum updateflags flags)
{
  struct avrpart * v;
  AVRMEM * mem;
  int size, vsize;
  int rc;

  mem = avr_locate_mem(p, upd->memtype);
  if (mem == NULL) {
    avrdude_message(MSG_INFO, "\"%s\" memory type not defined for part \"%s\"\n",
            upd->memtype, p->desc);
    return -1;
  }

  if (upd->op == DEVICE_READ) {
    /*
     * read out the specified device memory and write it to a file
     */
    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: reading %s memory:\n",
            progname, mem->desc);
	  }
    report_progress(0,1,"Reading");
    rc = avr_read(pgm, p, upd->memtype, 0);
    if (rc < 0) {
      avrdude_message(MSG_INFO, "%s: failed to read all of %s memory, rc=%d\n",
              progname, mem->desc, rc);
      return -1;
    }
    report_progress(1,1,NULL);
    size = rc;

    if (quell_progress < 2) {
      if (rc == 0)
        avrdude_message(MSG_INFO, "%s: Flash is empty, resulting file has no contents.\n",
                        progname);
      avrdude_message(MSG_INFO, "%s: writing output file \"%s\"\n",
                      progname,
                      strcmp(upd->filename, "-")==0 ? "<stdout>" : upd->filename);
    }
    rc = fileio(FIO_WRITE, upd->filename, upd->format, p, upd->memtype, size, 0);
    if (rc < 0) {
      avrdude_message(MSG_INFO, "%s: write to file '%s' failed\n",
              progname, upd->filename);
      return -1;
    }
  }
  else if (upd->op == DEVICE_WRITE) {
    /*
     * write the selected device memory using data from a file; first
     * read the data from the specified file
     */
    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: reading input file \"%s\"\n",
                      progname,
                      strcmp(upd->filename, "-")==0 ? "<stdin>" : upd->filename);
    }
    rc = fileio(FIO_READ, upd->filename, upd->format, p, upd->memtype, -1, upd->section);
    if (rc < 0) {
      avrdude_message(MSG_INFO, "%s: read from file '%s' failed\n",
              progname, upd->filename);
      return -1;
    }
    size = rc;

    /*
     * write the buffer contents to the selected memory type
     */
    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: writing %s (%d bytes):\n",
            progname, mem->desc, size);
	  }

	//Prusa3D bootloader progress on lcd
	if (strcmp(pgm->type, "Wiring") == 0)
	{
		if (pgm->set_upload_size != 0)
			pgm->set_upload_size(pgm, size);
	}

    if (!(flags & UF_NOWRITE)) {
      report_progress(0,1,"Writing");
      rc = avr_write(pgm, p, upd->memtype, size, (flags & UF_AUTO_ERASE) != 0);
      report_progress(1,1,NULL);
    }
    else {
      // /*
      //  * test mode, don't actually write to the chip, output the buffer
      //  * to stdout in intel hex instead
      //  */
      // rc = fileio(FIO_WRITE, "-", FMT_IHEX, p, upd->memtype, size, 0);
    }

    if (rc < 0) {
      avrdude_message(MSG_INFO, "%s: failed to write %s memory, rc=%d\n",
              progname, mem->desc, rc);
      return -1;
    }

    vsize = rc;

    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: %d bytes of %s written\n", progname,
            vsize, mem->desc);
    }

  }
  else if (upd->op == DEVICE_VERIFY) {
    /*
     * verify that the in memory file (p->mem[AVR_M_FLASH|AVR_M_EEPROM])
     * is the same as what is on the chip
     */
    pgm->vfy_led(pgm, ON);

    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: verifying %s memory against %s:\n",
            progname, mem->desc, upd->filename);

      avrdude_message(MSG_INFO, "%s: load data %s data from input file %s:\n",
            progname, mem->desc, upd->filename);
    }

    rc = fileio(FIO_READ, upd->filename, upd->format, p, upd->memtype, -1, upd->section);
    if (rc < 0) {
      avrdude_message(MSG_INFO, "%s: read from file '%s' failed\n",
              progname, upd->filename);
      return -1;
    }
    v = avr_dup_part(p);
    size = rc;
    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: input file %s contains %d bytes\n",
            progname, upd->filename, size);
      avrdude_message(MSG_INFO, "%s: reading on-chip %s data:\n",
            progname, mem->desc);
    }

    report_progress (0,1,"Reading");
    rc = avr_read(pgm, p, upd->memtype, v);
    if (rc < 0) {
      avrdude_message(MSG_INFO, "%s: failed to read all of %s memory, rc=%d\n",
              progname, mem->desc, rc);
      pgm->err_led(pgm, ON);
      return -1;
    }
    report_progress (1,1,NULL);



    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: verifying ...\n", progname);
    }
    rc = avr_verify(p, v, upd->memtype, size);
    if (rc < 0) {
      avrdude_message(MSG_INFO, "%s: verification error; content mismatch\n",
              progname);
      pgm->err_led(pgm, ON);
      return -1;
    }

    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: %d bytes of %s verified\n",
              progname, rc, mem->desc);
    }

    pgm->vfy_led(pgm, OFF);
  }
  else {
    avrdude_message(MSG_INFO, "%s: invalid update operation (%d) requested\n",
            progname, upd->op);
    return -1;
  }

  return 0;
}

