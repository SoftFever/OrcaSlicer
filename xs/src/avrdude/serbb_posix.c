/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000, 2001, 2002, 2003  Brian S. Dean <bsd@bsdhome.com>
 * Copyright (C) 2005 Michael Holzt <kju-avr@fqdn.org>
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
 * Posix serial bitbanging interface for avrdude.
 */

#if !defined(WIN32NATIVE)

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "bitbang.h"
#include "serbb.h"

#undef DEBUG

static struct termios oldmode;

/*
  serial port/pin mapping

  1	cd	<-
  2	(rxd)	<-
  3	txd	->
  4	dtr	->
  5	GND
  6	dsr	<-
  7	rts	->
  8	cts	<-
  9	ri	<-
*/

#define DB9PINS 9

static int serregbits[DB9PINS + 1] =
{ 0, TIOCM_CD, 0, 0, TIOCM_DTR, 0, TIOCM_DSR, TIOCM_RTS, TIOCM_CTS, TIOCM_RI };

#ifdef DEBUG
static char *serpins[DB9PINS + 1] =
  { "NONE", "CD", "RXD", "TXD", "DTR", "GND", "DSR", "RTS", "CTS", "RI" };
#endif

static int serbb_setpin(PROGRAMMER * pgm, int pinfunc, int value)
{
  unsigned int	ctl;
  int           r;
  int pin = pgm->pinno[pinfunc]; // get its value

  if (pin & PIN_INVERSE)
  {
    value  = !value;
    pin   &= PIN_MASK;
  }

  if ( pin < 1 || pin > DB9PINS )
    return -1;

#ifdef DEBUG
  printf("%s to %d\n",serpins[pin],value);
#endif

  switch ( pin )
  {
    case 3:  /* txd */
	     r = ioctl(pgm->fd.ifd, value ? TIOCSBRK : TIOCCBRK, 0);
	     if (r < 0) {
	       perror("ioctl(\"TIOCxBRK\")");
	       return -1;
	     }
             break;

    case 4:  /* dtr */
    case 7:  /* rts */
             r = ioctl(pgm->fd.ifd, TIOCMGET, &ctl);
 	     if (r < 0) {
	       perror("ioctl(\"TIOCMGET\")");
	       return -1;
 	     }
             if ( value )
               ctl |= serregbits[pin];
             else
               ctl &= ~(serregbits[pin]);
	     r = ioctl(pgm->fd.ifd, TIOCMSET, &ctl);
 	     if (r < 0) {
	       perror("ioctl(\"TIOCMSET\")");
	       return -1;
 	     }
             break;

    default: /* impossible */
             return -1;
  }

  if (pgm->ispdelay > 1)
    bitbang_delay(pgm->ispdelay);

  return 0;
}

static int serbb_getpin(PROGRAMMER * pgm, int pinfunc)
{
  unsigned int	ctl;
  unsigned char invert;
  int           r;
  int pin = pgm->pinno[pinfunc]; // get its value

  if (pin & PIN_INVERSE)
  {
    invert = 1;
    pin   &= PIN_MASK;
  } else
    invert = 0;

  if ( pin < 1 || pin > DB9PINS )
    return(-1);

  switch ( pin )
  {
    case 2:  /* rxd, currently not implemented, FIXME */
             return(-1);

    case 1:  /* cd  */
    case 6:  /* dsr */
    case 8:  /* cts */
    case 9:  /* ri  */
             r = ioctl(pgm->fd.ifd, TIOCMGET, &ctl);
 	     if (r < 0) {
	       perror("ioctl(\"TIOCMGET\")");
	       return -1;
 	     }
             if ( !invert )
             {
#ifdef DEBUG
               printf("%s is %d\n",serpins[pin],(ctl & serregbits[pin]) ? 1 : 0 );
#endif
               return ( (ctl & serregbits[pin]) ? 1 : 0 );
             }
             else
             {
#ifdef DEBUG
               printf("%s is %d (~)\n",serpins[pin],(ctl & serregbits[pin]) ? 0 : 1 );
#endif
               return (( ctl & serregbits[pin]) ? 0 : 1 );
             }

    default: /* impossible */
             return(-1);
  }
}

