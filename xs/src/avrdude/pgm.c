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

/* $Id$ */

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avrdude.h"
#include "libavrdude.h"

static int  pgm_default_2 (struct programmer_t *, AVRPART *);
static int  pgm_default_3 (struct programmer_t * pgm, AVRPART * p, AVRMEM * mem,
			   unsigned long addr, unsigned char * value);
static void pgm_default_4 (struct programmer_t *);
static int  pgm_default_5 (struct programmer_t * pgm, AVRPART * p, AVRMEM * mem,
			   unsigned long addr, unsigned char data);
static void pgm_default_6 (struct programmer_t *, const char *);


static int pgm_default_open (struct programmer_t *pgm, char * name)
{
  avrdude_message(MSG_INFO, "\n%s: Fatal error: Programmer does not support open()",
               progname);
  return -1;
}

static int  pgm_default_led (struct programmer_t * pgm, int value)
{
  /*
   * If programmer has no LEDs, just do nothing.
   */
  return 0;
}


static void pgm_default_powerup_powerdown (struct programmer_t * pgm)
{
  /*
   * If programmer does not support powerup/down, just do nothing.
   */
}


PROGRAMMER * pgm_new(void)
{
  int i;
  PROGRAMMER * pgm;

  pgm = (PROGRAMMER *)malloc(sizeof(*pgm));
  if (pgm == NULL) {
    avrdude_message(MSG_INFO, "%s: out of memory allocating programmer structure\n",
            progname);
    return NULL;
  }

  memset(pgm, 0, sizeof(*pgm));

  pgm->id = lcreat(NULL, 0);
  pgm->usbpid = lcreat(NULL, 0);
  pgm->desc[0] = 0;
  pgm->type[0] = 0;
  pgm->config_file[0] = 0;
  pgm->lineno = 0;
  pgm->baudrate = 0;
  pgm->initpgm = NULL;

  for (i=0; i<N_PINS; i++) {
    pgm->pinno[i] = 0;
    pin_clear_all(&(pgm->pin[i]));
  }

  /*
   * mandatory functions - these are called without checking to see
   * whether they are assigned or not
   */
  pgm->initialize     = pgm_default_2;
  pgm->display        = pgm_default_6;
  pgm->enable         = pgm_default_4;
  pgm->disable        = pgm_default_4;
  pgm->powerup        = pgm_default_powerup_powerdown;
  pgm->powerdown      = pgm_default_powerup_powerdown;
  pgm->program_enable = pgm_default_2;
  pgm->chip_erase     = pgm_default_2;
  pgm->open           = pgm_default_open;
  pgm->close          = pgm_default_4;
  pgm->read_byte      = pgm_default_3;
  pgm->write_byte     = pgm_default_5;

  /*
   * predefined functions - these functions have a valid default
   * implementation. Hence, they don't need to be defined in
   * the programmer.
   */
  pgm->rdy_led        = pgm_default_led;
  pgm->err_led        = pgm_default_led;
  pgm->pgm_led        = pgm_default_led;
  pgm->vfy_led        = pgm_default_led;

  /*
   * optional functions - these are checked to make sure they are
   * assigned before they are called
   */
  pgm->cmd            = NULL;
  pgm->cmd_tpi        = NULL;
  pgm->spi            = NULL;
  pgm->paged_write    = NULL;
  pgm->paged_load     = NULL;
  pgm->write_setup    = NULL;
  pgm->read_sig_bytes = NULL;
  pgm->set_vtarget    = NULL;
  pgm->set_varef      = NULL;
  pgm->set_fosc       = NULL;
  pgm->perform_osccal = NULL;
  pgm->parseextparams = NULL;
  pgm->setup          = NULL;
  pgm->teardown       = NULL;

  return pgm;
}

void pgm_free(PROGRAMMER * const p)
{
  ldestroy_cb(p->id, free);
  ldestroy_cb(p->usbpid, free);
  p->id = NULL;
  p->usbpid = NULL;
  /* this is done by pgm_teardown, but usually cookie is not set to NULL */
  /* if (p->cookie !=NULL) {
    free(p->cookie);
    p->cookie = NULL;
  }*/
  free(p);
}

PROGRAMMER * pgm_dup(const PROGRAMMER * const src)
{
  PROGRAMMER * pgm;
  LNODEID ln;

  pgm = (PROGRAMMER *)malloc(sizeof(*pgm));
  if (pgm == NULL) {
    avrdude_message(MSG_INFO, "%s: out of memory allocating programmer structure\n",
            progname);
    return NULL;
  }

  memcpy(pgm, src, sizeof(*pgm));

  pgm->id = lcreat(NULL, 0);
  pgm->usbpid = lcreat(NULL, 0);

  for (ln = lfirst(src->usbpid); ln; ln = lnext(ln)) {
    int *ip = malloc(sizeof(int));
    if (ip == NULL) {
      avrdude_message(MSG_INFO, "%s: out of memory allocating programmer structure\n",
              progname);
      exit(1);
    }
    *ip = *(int *) ldata(ln);
    ladd(pgm->usbpid, ip);
  }

  return pgm;
}


