/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000-2006  Brian S. Dean <bsd@bsdhome.com>
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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include "freebsd_ppi.h"
#elif defined(__linux__)
# include "linux_ppdev.h"
#elif defined(__sun__) || defined(__sun) /* Solaris */
# include "solaris_ecpp.h"
#endif

#include "avrdude.h"
#include "libavrdude.h"

#include "ppi.h"
#include "bitbang.h"
#include "par.h"

#if HAVE_PARPORT

struct ppipins_t {
  int pin;
  int reg;
  int bit;
  int inverted;
};

static struct ppipins_t ppipins[] = {
  {  1, PPICTRL,   0x01, 1 },
  {  2, PPIDATA,   0x01, 0 },
  {  3, PPIDATA,   0x02, 0 },
  {  4, PPIDATA,   0x04, 0 },
  {  5, PPIDATA,   0x08, 0 },
  {  6, PPIDATA,   0x10, 0 },
  {  7, PPIDATA,   0x20, 0 },
  {  8, PPIDATA,   0x40, 0 },
  {  9, PPIDATA,   0x80, 0 },
  { 10, PPISTATUS, 0x40, 0 },
  { 11, PPISTATUS, 0x80, 1 },
  { 12, PPISTATUS, 0x20, 0 },
  { 13, PPISTATUS, 0x10, 0 },
  { 14, PPICTRL,   0x02, 1 }, 
  { 15, PPISTATUS, 0x08, 0 },
  { 16, PPICTRL,   0x04, 0 }, 
  { 17, PPICTRL,   0x08, 1 }
};

#define NPINS (sizeof(ppipins)/sizeof(struct ppipins_t))

static int par_setpin_internal(PROGRAMMER * pgm, int pin, int value)
{
  int inverted;

  inverted = pin & PIN_INVERSE;
  pin &= PIN_MASK;

  if (pin < 1 || pin > 17)
    return -1;

  pin--;

  if (ppipins[pin].inverted)
    inverted = !inverted;

  if (inverted)
    value = !value;

  if (value)
    ppi_set(&pgm->fd, ppipins[pin].reg, ppipins[pin].bit);
  else
    ppi_clr(&pgm->fd, ppipins[pin].reg, ppipins[pin].bit);

  if (pgm->ispdelay > 1)
    bitbang_delay(pgm->ispdelay);

  return 0;
}

static int par_setpin(PROGRAMMER * pgm, int pinfunc, int value)
{
  return par_setpin_internal(pgm, pgm->pinno[pinfunc], value);
}

static void par_setmany(PROGRAMMER * pgm, int pinfunc, int value)
{
  int pin, mask;
  int pinset = pgm->pinno[pinfunc];

  /* mask is anything non-pin - needs to be applied to each par_setpin to preserve inversion */
  mask = pinset & (~PIN_MASK);

  for (pin = 1; pin <= 17; pin++) {
    if (pinset & (1 << pin))
      par_setpin_internal(pgm, pin | mask, value);
  }
}

static int par_getpin(PROGRAMMER * pgm, int pinfunc)
{
  int value;
  int inverted;
  int pin = pgm->pinno[pinfunc];

  inverted = pin & PIN_INVERSE;
  pin &= PIN_MASK;

  if (pin < 1 || pin > 17)
    return -1;

  pin--;

  value = ppi_get(&pgm->fd, ppipins[pin].reg, ppipins[pin].bit);

  if (value)
    value = 1;
    
  if (ppipins[pin].inverted)
    inverted = !inverted;

  if (inverted)
    value = !value;

  return value;
}


