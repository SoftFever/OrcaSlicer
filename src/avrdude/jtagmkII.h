/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2002-2004, 2006  Brian S. Dean <bsd@bsdhome.com>
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

#ifndef jtagmkII_h
#define jtagmkII_h

#ifdef __cplusplus
extern "C" {
#endif

int  jtagmkII_send(PROGRAMMER * pgm, unsigned char * data, size_t len);
int  jtagmkII_recv(PROGRAMMER * pgm, unsigned char **msg);
void jtagmkII_close(PROGRAMMER * pgm);
int  jtagmkII_getsync(PROGRAMMER * pgm, int mode);
int  jtagmkII_getparm(PROGRAMMER * pgm, unsigned char parm,
		      unsigned char * value);

extern const char jtagmkII_desc[];
extern const char jtagmkII_avr32_desc[];
extern const char jtagmkII_dw_desc[];
extern const char jtagmkII_pdi_desc[];
extern const char jtagmkII_dragon_desc[];
extern const char jtagmkII_dragon_dw_desc[];
extern const char jtagmkII_dragon_pdi_desc[];
void jtagmkII_initpgm (PROGRAMMER * pgm);
void jtagmkII_avr32_initpgm (PROGRAMMER * pgm);
void jtagmkII_dw_initpgm (PROGRAMMER * pgm);
void jtagmkII_pdi_initpgm (PROGRAMMER * pgm);
void jtagmkII_dragon_initpgm (PROGRAMMER * pgm);
void jtagmkII_dragon_dw_initpgm (PROGRAMMER * pgm);
void jtagmkII_dragon_pdi_initpgm (PROGRAMMER * pgm);

/*
 * These functions are referenced from stk500v2.c for JTAG ICE mkII
 * and AVR Dragon programmers running in one of the STK500v2
 * modi.
 */
void jtagmkII_setup(PROGRAMMER * pgm);
void jtagmkII_teardown(PROGRAMMER * pgm);

#ifdef __cplusplus
}
#endif

#endif

