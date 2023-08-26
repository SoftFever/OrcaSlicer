/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2013 Radoslav Kolev <radoslav@kolev.info>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* $Id: par.h 722 2007-01-24 22:43:46Z joerg_wunsch $ */

#ifndef linuxgpio_h
#define linuxgpio_h

#ifdef __cplusplus
extern "C" {
#endif

extern const char linuxgpio_desc[];
void linuxgpio_initpgm        (PROGRAMMER * pgm);

#ifdef __cplusplus
}
#endif

#endif
