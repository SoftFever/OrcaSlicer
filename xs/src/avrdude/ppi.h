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

#ifndef ppi_h
#define ppi_h

/*
 * PPI registers
 */
enum {
  PPIDATA,
  PPICTRL,
  PPISTATUS
};

#ifdef __cplusplus
extern "C" {
#endif

int ppi_get       (union filedescriptor *fdp, int reg, int bit);

int ppi_set       (union filedescriptor *fdp, int reg, int bit);

int ppi_clr       (union filedescriptor *fdp, int reg, int bit);

int ppi_getall    (union filedescriptor *fdp, int reg);

int ppi_setall    (union filedescriptor *fdp, int reg, int val);

int ppi_toggle    (union filedescriptor *fdp, int reg, int bit);

void ppi_open     (char * port, union filedescriptor *fdp);

void ppi_close    (union filedescriptor *fdp);

#ifdef __cplusplus
}
#endif

#endif


