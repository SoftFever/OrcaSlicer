/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 *
 * avrdude support for The Bus Pirate - universal serial interface
 *
 * Copyright (C) 2009 Michal Ludvig <mludvig@logix.net.nz>
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

#ifndef buspirate_h
#define buspirate_h

extern const char buspirate_desc[];
extern const char buspirate_bb_desc[];
void buspirate_initpgm (struct programmer_t *pgm);
void buspirate_bb_initpgm (struct programmer_t *pgm);

#endif
