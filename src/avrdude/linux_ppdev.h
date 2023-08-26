/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2003, 2005 Theodore A. Roth
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

#ifndef linux_ppdev_h
#define linux_ppdev_h

#define OBSOLETE__IOW _IOW

#include <sys/ioctl.h>
#ifdef HAVE_PARPORT
#include <linux/parport.h>
#include <linux/ppdev.h>
#endif

#include <stdlib.h>

#define ppi_claim(fd)                                        \
  if (ioctl(fd, PPCLAIM)) {                                  \
    avrdude_message(MSG_INFO, "%s: can't claim device \"%s\": %s\n\n", \
            progname, port, strerror(errno));                \
    close(fd);                                               \
    return;                                                  \
  }

#define ppi_release(fd)                                      \
  if (ioctl(fd, PPRELEASE)) {                                \
    avrdude_message(MSG_INFO, "%s: can't release device: %s\n\n", \
            progname, strerror(errno));                      \
  }

#define DO_PPI_READ(fd, reg, valp) \
	(void)ioctl(fd, \
		(reg) == PPIDATA? PPRDATA: ((reg) == PPICTRL? PPRCONTROL: PPRSTATUS), \
		    valp)
#define DO_PPI_WRITE(fd, reg, valp) \
	(void)ioctl(fd, \
		(reg) == PPIDATA? PPWDATA: ((reg) == PPICTRL? PPWCONTROL: PPWSTATUS), \
		    valp)

#endif /* linux_ppdev_h */
