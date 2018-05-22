/*
 * avrftdi - extension for avrdude, Wolfgang Moser, Ville Voipio
 * Copyright (C) 2011 Hannes Weisbach, Doug Springer
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

#ifndef avrftdi_h
#define avrftdi_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


extern const char avrftdi_desc[];
void avrftdi_initpgm        (PROGRAMMER * pgm);

#ifdef __cplusplus
}
#endif

#endif


