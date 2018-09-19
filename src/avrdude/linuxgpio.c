/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Support for bitbanging GPIO pins using the /sys/class/gpio interface
 * 
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

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "bitbang.h"

#if HAVE_LINUXGPIO

/*
 * GPIO user space helpers
 *
 * Copyright 2009 Analog Devices Inc.
 * Michael Hennerich (hennerich@blackfin.uclinux.org)
 *
 * Licensed under the GPL-2 or later
 */

/*
 * GPIO user space helpers
 * The following functions are acting on an "unsigned gpio" argument, which corresponds to the 
 * gpio numbering scheme in the kernel (starting from 0).  
 * The higher level functions use "int pin" to specify the pins with an offset of 1:
 * gpio = pin - 1;
 */

#define GPIO_DIR_IN	0
#define GPIO_DIR_OUT	1

static int linuxgpio_export(unsigned int gpio)
{
  int fd, len, r;
  char buf[11];

  fd = open("/sys/class/gpio/export", O_WRONLY);
  if (fd < 0) {
    perror("Can't open /sys/class/gpio/export");
    return fd;
  }

  len = snprintf(buf, sizeof(buf), "%u", gpio);
  r = write(fd, buf, len);
  close(fd);

  return r;
}

static int linuxgpio_unexport(unsigned int gpio)
{
  int fd, len, r;
  char buf[11];

  fd = open("/sys/class/gpio/unexport", O_WRONLY);
  if (fd < 0) {
    perror("Can't open /sys/class/gpio/unexport");
    return fd;
  }

  len = snprintf(buf, sizeof(buf), "%u", gpio);
  r = write(fd, buf, len);
  close(fd);

  return r;
}

static int linuxgpio_openfd(unsigned int gpio)
{
  char filepath[60];

  snprintf(filepath, sizeof(filepath), "/sys/class/gpio/gpio%u/value", gpio);
  return (open(filepath, O_RDWR));
}

static int linuxgpio_dir(unsigned int gpio, unsigned int dir)
{
  int fd, r;
  char buf[60];

  snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%u/direction", gpio);

  fd = open(buf, O_WRONLY);
  if (fd < 0) {
    perror("Can't open gpioX/direction");
    return fd;
  }

  if (dir == GPIO_DIR_OUT)
    r = write(fd, "out", 4);
  else
    r = write(fd, "in", 3);

  close(fd);

  return r;
}

static int linuxgpio_dir_out(unsigned int gpio)
{
  return linuxgpio_dir(gpio, GPIO_DIR_OUT);
}

static int linuxgpio_dir_in(unsigned int gpio)
{
  return linuxgpio_dir(gpio, GPIO_DIR_IN);
}

/*
 * End of GPIO user space helpers
 */

#define N_GPIO (PIN_MAX + 1)

/*
* an array which holds open FDs to /sys/class/gpio/gpioXX/value for all needed pins
*/
static int linuxgpio_fds[N_GPIO] ;


static int linuxgpio_setpin(PROGRAMMER * pgm, int pinfunc, int value)
{
  int r;
  int pin = pgm->pinno[pinfunc]; // TODO

  if (pin & PIN_INVERSE)
  {
    value  = !value;
    pin   &= PIN_MASK;
  }

  if ( linuxgpio_fds[pin] < 0 )
    return -1;

  if (value)
    r = write(linuxgpio_fds[pin], "1", 1);
  else
    r = write(linuxgpio_fds[pin], "0", 1);

  if (r!=1) return -1;

  if (pgm->ispdelay > 1)
    bitbang_delay(pgm->ispdelay);

  return 0;
}

static int linuxgpio_getpin(PROGRAMMER * pgm, int pinfunc)
{
  unsigned char invert=0;
  char c;
  int pin = pgm->pinno[pinfunc]; // TODO

  if (pin & PIN_INVERSE)
  {
    invert = 1;
    pin   &= PIN_MASK;
  }

  if ( linuxgpio_fds[pin] < 0 )
    return -1;

  if (lseek(linuxgpio_fds[pin], 0, SEEK_SET)<0)
    return -1;

  if (read(linuxgpio_fds[pin], &c, 1)!=1)
    return -1;

  if (c=='0')
    return 0+invert;
  else if (c=='1')
    return 1-invert;
  else
    return -1;

}

static int linuxgpio_highpulsepin(PROGRAMMER * pgm, int pinfunc)
{
  int pin = pgm->pinno[pinfunc]; // TODO
  
  if ( linuxgpio_fds[pin & PIN_MASK] < 0 )
    return -1;

  linuxgpio_setpin(pgm, pinfunc, 1);
  linuxgpio_setpin(pgm, pinfunc, 0);

  return 0;
}



static void linuxgpio_display(PROGRAMMER *pgm, const char *p)
{
    avrdude_message(MSG_INFO, "%sPin assignment  : /sys/class/gpio/gpio{n}\n",p);
    pgm_display_generic_mask(pgm, p, SHOW_AVR_PINS);
}