static int par_highpulsepin(PROGRAMMER * pgm, int pinfunc)
{
  int inverted;
  int pin = pgm->pinno[pinfunc];

  inverted = pin & PIN_INVERSE;
  pin &= PIN_MASK;

  if (pin < 1 || pin > 17)
    return -1;

  pin--;

  if (ppipins[pin].inverted)
    inverted = !inverted;

  if (inverted) {
    ppi_clr(&pgm->fd, ppipins[pin].reg, ppipins[pin].bit);
    if (pgm->ispdelay > 1)
      bitbang_delay(pgm->ispdelay);

    ppi_set(&pgm->fd, ppipins[pin].reg, ppipins[pin].bit);
    if (pgm->ispdelay > 1)
      bitbang_delay(pgm->ispdelay);
  } else {
    ppi_set(&pgm->fd, ppipins[pin].reg, ppipins[pin].bit);
    if (pgm->ispdelay > 1)
      bitbang_delay(pgm->ispdelay);

    ppi_clr(&pgm->fd, ppipins[pin].reg, ppipins[pin].bit);
    if (pgm->ispdelay > 1)
      bitbang_delay(pgm->ispdelay);
  }

  return 0;
}

/*
 * apply power to the AVR processor
 */
static void par_powerup(PROGRAMMER * pgm)
{
  par_setmany(pgm, PPI_AVR_VCC, 1);	/* power up */
  usleep(100000);
}


/*
 * remove power from the AVR processor
 */
static void par_powerdown(PROGRAMMER * pgm)
{
  par_setmany(pgm, PPI_AVR_VCC, 0);	/* power down */
}

static void par_disable(PROGRAMMER * pgm)
{
  par_setmany(pgm, PPI_AVR_BUFF, 1); /* turn off */
}

static void par_enable(PROGRAMMER * pgm)
{
  /*
   * Prepare to start talking to the connected device - pull reset low
   * first, delay a few milliseconds, then enable the buffer.  This
   * sequence allows the AVR to be reset before the buffer is enabled
   * to avoid a short period of time where the AVR may be driving the
   * programming lines at the same time the programmer tries to.  Of
   * course, if a buffer is being used, then the /RESET line from the
   * programmer needs to be directly connected to the AVR /RESET line
   * and not via the buffer chip.
   */

  par_setpin(pgm, PIN_AVR_RESET, 0);
  usleep(1);

  /*
   * enable the 74367 buffer, if connected; this signal is active low
   */
  par_setmany(pgm, PPI_AVR_BUFF, 0);
}

static int par_open(PROGRAMMER * pgm, char * port)
{
  int rc;

  if (bitbang_check_prerequisites(pgm) < 0)
    return -1;

  ppi_open(port, &pgm->fd);
  if (pgm->fd.ifd < 0) {
    avrdude_message(MSG_INFO, "%s: failed to open parallel port \"%s\"\n\n",
            progname, port);
    return -1;
  }

  /*
   * save pin values, so they can be restored when device is closed
   */
  rc = ppi_getall(&pgm->fd, PPIDATA);
  if (rc < 0) {
    avrdude_message(MSG_INFO, "%s: error reading status of ppi data port\n", progname);
    return -1;
  }
  pgm->ppidata = rc;

  rc = ppi_getall(&pgm->fd, PPICTRL);
  if (rc < 0) {
    avrdude_message(MSG_INFO, "%s: error reading status of ppi ctrl port\n", progname);
    return -1;
  }
  pgm->ppictrl = rc;

  return 0;
}


