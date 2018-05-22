/*
 * avrdude - A Downloader/Uploader for AVR device programmers
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

/*
 * avrdude interface for Atmel STK500 programmer
 *
 * This is a wrapper around the STK500[v1] and STK500v2 programmers.
 * Try to select the programmer type that actually responds, and
 * divert to the actual programmer implementation if successful.
 */

#include "ac_cfg.h"

#include <stdio.h>
#include <string.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "stk500generic.h"
#include "stk500.h"
#include "stk500v2.h"

static int stk500generic_open(PROGRAMMER * pgm, char * port)
{
  stk500_initpgm(pgm);
  if (pgm->open(pgm, port) >= 0)
    {
      avrdude_message(MSG_INFO, "%s: successfully opened stk500v1 device -- please use -c stk500v1\n",
                      progname);
      return 0;
    }

  pgm->close(pgm);

  stk500v2_initpgm(pgm);
  if (pgm->open(pgm, port) >= 0)
    {
      avrdude_message(MSG_INFO, "%s: successfully opened stk500v2 device -- please use -c stk500v2\n",
                      progname);
      return 0;
    }

  avrdude_message(MSG_INFO, "%s: cannot open either stk500v1 or stk500v2 programmer\n",
                  progname);
  return -1;
}

static void stk500generic_setup(PROGRAMMER * pgm)
{
  /*
   * Only STK500v2 needs setup/teardown.
   */
  stk500v2_initpgm(pgm);
  pgm->setup(pgm);
}

static void stk500generic_teardown(PROGRAMMER * pgm)
{
  stk500v2_initpgm(pgm);
  pgm->teardown(pgm);
}

const char stk500generic_desc[] = "Atmel STK500, autodetect firmware version";

void stk500generic_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "STK500GENERIC");

  pgm->open           = stk500generic_open;
  pgm->setup          = stk500generic_setup;
  pgm->teardown       = stk500generic_teardown;
}
