/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2002-2005  Brian S. Dean <bsd@bsdhome.com>
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

#ifndef stk500v2_h
#define stk500v2_h

#ifdef __cplusplus
extern "C" {
#endif

extern const char stk500v2_desc[];
extern const char stk500hvsp_desc[];
extern const char stk500pp_desc[];
extern const char stk500v2_jtagmkII_desc[];
extern const char stk500v2_dragon_hvsp_desc[];
extern const char stk500v2_dragon_isp_desc[];
extern const char stk500v2_dragon_pp_desc[];
extern const char stk500v2_jtag3_desc[];
extern const char stk600_desc[];
extern const char stk600hvsp_desc[];
extern const char stk600pp_desc[];
void stk500v2_initpgm (PROGRAMMER * pgm);
void stk500hvsp_initpgm (PROGRAMMER * pgm);
void stk500pp_initpgm (PROGRAMMER * pgm);
void stk500v2_jtagmkII_initpgm(PROGRAMMER * pgm);
void stk500v2_jtag3_initpgm(PROGRAMMER * pgm);
void stk500v2_dragon_hvsp_initpgm(PROGRAMMER * pgm);
void stk500v2_dragon_isp_initpgm(PROGRAMMER * pgm);
void stk500v2_dragon_pp_initpgm(PROGRAMMER * pgm);
void stk600_initpgm (PROGRAMMER * pgm);
void stk600hvsp_initpgm (PROGRAMMER * pgm);
void stk600pp_initpgm (PROGRAMMER * pgm);

void stk500v2_setup(PROGRAMMER * pgm);
void stk500v2_teardown(PROGRAMMER * pgm);
int stk500v2_drain(PROGRAMMER * pgm, int display);
int stk500v2_getsync(PROGRAMMER * pgm);

void stk500v2_set_upload_size(PROGRAMMER * pgm, int size);

#ifdef __cplusplus
}
#endif

#endif