static void pgm_default(void)
{
  avrdude_message(MSG_INFO, "%s: programmer operation not supported\n", progname);
}


static int  pgm_default_2 (struct programmer_t * pgm, AVRPART * p)
{
  pgm_default();
  return -1;
}

static int  pgm_default_3 (struct programmer_t * pgm, AVRPART * p, AVRMEM * mem,
			   unsigned long addr, unsigned char * value)
{
  pgm_default();
  return -1;
}

static void pgm_default_4 (struct programmer_t * pgm)
{
  pgm_default();
}

static int  pgm_default_5 (struct programmer_t * pgm, AVRPART * p, AVRMEM * mem,
			   unsigned long addr, unsigned char data)
{
  pgm_default();
  return -1;
}

static void pgm_default_6 (struct programmer_t * pgm, const char * p)
{
  pgm_default();
}


void programmer_display(PROGRAMMER * pgm, const char * p)
{
  avrdude_message(MSG_INFO, "%sProgrammer Type : %s\n", p, pgm->type);
  avrdude_message(MSG_INFO, "%sDescription     : %s\n", p, pgm->desc);

  pgm->display(pgm, p);
}


void pgm_display_generic_mask(PROGRAMMER * pgm, const char * p, unsigned int show)
{
  if(show & (1<<PPI_AVR_VCC)) 
    avrdude_message(MSG_INFO, "%s  VCC     = %s\n", p, pins_to_str(&pgm->pin[PPI_AVR_VCC]));
  if(show & (1<<PPI_AVR_BUFF))
    avrdude_message(MSG_INFO, "%s  BUFF    = %s\n", p, pins_to_str(&pgm->pin[PPI_AVR_BUFF]));
  if(show & (1<<PIN_AVR_RESET))
    avrdude_message(MSG_INFO, "%s  RESET   = %s\n", p, pins_to_str(&pgm->pin[PIN_AVR_RESET]));
  if(show & (1<<PIN_AVR_SCK))
    avrdude_message(MSG_INFO, "%s  SCK     = %s\n", p, pins_to_str(&pgm->pin[PIN_AVR_SCK]));
  if(show & (1<<PIN_AVR_MOSI))
    avrdude_message(MSG_INFO, "%s  MOSI    = %s\n", p, pins_to_str(&pgm->pin[PIN_AVR_MOSI]));
  if(show & (1<<PIN_AVR_MISO))
    avrdude_message(MSG_INFO, "%s  MISO    = %s\n", p, pins_to_str(&pgm->pin[PIN_AVR_MISO]));
  if(show & (1<<PIN_LED_ERR))
    avrdude_message(MSG_INFO, "%s  ERR LED = %s\n", p, pins_to_str(&pgm->pin[PIN_LED_ERR]));
  if(show & (1<<PIN_LED_RDY))
    avrdude_message(MSG_INFO, "%s  RDY LED = %s\n", p, pins_to_str(&pgm->pin[PIN_LED_RDY]));
  if(show & (1<<PIN_LED_PGM))
    avrdude_message(MSG_INFO, "%s  PGM LED = %s\n", p, pins_to_str(&pgm->pin[PIN_LED_PGM]));
  if(show & (1<<PIN_LED_VFY))
    avrdude_message(MSG_INFO, "%s  VFY LED = %s\n", p, pins_to_str(&pgm->pin[PIN_LED_VFY]));
}

void pgm_display_generic(PROGRAMMER * pgm, const char * p)
{
  pgm_display_generic_mask(pgm, p, SHOW_ALL_PINS);
}

PROGRAMMER * locate_programmer(LISTID programmers, const char * configid)
{
  LNODEID ln1, ln2;
  PROGRAMMER * p = NULL;
  const char * id;
  int found;

  found = 0;

  for (ln1=lfirst(programmers); ln1 && !found; ln1=lnext(ln1)) {
    p = ldata(ln1);
    for (ln2=lfirst(p->id); ln2 && !found; ln2=lnext(ln2)) {
      id = ldata(ln2);
      if (strcasecmp(configid, id) == 0)
        found = 1;
    }
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
void walk_programmers(LISTID programmers, walk_programmers_cb cb, void *cookie)
{
  LNODEID ln1;
  LNODEID ln2;
  PROGRAMMER * p;

  for (ln1 = lfirst(programmers); ln1; ln1 = lnext(ln1)) {
    p = ldata(ln1);
    for (ln2=lfirst(p->id); ln2; ln2=lnext(ln2)) {
      cb(ldata(ln2), p->desc, p->config_file, p->lineno, cookie);
    }
  }
}

/*
 * Compare function to sort the list of programmers
 */
static int sort_programmer_compare(PROGRAMMER * p1,PROGRAMMER * p2)
{
  char* id1;
  char* id2;
  if(p1 == NULL || p2 == NULL) {
    return 0;
  }
  id1 = ldata(lfirst(p1->id));
  id2 = ldata(lfirst(p2->id));
  return strncasecmp(id1,id2,AVR_IDLEN);
}

/*
 * Sort the list of programmers given as "programmers"
 */
void sort_programmers(LISTID programmers)
{
  lsort(programmers,(int (*)(void*, void*)) sort_programmer_compare);
}

