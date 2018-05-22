/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2012 Joerg Wunsch <j@uriah.heep.sax.de>
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

#ifndef jtag3_h
#define jtag3_h

#ifdef __cplusplus
extern "C" {
#endif

int  jtag3_open_common(PROGRAMMER * pgm, char * port);
int  jtag3_send(PROGRAMMER * pgm, unsigned char * data, size_t len);
int  jtag3_recv(PROGRAMMER * pgm, unsigned char **msg);
void jtag3_close(PROGRAMMER * pgm);
int  jtag3_getsync(PROGRAMMER * pgm, int mode);
int  jtag3_getparm(PROGRAMMER * pgm, unsigned char scope,
		   unsigned char section, unsigned char parm,
		   unsigned char *value, unsigned char length);
int jtag3_setparm(PROGRAMMER * pgm, unsigned char scope,
		  unsigned char section, unsigned char parm,
		  unsigned char *value, unsigned char length);
int jtag3_command(PROGRAMMER *pgm, unsigned char *cmd, unsigned int cmdlen,
		  unsigned char **resp, const char *descr);
extern const char jtag3_desc[];
extern const char jtag3_dw_desc[];
extern const char jtag3_pdi_desc[];
void jtag3_initpgm (PROGRAMMER * pgm);
void jtag3_dw_initpgm (PROGRAMMER * pgm);
void jtag3_pdi_initpgm (PROGRAMMER * pgm);

/*
 * These functions are referenced from stk500v2.c for JTAGICE3 in
 * one of the STK500v2 modi.
 */
void jtag3_setup(PROGRAMMER * pgm);
void jtag3_teardown(PROGRAMMER * pgm);

#ifdef __cplusplus
}
#endif

#endif