static void par_close(PROGRAMMER * pgm)
{

  /*
   * Restore pin values before closing,
   * but ensure that buffers are turned off.
   */
  ppi_setall(&pgm->fd, PPIDATA, pgm->ppidata);
  ppi_setall(&pgm->fd, PPICTRL, pgm->ppictrl);

  par_setmany(pgm, PPI_AVR_BUFF, 1);

  /*
   * Handle exit specs.
   */
  switch (pgm->exit_reset) {
  case EXIT_RESET_ENABLED:
    par_setpin(pgm, PIN_AVR_RESET, 0);
    break;

  case EXIT_RESET_DISABLED:
    par_setpin(pgm, PIN_AVR_RESET, 1);
    break;

  case EXIT_RESET_UNSPEC:
    /* Leave it alone. */
    break;
  }

  switch (pgm->exit_datahigh) {
  case EXIT_DATAHIGH_ENABLED:
    ppi_setall(&pgm->fd, PPIDATA, 0xff);
    break;

  case EXIT_DATAHIGH_DISABLED:
    ppi_setall(&pgm->fd, PPIDATA, 0x00);
    break;

  case EXIT_DATAHIGH_UNSPEC:
    /* Leave it alone. */
    break;
  }

  switch (pgm->exit_vcc) {
  case EXIT_VCC_ENABLED:
    par_setmany(pgm, PPI_AVR_VCC, 1);
    break;

  case EXIT_VCC_DISABLED:
    par_setmany(pgm, PPI_AVR_VCC, 0);
    break;

  case EXIT_VCC_UNSPEC:
    /* Leave it alone. */
    break;
  }

  ppi_close(&pgm->fd);
  pgm->fd.ifd = -1;
}

/*
 * parse the -E string
 */
static int par_parseexitspecs(PROGRAMMER * pgm, char *s)
{
  char *cp;

  while ((cp = strtok(s, ","))) {
    if (strcmp(cp, "reset") == 0) {
      pgm->exit_reset = EXIT_RESET_ENABLED;
    }
    else if (strcmp(cp, "noreset") == 0) {
      pgm->exit_reset = EXIT_RESET_DISABLED;
    }
    else if (strcmp(cp, "vcc") == 0) {
      pgm->exit_vcc = EXIT_VCC_ENABLED;
    }
    else if (strcmp(cp, "novcc") == 0) {
      pgm->exit_vcc = EXIT_VCC_DISABLED;
    }
    else if (strcmp(cp, "d_high") == 0) {
      pgm->exit_datahigh = EXIT_DATAHIGH_ENABLED;
    }
    else if (strcmp(cp, "d_low") == 0) {
      pgm->exit_datahigh = EXIT_DATAHIGH_DISABLED;
    }
    else {
      return -1;
    }
    s = 0; /* strtok() should be called with the actual string only once */
  }

  return 0;
}

void par_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "PPI");

  pgm_fill_old_pins(pgm); // TODO to be removed if old pin data no longer needed

  pgm->exit_vcc = EXIT_VCC_UNSPEC;
  pgm->exit_reset = EXIT_RESET_UNSPEC;
  pgm->exit_datahigh = EXIT_DATAHIGH_UNSPEC;

  pgm->rdy_led        = bitbang_rdy_led;
  pgm->err_led        = bitbang_err_led;
  pgm->pgm_led        = bitbang_pgm_led;
  pgm->vfy_led        = bitbang_vfy_led;
  pgm->initialize     = bitbang_initialize;
  pgm->display        = pgm_display_generic;
  pgm->enable         = par_enable;
  pgm->disable        = par_disable;
  pgm->powerup        = par_powerup;
  pgm->powerdown      = par_powerdown;
  pgm->program_enable = bitbang_program_enable;
  pgm->chip_erase     = bitbang_chip_erase;
  pgm->cmd            = bitbang_cmd;
  pgm->cmd_tpi        = bitbang_cmd_tpi;
  pgm->spi            = bitbang_spi;
  pgm->open           = par_open;
  pgm->close          = par_close;
  pgm->setpin         = par_setpin;
  pgm->getpin         = par_getpin;
  pgm->highpulsepin   = par_highpulsepin;
  pgm->parseexitspecs = par_parseexitspecs;
  pgm->read_byte      = avr_read_byte_default;
  pgm->write_byte     = avr_write_byte_default;
}

#else  /* !HAVE_PARPORT */

void par_initpgm(PROGRAMMER * pgm)
{
  avrdude_message(MSG_INFO, "%s: parallel port access not available in this configuration\n",
                  progname);
}

#endif /* HAVE_PARPORT */

const char par_desc[] = "Parallel port bitbanging";