static int serbb_highpulsepin(PROGRAMMER * pgm, int pinfunc)
{
  int pin = pgm->pinno[pinfunc]; // replace pin name by its value

  if ( (pin & PIN_MASK) < 1 || (pin & PIN_MASK) > DB9PINS )
    return -1;

  serbb_setpin(pgm, pinfunc, 1);
  serbb_setpin(pgm, pinfunc, 0);

  return 0;
}



static void serbb_display(PROGRAMMER *pgm, const char *p)
{
  /* MAYBE */
}

static void serbb_enable(PROGRAMMER *pgm)
{
  /* nothing */
}

static void serbb_disable(PROGRAMMER *pgm)
{
  /* nothing */
}

static void serbb_powerup(PROGRAMMER *pgm)
{
  /* nothing */
}

static void serbb_powerdown(PROGRAMMER *pgm)
{
  /* nothing */
}

static int serbb_open(PROGRAMMER *pgm, char *port)
{
  struct termios mode;
  int flags;
  int r;

  if (bitbang_check_prerequisites(pgm) < 0)
    return -1;

  /* adapted from uisp code */

  pgm->fd.ifd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);

  if (pgm->fd.ifd < 0) {
    perror(port);
    return(-1);
  }

  r = tcgetattr(pgm->fd.ifd, &mode);
  if (r < 0) {
    avrdude_message(MSG_INFO, "%s: ", port);
    perror("tcgetattr");
    return(-1);
  }
  oldmode = mode;

  mode.c_iflag = IGNBRK | IGNPAR;
  mode.c_oflag = 0;
  mode.c_cflag = CLOCAL | CREAD | CS8 | B9600;
  mode.c_cc [VMIN] = 1;
  mode.c_cc [VTIME] = 0;

  r = tcsetattr(pgm->fd.ifd, TCSANOW, &mode);
  if (r < 0) {
      avrdude_message(MSG_INFO, "%s: ", port);
      perror("tcsetattr");
      return(-1);
  }

  /* Clear O_NONBLOCK flag.  */
  flags = fcntl(pgm->fd.ifd, F_GETFL, 0);
  if (flags == -1)
    {
      avrdude_message(MSG_INFO, "%s: Can not get flags: %s\n",
	      progname, strerror(errno));
      return(-1);
    }
  flags &= ~O_NONBLOCK;
  if (fcntl(pgm->fd.ifd, F_SETFL, flags) == -1)
    {
      avrdude_message(MSG_INFO, "%s: Can not clear nonblock flag: %s\n",
	      progname, strerror(errno));
      return(-1);
    }

  return(0);
}

static void serbb_close(PROGRAMMER *pgm)
{
  if (pgm->fd.ifd != -1)
  {
	  (void)tcsetattr(pgm->fd.ifd, TCSANOW, &oldmode);
	  pgm->setpin(pgm, PIN_AVR_RESET, 1);
	  close(pgm->fd.ifd);
  }
  return;
}

const char serbb_desc[] = "Serial port bitbanging";

void serbb_initpgm(PROGRAMMER *pgm)
{
  strcpy(pgm->type, "SERBB");

  pgm_fill_old_pins(pgm); // TODO to be removed if old pin data no longer needed

  pgm->rdy_led        = bitbang_rdy_led;
  pgm->err_led        = bitbang_err_led;
  pgm->pgm_led        = bitbang_pgm_led;
  pgm->vfy_led        = bitbang_vfy_led;
  pgm->initialize     = bitbang_initialize;
  pgm->display        = serbb_display;
  pgm->enable         = serbb_enable;
  pgm->disable        = serbb_disable;
  pgm->powerup        = serbb_powerup;
  pgm->powerdown      = serbb_powerdown;
  pgm->program_enable = bitbang_program_enable;
  pgm->chip_erase     = bitbang_chip_erase;
  pgm->cmd            = bitbang_cmd;
  pgm->cmd_tpi        = bitbang_cmd_tpi;
  pgm->open           = serbb_open;
  pgm->close          = serbb_close;
  pgm->setpin         = serbb_setpin;
  pgm->getpin         = serbb_getpin;
  pgm->highpulsepin   = serbb_highpulsepin;
  pgm->read_byte      = avr_read_byte_default;
  pgm->write_byte     = avr_write_byte_default;
}

#endif  /* WIN32NATIVE */