static void linuxgpio_enable(PROGRAMMER *pgm)
{
  /* nothing */
}

static void linuxgpio_disable(PROGRAMMER *pgm)
{
  /* nothing */
}

static void linuxgpio_powerup(PROGRAMMER *pgm)
{
  /* nothing */
}

static void linuxgpio_powerdown(PROGRAMMER *pgm)
{
  /* nothing */
}

static int linuxgpio_open(PROGRAMMER *pgm, char *port)
{
  int r, i, pin;

  if (bitbang_check_prerequisites(pgm) < 0)
    return -1;


  for (i=0; i<N_GPIO; i++)
    linuxgpio_fds[i] = -1;
  //Avrdude assumes that if a pin number is 0 it means not used/available
  //this causes a problem because 0 is a valid GPIO number in Linux sysfs.
  //To avoid annoying off by one pin numbering we assume SCK, MOSI, MISO 
  //and RESET pins are always defined in avrdude.conf, even as 0. If they're
  //not programming will not work anyway. The drawbacks of this approach are
  //that unwanted toggling of GPIO0 can occur and that other optional pins
  //mostry LED status, can't be set to GPIO0. It can be fixed when a better 
  //solution exists.
  for (i=0; i<N_PINS; i++) {
    if ( (pgm->pinno[i] & PIN_MASK) != 0 ||
         i == PIN_AVR_RESET ||
         i == PIN_AVR_SCK   ||
         i == PIN_AVR_MOSI  ||
         i == PIN_AVR_MISO ) {
        pin = pgm->pinno[i] & PIN_MASK;
        if ((r=linuxgpio_export(pin)) < 0) {
            avrdude_message(MSG_INFO, "Can't export GPIO %d, already exported/busy?: %s",
                    pin, strerror(errno));
            return r;
        }
        if (i == PIN_AVR_MISO)
            r=linuxgpio_dir_in(pin);
        else
            r=linuxgpio_dir_out(pin);

        if (r < 0)
            return r;

        if ((linuxgpio_fds[pin]=linuxgpio_openfd(pin)) < 0)
            return linuxgpio_fds[pin];
    }
  }

 return(0);
}

static void linuxgpio_close(PROGRAMMER *pgm)
{
  int i, reset_pin;

  reset_pin = pgm->pinno[PIN_AVR_RESET] & PIN_MASK;

  //first configure all pins as input, except RESET
  //this should avoid possible conflicts when AVR firmware starts
  for (i=0; i<N_GPIO; i++) {
    if (linuxgpio_fds[i] >= 0 && i != reset_pin) {
       close(linuxgpio_fds[i]);
       linuxgpio_dir_in(i);
       linuxgpio_unexport(i);
    }
  }
  //configure RESET as input, if there's external pull up it will go high
  if (linuxgpio_fds[reset_pin] >= 0) {
    close(linuxgpio_fds[reset_pin]);
    linuxgpio_dir_in(reset_pin);
    linuxgpio_unexport(reset_pin);
  }
}

void linuxgpio_initpgm(PROGRAMMER *pgm)
{
  strcpy(pgm->type, "linuxgpio");

  pgm_fill_old_pins(pgm); // TODO to be removed if old pin data no longer needed

  pgm->rdy_led        = bitbang_rdy_led;
  pgm->err_led        = bitbang_err_led;
  pgm->pgm_led        = bitbang_pgm_led;
  pgm->vfy_led        = bitbang_vfy_led;
  pgm->initialize     = bitbang_initialize;
  pgm->display        = linuxgpio_display;
  pgm->enable         = linuxgpio_enable;
  pgm->disable        = linuxgpio_disable;
  pgm->powerup        = linuxgpio_powerup;
  pgm->powerdown      = linuxgpio_powerdown;
  pgm->program_enable = bitbang_program_enable;
  pgm->chip_erase     = bitbang_chip_erase;
  pgm->cmd            = bitbang_cmd;
  pgm->open           = linuxgpio_open;
  pgm->close          = linuxgpio_close;
  pgm->setpin         = linuxgpio_setpin;
  pgm->getpin         = linuxgpio_getpin;
  pgm->highpulsepin   = linuxgpio_highpulsepin;
  pgm->read_byte      = avr_read_byte_default;
  pgm->write_byte     = avr_write_byte_default;
}

const char linuxgpio_desc[] = "GPIO bitbanging using the Linux sysfs interface";

#else  /* !HAVE_LINUXGPIO */

void linuxgpio_initpgm(PROGRAMMER * pgm)
{
  avrdude_message(MSG_INFO, "%s: Linux sysfs GPIO support not available in this configuration\n",
                  progname);
}

const char linuxgpio_desc[] = "GPIO bitbanging using the Linux sysfs interface (not available)";

#endif /* HAVE_LINUXGPIO */
