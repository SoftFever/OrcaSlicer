/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2002-2004  Brian S. Dean <bsd@bsdhome.com>
 * Copyright 2007 Joerg Wunsch <j@uriah.heep.sax.de>
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

/* $Id: pgm.c 976 2011-08-23 21:03:36Z joerg_wunsch $ */

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "arduino.h"
#include "avr910.h"
// #include "avrftdi.h"
#include "buspirate.h"
#include "butterfly.h"
// #include "flip1.h"
// #include "flip2.h"
// #include "ft245r.h"
// #include "jtagmkI.h"
// #include "jtagmkII.h"
// #include "jtag3.h"
#include "linuxgpio.h"
// #include "par.h"
#include "pickit2.h"
#include "ppi.h"
#include "serbb.h"
#include "stk500.h"
#include "stk500generic.h"
#include "stk500v2.h"
// #include "usbasp.h"
// #include "usbtiny.h"
#include "wiring.h"


const PROGRAMMER_TYPE programmers_types[] = {
        {"arduino", arduino_initpgm, arduino_desc},
        {"avr910", avr910_initpgm, avr910_desc},
        // {"avrftdi", avrftdi_initpgm, avrftdi_desc},
        {"buspirate", buspirate_initpgm, buspirate_desc},
        {"buspirate_bb", buspirate_bb_initpgm, buspirate_bb_desc},
        {"butterfly", butterfly_initpgm, butterfly_desc},
        {"butterfly_mk", butterfly_mk_initpgm, butterfly_mk_desc},
        // {"dragon_dw", jtagmkII_dragon_dw_initpgm, jtagmkII_dragon_dw_desc},
        {"dragon_hvsp", stk500v2_dragon_hvsp_initpgm, stk500v2_dragon_hvsp_desc},
        {"dragon_isp", stk500v2_dragon_isp_initpgm, stk500v2_dragon_isp_desc},
        // {"dragon_jtag", jtagmkII_dragon_initpgm, jtagmkII_dragon_desc},
        // {"dragon_pdi", jtagmkII_dragon_pdi_initpgm, jtagmkII_dragon_pdi_desc},
        {"dragon_pp", stk500v2_dragon_pp_initpgm, stk500v2_dragon_pp_desc},
        // {"flip1", flip1_initpgm, flip1_desc},
        // {"flip2", flip2_initpgm, flip2_desc},
        // {"ftdi_syncbb", ft245r_initpgm, ft245r_desc},
        // {"jtagmki", jtagmkI_initpgm, jtagmkI_desc},
        // {"jtagmkii", jtagmkII_initpgm, jtagmkII_desc},
        // {"jtagmkii_avr32", jtagmkII_avr32_initpgm, jtagmkII_avr32_desc},
        // {"jtagmkii_dw", jtagmkII_dw_initpgm, jtagmkII_dw_desc},
        // {"jtagmkii_isp", stk500v2_jtagmkII_initpgm, stk500v2_jtagmkII_desc},
        // {"jtagmkii_pdi", jtagmkII_pdi_initpgm, jtagmkII_pdi_desc},
        // {"jtagice3", jtag3_initpgm, jtag3_desc},
        // {"jtagice3_pdi", jtag3_pdi_initpgm, jtag3_pdi_desc},
        // {"jtagice3_dw", jtag3_dw_initpgm, jtag3_dw_desc},
        // {"jtagice3_isp", stk500v2_jtag3_initpgm, stk500v2_jtag3_desc},
        {"linuxgpio", linuxgpio_initpgm, linuxgpio_desc},
        // {"par", par_initpgm, par_desc},
        {"pickit2", pickit2_initpgm, pickit2_desc},
        {"serbb", serbb_initpgm, serbb_desc},
        {"stk500", stk500_initpgm, stk500_desc},
        {"stk500generic", stk500generic_initpgm, stk500generic_desc},
        {"stk500v2", stk500v2_initpgm, stk500v2_desc},
        {"stk500hvsp", stk500hvsp_initpgm, stk500hvsp_desc},
        {"stk500pp", stk500pp_initpgm, stk500pp_desc},
        {"stk600", stk600_initpgm, stk600_desc},
        {"stk600hvsp", stk600hvsp_initpgm, stk600hvsp_desc},
        {"stk600pp", stk600pp_initpgm, stk600pp_desc},
        // {"usbasp", usbasp_initpgm, usbasp_desc},
        // {"usbtiny", usbtiny_initpgm, usbtiny_desc},
        {"wiring", wiring_initpgm, wiring_desc},
};

const PROGRAMMER_TYPE * locate_programmer_type(const char * id)
{
  const PROGRAMMER_TYPE * p = NULL;
  int i;
  int found;

  found = 0;

  for (i = 0; i < sizeof(programmers_types)/sizeof(programmers_types[0]) && !found; i++) {
    p = &(programmers_types[i]);
    if (strcasecmp(id, p->id) == 0)
        found = 1;
  }

  if (found)
    return p;

  return NULL;
}

/*
 * Iterate over the list of programmers given as "programmers", and
 * call the callback function cb for each entry found.  cb is being
 * passed the following arguments:
 * . the name of the programmer (for -c)
 * . the descriptive text given in the config file
 * . the name of the config file this programmer has been defined in
 * . the line number of the config file this programmer has been defined at
 * . the "cookie" passed into walk_programmers() (opaque client data)
 */
 /*
void walk_programmer_types(LISTID programmer_types, walk_programmer_types_cb cb, void *cookie)
{
  LNODEID ln1;
  PROGRAMMER * p;

  for (ln1 = lfirst(programmers); ln1; ln1 = lnext(ln1)) {
    p = ldata(ln1);
      cb(p->id, p->desc, cookie);
    }
  }
}*/

void walk_programmer_types(walk_programmer_types_cb cb, void *cookie)
{
  const PROGRAMMER_TYPE * p;
  int i;

  for (i = 0; i < sizeof(programmers_types)/sizeof(programmers_types[0]); i++) {
    p = &(programmers_types[i]);
    cb(p->id, p->desc, cookie);
  }
}


