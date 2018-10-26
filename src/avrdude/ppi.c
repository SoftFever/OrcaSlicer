/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000-2004  Brian S. Dean <bsd@bsdhome.com>
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


#if !defined(WIN32NATIVE)

#include "ac_cfg.h"

#if HAVE_PARPORT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include "freebsd_ppi.h"
#elif defined(__linux__)
# include "linux_ppdev.h"
#elif defined(__sun__) || defined(__sun) /* Solaris */
# include "solaris_ecpp.h"
#endif

#include "avrdude.h"
#include "libavrdude.h"

#include "ppi.h"

enum {
  PPI_READ,
  PPI_WRITE,
  PPI_SHADOWREAD
};

static int ppi_shadow_access(union filedescriptor *fdp, int reg,
			     unsigned char *v, unsigned char action)
{
  static unsigned char shadow[3];
  int shadow_num;

  switch (reg) {
    case PPIDATA:
      shadow_num = 0;
      break;
    case PPICTRL:
      shadow_num = 1;
      break;
    case PPISTATUS:
      shadow_num = 2;
      break;
    default:
      avrdude_message(MSG_INFO, "%s: avr_set(): invalid register=%d\n",
              progname, reg);
      return -1;
      break;
  }

  switch (action) {
    case PPI_SHADOWREAD:
      *v = shadow[shadow_num];
      break;
    case PPI_READ:
      DO_PPI_READ(fdp->ifd, reg, v);
      shadow[shadow_num]=*v;
      break;
    case PPI_WRITE:
      shadow[shadow_num]=*v;
      DO_PPI_WRITE(fdp->ifd, reg, v);
      break;
  }
  return 0;
}

/*
 * set the indicated bit of the specified register.
 */
int ppi_set(union filedescriptor *fdp, int reg, int bit)
{
  unsigned char v;
  int rc;

  rc = ppi_shadow_access(fdp, reg, &v, PPI_SHADOWREAD);
  v |= bit;
  rc |= ppi_shadow_access(fdp, reg, &v, PPI_WRITE);

  if (rc)
    return -1;

  return 0;
}


/*
 * clear the indicated bit of the specified register.
 */
int ppi_clr(union filedescriptor *fdp, int reg, int bit)
{
  unsigned char v;
  int rc;

  rc = ppi_shadow_access(fdp, reg, &v, PPI_SHADOWREAD);
  v &= ~bit;
  rc |= ppi_shadow_access(fdp, reg, &v, PPI_WRITE);

  if (rc)
    return -1;

  return 0;
}


/*
 * get the indicated bit of the specified register.
 */
int ppi_get(union filedescriptor *fdp, int reg, int bit)
{
  unsigned char v;
  int rc;

  rc = ppi_shadow_access(fdp, reg, &v, PPI_READ);
  v &= bit;

  if (rc)
    return -1;

  return v; /* v == bit */
}

/*
 * toggle the indicated bit of the specified register.
 */
int ppi_toggle(union filedescriptor *fdp, int reg, int bit)
{
  unsigned char v;
  int rc;

  rc = ppi_shadow_access(fdp, reg, &v, PPI_SHADOWREAD);
  v ^= bit;
  rc |= ppi_shadow_access(fdp, reg, &v, PPI_WRITE);

  if (rc)
    return -1;

  return 0;
}


/*
 * get all bits of the specified register.
 */
int ppi_getall(union filedescriptor *fdp, int reg)
{
  unsigned char v;
  int rc;

  rc = ppi_shadow_access(fdp, reg, &v, PPI_READ);

  if (rc)
    return -1;

  return v; /* v == bit */
}

/*
 * set all bits of the specified register to val.
 */
int ppi_setall(union filedescriptor *fdp, int reg, int val)
{
  unsigned char v;
  int rc;

  v = val;
  rc = ppi_shadow_access(fdp, reg, &v, PPI_WRITE);

  if (rc)
    return -1;

  return 0;
}


void ppi_open(char * port, union filedescriptor *fdp)
{
  int fd;
  unsigned char v;

  fd = open(port, O_RDWR);
  if (fd < 0) {
    avrdude_message(MSG_INFO, "%s: can't open device \"%s\": %s\n",
              progname, port, strerror(errno));
    fdp->ifd = -1;
    return;
  }

  ppi_claim (fd);

  /*
   * Initialize shadow registers
   */

  ppi_shadow_access (fdp, PPIDATA, &v, PPI_READ);
  ppi_shadow_access (fdp, PPICTRL, &v, PPI_READ);
  ppi_shadow_access (fdp, PPISTATUS, &v, PPI_READ);

  fdp->ifd = fd;
}


void ppi_close(union filedescriptor *fdp)
{
  ppi_release (fdp->ifd);
  close(fdp->ifd);
}

#endif /* HAVE_PARPORT */

#endif /* !WIN32NATIVE */
