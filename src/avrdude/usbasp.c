/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2006  Thomas Fischl
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

/*
 * Interface to the USBasp programmer.
 *
 * See http://www.fischl.de/usbasp/
 */
#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "usbasp.h"
#include "usbdevs.h"

#if defined(HAVE_LIBUSB) || defined(HAVE_LIBUSB_1_0)

#ifdef HAVE_LIBUSB_1_0
# define USE_LIBUSB_1_0
#endif

#if defined(USE_LIBUSB_1_0)
# if defined(HAVE_LIBUSB_1_0_LIBUSB_H)
#  include <libusb-1.0/libusb.h>
# else
#  include <libusb.h>
# endif
#else
# if defined(HAVE_USB_H)
#  include <usb.h>
# elif defined(HAVE_LUSB0_USB_H)
#  include <lusb0_usb.h>
# else
#  error "libusb needs either <usb.h> or <lusb0_usb.h>"
# endif
#endif

#ifdef USE_LIBUSB_1_0

static libusb_context *ctx = NULL;

static int libusb_to_errno(int result)
{
	switch (result) {
	case LIBUSB_SUCCESS:
		return 0;
	case LIBUSB_ERROR_IO:
		return EIO;
	case LIBUSB_ERROR_INVALID_PARAM:
		return EINVAL;
	case LIBUSB_ERROR_ACCESS:
		return EACCES;
	case LIBUSB_ERROR_NO_DEVICE:
		return ENXIO;
	case LIBUSB_ERROR_NOT_FOUND:
		return ENOENT;
	case LIBUSB_ERROR_BUSY:
		return EBUSY;
#ifdef ETIMEDOUT
	case LIBUSB_ERROR_TIMEOUT:
		return ETIMEDOUT;
#endif
#ifdef EOVERFLOW
	case LIBUSB_ERROR_OVERFLOW:
		return EOVERFLOW;
#endif
	case LIBUSB_ERROR_PIPE:
		return EPIPE;
	case LIBUSB_ERROR_INTERRUPTED:
		return EINTR;
	case LIBUSB_ERROR_NO_MEM:
		return ENOMEM;
	case LIBUSB_ERROR_NOT_SUPPORTED:
		return ENOSYS;
	default:
		return ERANGE;
	}
}

#endif


/*
 * Private data for this programmer.
 */
struct pdata
{
#ifdef USE_LIBUSB_1_0
  libusb_device_handle *usbhandle;
#else
  usb_dev_handle *usbhandle;
#endif
  int sckfreq_hz;
  unsigned int capabilities;
  int use_tpi;
};

#define PDATA(pgm) ((struct pdata *)(pgm->cookie))
#define IMPORT_PDATA(pgm) struct pdata *pdata = PDATA(pgm)



/* Prototypes */
// interface - management
static void usbasp_setup(PROGRAMMER * pgm);
static void usbasp_teardown(PROGRAMMER * pgm);
// internal functions
static int usbasp_transmit(PROGRAMMER * pgm, unsigned char receive,
			   unsigned char functionid, const unsigned char *send,
			   unsigned char *buffer, int buffersize);
#ifdef USE_LIBUSB_1_0
static int usbOpenDevice(libusb_device_handle **device, int vendor, char *vendorName, int product, char *productName);
#else
static int usbOpenDevice(usb_dev_handle **device, int vendor, char *vendorName, int product, char *productName);
#endif
// interface - prog.
static int usbasp_open(PROGRAMMER * pgm, char * port);
static void usbasp_close(PROGRAMMER * pgm);
// dummy functions
static void usbasp_disable(PROGRAMMER * pgm);
static void usbasp_enable(PROGRAMMER * pgm);
static void usbasp_display(PROGRAMMER * pgm, const char * p);
// universal functions
static int usbasp_initialize(PROGRAMMER * pgm, AVRPART * p);
// SPI specific functions
static int usbasp_spi_cmd(PROGRAMMER * pgm, const unsigned char *cmd, unsigned char *res);
static int usbasp_spi_program_enable(PROGRAMMER * pgm, AVRPART * p);
static int usbasp_spi_chip_erase(PROGRAMMER * pgm, AVRPART * p);
static int usbasp_spi_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                 unsigned int page_size,
                                 unsigned int addr, unsigned int n_bytes);
static int usbasp_spi_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned int page_size,
                                  unsigned int addr, unsigned int n_bytes);
static int usbasp_spi_set_sck_period(PROGRAMMER *pgm, double sckperiod);
// TPI specific functions
static void usbasp_tpi_send_byte(PROGRAMMER * pgm, uint8_t b);
static int usbasp_tpi_cmd(PROGRAMMER * pgm, const unsigned char *cmd, unsigned char *res);
static int usbasp_tpi_program_enable(PROGRAMMER * pgm, AVRPART * p);
static int usbasp_tpi_chip_erase(PROGRAMMER * pgm, AVRPART * p);
static int usbasp_tpi_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                 unsigned int page_size,
                                 unsigned int addr, unsigned int n_bytes);
static int usbasp_tpi_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned int page_size,
                                  unsigned int addr, unsigned int n_bytes);
static int usbasp_tpi_set_sck_period(PROGRAMMER *pgm, double sckperiod);
static int usbasp_tpi_read_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, unsigned long addr, unsigned char * value);
static int usbasp_tpi_write_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, unsigned long addr, unsigned char data);


/* Interface - management */
static void usbasp_setup(PROGRAMMER * pgm)
{
  if ((pgm->cookie = malloc(sizeof(struct pdata))) == 0) {
    avrdude_message(MSG_INFO, "%s: usbasp_setup(): Out of memory allocating private data\n",
                    progname);
    exit(1);
  }
  memset(pgm->cookie, 0, sizeof(struct pdata));
}

static void usbasp_teardown(PROGRAMMER * pgm)
{
  free(pgm->cookie);
}

/* Internal functions */

static const char *usbasp_get_funcname(unsigned char functionid)
{
  switch (functionid) {
  case USBASP_FUNC_CONNECT:         return "USBASP_FUNC_CONNECT";         break;
  case USBASP_FUNC_DISCONNECT:      return "USBASP_FUNC_DISCONNECT";      break;
  case USBASP_FUNC_TRANSMIT:        return "USBASP_FUNC_TRANSMIT";        break;
  case USBASP_FUNC_READFLASH:       return "USBASP_FUNC_READFLASH";       break;
  case USBASP_FUNC_ENABLEPROG:      return "USBASP_FUNC_ENABLEPROG";      break;
  case USBASP_FUNC_WRITEFLASH:      return "USBASP_FUNC_WRITEFLASH";      break;
  case USBASP_FUNC_READEEPROM:      return "USBASP_FUNC_READEEPROM";      break;
  case USBASP_FUNC_WRITEEEPROM:     return "USBASP_FUNC_WRITEEEPROM";     break;
  case USBASP_FUNC_SETLONGADDRESS:  return "USBASP_FUNC_SETLONGADDRESS";  break;
  case USBASP_FUNC_SETISPSCK:       return "USBASP_FUNC_SETISPSCK";       break;
  case USBASP_FUNC_TPI_CONNECT:     return "USBASP_FUNC_TPI_CONNECT";     break;
  case USBASP_FUNC_TPI_DISCONNECT:  return "USBASP_FUNC_TPI_DISCONNECT";  break;
  case USBASP_FUNC_TPI_RAWREAD:     return "USBASP_FUNC_TPI_RAWREAD";     break;
  case USBASP_FUNC_TPI_RAWWRITE:    return "USBASP_FUNC_TPI_RAWWRITE";    break;
  case USBASP_FUNC_TPI_READBLOCK:   return "USBASP_FUNC_TPI_READBLOCK";   break;
  case USBASP_FUNC_TPI_WRITEBLOCK:  return "USBASP_FUNC_TPI_WRITEBLOCK";  break;
  case USBASP_FUNC_GETCAPABILITIES: return "USBASP_FUNC_GETCAPABILITIES"; break;
  default:                          return "Unknown USBASP function";     break;
  }
}

/*
 * wrapper for usb_control_msg call
 */
static int usbasp_transmit(PROGRAMMER * pgm,
			   unsigned char receive, unsigned char functionid,
			   const unsigned char *send,
			   unsigned char *buffer, int buffersize)
{
  int nbytes;

  if (verbose > 3) {
    avrdude_message(MSG_TRACE, "%s: usbasp_transmit(\"%s\", 0x%02x, 0x%02x, 0x%02x, 0x%02x)\n",
                    progname,
                    usbasp_get_funcname(functionid), send[0], send[1], send[2], send[3]);
    if (!receive && buffersize > 0) {
      int i;
      avrdude_message(MSG_TRACE, "%s => ", progbuf);
      for (i = 0; i < buffersize; i++)
	avrdude_message(MSG_TRACE, "[%02x] ", buffer[i]);
      avrdude_message(MSG_TRACE, "\n");
    }
  }

#ifdef USE_LIBUSB_1_0
  nbytes = libusb_control_transfer(PDATA(pgm)->usbhandle,
				   (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | (receive << 7)) & 0xff,
				   functionid & 0xff, 
				   ((send[1] << 8) | send[0]) & 0xffff, 
				   ((send[3] << 8) | send[2]) & 0xffff, 
				   buffer, 
				   buffersize & 0xffff,
				   5000);
  if(nbytes < 0){
    avrdude_message(MSG_INFO, "%s: error: usbasp_transmit: %s\n", progname, strerror(libusb_to_errno(nbytes)));
    return -1;
  }
#else
  nbytes = usb_control_msg(PDATA(pgm)->usbhandle,
			   USB_TYPE_VENDOR | USB_RECIP_DEVICE | (receive << 7),
			   functionid,
			   (send[1] << 8) | send[0],
			   (send[3] << 8) | send[2],
			   (char *)buffer, buffersize,
			   5000);
  if(nbytes < 0){
    avrdude_message(MSG_INFO, "%s: error: usbasp_transmit: %s\n", progname, usb_strerror());
    return -1;
  }
#endif

  if (verbose > 3 && receive && nbytes > 0) {
    int i;
    avrdude_message(MSG_TRACE, "%s<= ", progbuf);
    for (i = 0; i < nbytes; i++)
      avrdude_message(MSG_TRACE, "[%02x] ", buffer[i]);
    avrdude_message(MSG_TRACE, "\n");
  }

  return nbytes;
}


/*
 * Try to open USB device with given VID, PID, vendor and product name
 * Parts of this function were taken from an example code by OBJECTIVE
 * DEVELOPMENT Software GmbH (www.obdev.at) to meet conditions for
 * shared VID/PID
 */
#ifdef USE_LIBUSB_1_0
static int usbOpenDevice(libusb_device_handle **device, int vendor,
			 char *vendorName, int product, char *productName)
{
    libusb_device_handle *handle = NULL;
    int                  errorCode = USB_ERROR_NOTFOUND;
    static int           didUsbInit = 0;
    int j;
    int r;

    if(!didUsbInit){
        didUsbInit = 1;
        libusb_init(&ctx);
    }
    
    libusb_device **dev_list;
    int dev_list_len = libusb_get_device_list(ctx, &dev_list);

    for (j=0; j<dev_list_len; ++j) {
        libusb_device *dev = dev_list[j];
        struct libusb_device_descriptor descriptor;
	libusb_get_device_descriptor(dev, &descriptor);
	if (descriptor.idVendor == vendor && descriptor.idProduct == product) {
            char    string[256];
	    /* we need to open the device in order to query strings */
            r = libusb_open(dev, &handle);
            if (!handle) {
                 errorCode = USB_ERROR_ACCESS;
                 avrdude_message(MSG_INFO, "%s: Warning: cannot open USB device: %s\n",
                                 progname, strerror(libusb_to_errno(r)));
                    continue;
            }
            errorCode = 0;
            /* now check whether the names match: */
            /* if vendorName not given ignore it (any vendor matches) */
	    r = libusb_get_string_descriptor_ascii(handle, descriptor.iManufacturer & 0xff, (unsigned char*)string, sizeof(string));
            if (r < 0) {
                if ((vendorName != NULL) && (vendorName[0] != 0)) {
                    errorCode = USB_ERROR_IO;
                    avrdude_message(MSG_INFO, "%s: Warning: cannot query manufacturer for device: %s\n",
                                    progname, strerror(libusb_to_errno(r)));
		}
            } else {
                avrdude_message(MSG_NOTICE2, "%s: seen device from vendor ->%s<-\n",
                                    progname, string);
                if ((vendorName != NULL) && (vendorName[0] != 0) && (strcmp(string, vendorName) != 0))
                    errorCode = USB_ERROR_NOTFOUND;
            }
            /* if productName not given ignore it (any product matches) */
	    r = libusb_get_string_descriptor_ascii(handle, descriptor.iProduct & 0xff, (unsigned char*)string, sizeof(string));
            if (r < 0) {
                if ((productName != NULL) && (productName[0] != 0)) {
                    errorCode = USB_ERROR_IO;
                    avrdude_message(MSG_INFO, "%s: Warning: cannot query product for device: %s\n",
                                    progname, strerror(libusb_to_errno(r)));
		}
            } else {
                avrdude_message(MSG_NOTICE2, "%s: seen product ->%s<-\n",
                                    progname, string);
                if((productName != NULL) && (productName[0] != 0) && (strcmp(string, productName) != 0))
                    errorCode = USB_ERROR_NOTFOUND;
            }
            if (errorCode == 0)
                break;
            libusb_close(handle);
            handle = NULL;
        }
    }
    libusb_free_device_list(dev_list,1);
    if (handle != NULL){
        errorCode = 0;
        *device = handle;
    }
    return errorCode;
}
#else
static int usbOpenDevice(usb_dev_handle **device, int vendor,
			 char *vendorName, int product, char *productName)
{
struct usb_bus       *bus;
struct usb_device    *dev;
usb_dev_handle       *handle = NULL;
int                  errorCode = USB_ERROR_NOTFOUND;
static int           didUsbInit = 0;

    if(!didUsbInit){
        didUsbInit = 1;
        usb_init();
    }
    usb_find_busses();
    usb_find_devices();
    for(bus=usb_get_busses(); bus; bus=bus->next){
        for(dev=bus->devices; dev; dev=dev->next){
            if(dev->descriptor.idVendor == vendor &&
	       dev->descriptor.idProduct == product){
                char    string[256];
                int     len;
		/* we need to open the device in order to query strings */
                handle = usb_open(dev);
                if(!handle){
                    errorCode = USB_ERROR_ACCESS;
                    avrdude_message(MSG_INFO, "%s: Warning: cannot open USB device: %s\n",
                                    progname, usb_strerror());
                    continue;
                }
                errorCode = 0;
                /* now check whether the names match: */
                /* if vendorName not given ignore it (any vendor matches) */
                len = usb_get_string_simple(handle, dev->descriptor.iManufacturer,
					    string, sizeof(string));
                if(len < 0){
                    if ((vendorName != NULL) && (vendorName[0] != 0)) {
                    errorCode = USB_ERROR_IO;
                    avrdude_message(MSG_INFO, "%s: Warning: cannot query manufacturer for device: %s\n",
                                    progname, usb_strerror());
		    }
                } else {
                    avrdude_message(MSG_NOTICE2, "%s: seen device from vendor ->%s<-\n",
                                        progname, string);
                    if((vendorName != NULL) && (vendorName[0] != 0) && (strcmp(string, vendorName) != 0))
                        errorCode = USB_ERROR_NOTFOUND;
                }
                /* if productName not given ignore it (any product matches) */
                len = usb_get_string_simple(handle, dev->descriptor.iProduct,
					    string, sizeof(string));
                if(len < 0){
                    if ((productName != NULL) && (productName[0] != 0)) {
                        errorCode = USB_ERROR_IO;
                        avrdude_message(MSG_INFO, "%s: Warning: cannot query product for device: %s\n",
                                        progname, usb_strerror());
		    }
                } else {
                    avrdude_message(MSG_NOTICE2, "%s: seen product ->%s<-\n",
                                        progname, string);
                    if((productName != NULL) && (productName[0] != 0) && (strcmp(string, productName) != 0))
                        errorCode = USB_ERROR_NOTFOUND;
                }
                if (errorCode == 0)
                    break;
                usb_close(handle);
                handle = NULL;
            }
        }
        if(handle)
            break;
    }
    if(handle != NULL){
        errorCode = 0;
        *device = handle;
    }
    return errorCode;
}
#endif


/* Interface - prog. */
static int usbasp_open(PROGRAMMER * pgm, char * port)
{
  avrdude_message(MSG_DEBUG, "%s: usbasp_open(\"%s\")\n",
	    progname, port);

  /* usb_init will be done in usbOpenDevice */
  LNODEID usbpid = lfirst(pgm->usbpid);
  int pid, vid;
  if (usbpid) {
    pid = *(int *)(ldata(usbpid));
    if (lnext(usbpid))
      avrdude_message(MSG_INFO, "%s: Warning: using PID 0x%04x, ignoring remaining PIDs in list\n",
                      progname, pid);
  } else {
    pid = USBASP_SHARED_PID;
  }
  vid = pgm->usbvid? pgm->usbvid: USBASP_SHARED_VID;
  if (usbOpenDevice(&PDATA(pgm)->usbhandle, vid, pgm->usbvendor, pid, pgm->usbproduct) != 0) {
    /* try alternatives */
    if(strcasecmp(ldata(lfirst(pgm->id)), "usbasp") == 0) {
    /* for id usbasp autodetect some variants */
      if(strcasecmp(port, "nibobee") == 0) {
        avrdude_message(MSG_INFO, "%s: warning: Using \"-C usbasp -P nibobee\" is deprecated,"
	        "use \"-C nibobee\" instead.\n",
	        progname);
        if (usbOpenDevice(&PDATA(pgm)->usbhandle, USBASP_NIBOBEE_VID, "www.nicai-systems.com",
		        USBASP_NIBOBEE_PID, "NIBObee") != 0) {
          avrdude_message(MSG_INFO, "%s: error: could not find USB device "
                          "\"NIBObee\" with vid=0x%x pid=0x%x\n",
                          progname, USBASP_NIBOBEE_VID, USBASP_NIBOBEE_PID);
          return -1;
        }
        return 0;
      }
      /* check if device with old VID/PID is available */
      if (usbOpenDevice(&PDATA(pgm)->usbhandle, USBASP_OLD_VID, "www.fischl.de",
		             USBASP_OLD_PID, "USBasp") == 0) {
        /* found USBasp with old IDs */
        avrdude_message(MSG_INFO, "%s: Warning: Found USB device \"USBasp\" with "
                        "old VID/PID! Please update firmware of USBasp!\n",
                        progname);
	return 0;
      }
    /* original USBasp is specified in config file, so no need to check it again here */
    /* no alternative found => fall through to generic error message */
    }

    avrdude_message(MSG_INFO, "%s: error: could not find USB device with vid=0x%x pid=0x%x",
                    progname, vid, pid);
    if (pgm->usbvendor[0] != 0) {
       avrdude_message(MSG_INFO, " vendor='%s'", pgm->usbvendor);
    }
    if (pgm->usbproduct[0] != 0) {
       avrdude_message(MSG_INFO, " product='%s'", pgm->usbproduct);
    }
    avrdude_message(MSG_INFO, "\n");
    return -1;
  }

  return 0;
}

static void usbasp_close(PROGRAMMER * pgm)
{
  avrdude_message(MSG_DEBUG, "%s: usbasp_close()\n", progname);

  if (PDATA(pgm)->usbhandle!=NULL) {
    unsigned char temp[4];
    memset(temp, 0, sizeof(temp));

    if (PDATA(pgm)->use_tpi) {
        usbasp_transmit(pgm, 1, USBASP_FUNC_TPI_DISCONNECT, temp, temp, sizeof(temp));
    } else {
        usbasp_transmit(pgm, 1, USBASP_FUNC_DISCONNECT, temp, temp, sizeof(temp));
    }

#ifdef USE_LIBUSB_1_0
    libusb_close(PDATA(pgm)->usbhandle);
#else
    usb_close(PDATA(pgm)->usbhandle);
#endif
  }
#ifdef USE_LIBUSB_1_0
  libusb_exit(ctx);
#else
  /* nothing for usb 0.1 ? */
#endif
}


/* Dummy functions */
static void usbasp_disable(PROGRAMMER * pgm)
{
  /* Do nothing. */

  return;
}

static void usbasp_enable(PROGRAMMER * pgm)
{
  /* Do nothing. */

  return;
}

static void usbasp_display(PROGRAMMER * pgm, const char * p)
{
  return;
}


/* Universal functions: for both SPI and TPI */
static int usbasp_initialize(PROGRAMMER * pgm, AVRPART * p)
{
  unsigned char temp[4];
  unsigned char res[4];
  IMPORT_PDATA(pgm);

  avrdude_message(MSG_DEBUG, "%s: usbasp_initialize()\n", progname);

  /* get capabilities */
  memset(temp, 0, sizeof(temp));
  if(usbasp_transmit(pgm, 1, USBASP_FUNC_GETCAPABILITIES, temp, res, sizeof(res)) == 4)
    pdata->capabilities = res[0] | ((unsigned int)res[1] << 8) | ((unsigned int)res[2] << 16) | ((unsigned int)res[3] << 24);
  else
    pdata->capabilities = 0;

  pdata->use_tpi = ((pdata->capabilities & USBASP_CAP_TPI) != 0 && (p->flags & AVRPART_HAS_TPI) != 0) ? 1 : 0;

  if(pdata->use_tpi)
  {
    /* calc tpiclk delay */
    int dly = 1500000.0 * pgm->bitclock;
    if(dly < 1)
        dly = 1;
    else if(dly > 2047)
        dly = 2047;
    temp[0] = dly;
    temp[1] = dly >> 8;

    /* connect */
    usbasp_transmit(pgm, 1, USBASP_FUNC_TPI_CONNECT, temp, res, sizeof(res));
    
    /* change interface */
    pgm->program_enable = usbasp_tpi_program_enable;
    pgm->chip_erase     = usbasp_tpi_chip_erase;
    pgm->cmd            = usbasp_tpi_cmd;
    pgm->read_byte      = usbasp_tpi_read_byte;
    pgm->write_byte     = usbasp_tpi_write_byte;
    pgm->paged_write    = usbasp_tpi_paged_write;
    pgm->paged_load     = usbasp_tpi_paged_load;
    pgm->set_sck_period	= usbasp_tpi_set_sck_period;
  }
  else
  {
    /* set sck period */
    pgm->set_sck_period(pgm, pgm->bitclock);

    /* connect to target device */
    usbasp_transmit(pgm, 1, USBASP_FUNC_CONNECT, temp, res, sizeof(res));

    /* change interface */
    pgm->program_enable = usbasp_spi_program_enable;
    pgm->chip_erase     = usbasp_spi_chip_erase;
    pgm->cmd            = usbasp_spi_cmd;
    pgm->read_byte      = avr_read_byte_default;
    pgm->write_byte     = avr_write_byte_default;
    pgm->paged_write    = usbasp_spi_paged_write;
    pgm->paged_load     = usbasp_spi_paged_load;
    pgm->set_sck_period	= usbasp_spi_set_sck_period;
  }

  /* wait, so device is ready to receive commands */
  usleep(100000);

  return pgm->program_enable(pgm, p);
}

/* SPI specific functions */
static int usbasp_spi_cmd(PROGRAMMER * pgm, const unsigned char *cmd,
                   unsigned char *res)
{
  avrdude_message(MSG_DEBUG, "%s: usbasp_spi_cmd(0x%02x, 0x%02x, 0x%02x, 0x%02x)%s",
	    progname, cmd[0], cmd[1], cmd[2], cmd[3],
	    verbose > 3? "...\n": "");

  int nbytes =
    usbasp_transmit(pgm, 1, USBASP_FUNC_TRANSMIT, cmd, res, 4);

  if(nbytes != 4){
    if (verbose == 3)
      putc('\n', stderr);

    avrdude_message(MSG_INFO, "%s: error: wrong responds size\n",
	    progname);
    return -1;
  }
  avrdude_message(MSG_TRACE, "%s: usbasp_spi_cmd()", progname);
  avrdude_message(MSG_DEBUG, " => 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
        res[0], res[1], res[2], res[3]);

  return 0;
}

static int usbasp_spi_program_enable(PROGRAMMER * pgm, AVRPART * p)
{
  unsigned char res[4];
  unsigned char cmd[4];
  memset(cmd, 0, sizeof(cmd));
  memset(res, 0, sizeof(res));

  cmd[0] = 0;

  avrdude_message(MSG_DEBUG, "%s: usbasp_program_enable()\n",
	    progname);

  int nbytes =
    usbasp_transmit(pgm, 1, USBASP_FUNC_ENABLEPROG, cmd, res, sizeof(res));

  if ((nbytes != 1) | (res[0] != 0)) {
    avrdude_message(MSG_INFO, "%s: error: program enable: target doesn't answer. %x \n",
	    progname, res[0]);
    return -1;
  }

  return 0;
}

static int usbasp_spi_chip_erase(PROGRAMMER * pgm, AVRPART * p)
{
  unsigned char cmd[4];
  unsigned char res[4];

  avrdude_message(MSG_DEBUG, "%s: usbasp_chip_erase()\n",
	    progname);

  if (p->op[AVR_OP_CHIP_ERASE] == NULL) {
    avrdude_message(MSG_INFO, "chip erase instruction not defined for part \"%s\"\n",
            p->desc);
    return -1;
  }

  memset(cmd, 0, sizeof(cmd));

  avr_set_bits(p->op[AVR_OP_CHIP_ERASE], cmd);
  pgm->cmd(pgm, cmd, res);
  usleep(p->chip_erase_delay);
  pgm->initialize(pgm, p);

  return 0;
}

static int usbasp_spi_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                 unsigned int page_size,
                                 unsigned int address, unsigned int n_bytes)
{
  int n;
  unsigned char cmd[4];
  int wbytes = n_bytes;
  int blocksize;
  unsigned char *buffer = m->buf + address;
  int function;

  avrdude_message(MSG_DEBUG, "%s: usbasp_program_paged_load(\"%s\", 0x%x, %d)\n",
                    progname, m->desc, address, n_bytes);

  if (strcmp(m->desc, "flash") == 0) {
    function = USBASP_FUNC_READFLASH;
  } else if (strcmp(m->desc, "eeprom") == 0) {
    function = USBASP_FUNC_READEEPROM;
  } else {
    return -2;
  }

  /* set blocksize depending on sck frequency */  
  if ((PDATA(pgm)->sckfreq_hz > 0) && (PDATA(pgm)->sckfreq_hz < 10000)) {
     blocksize = USBASP_READBLOCKSIZE / 10;
  } else {
     blocksize = USBASP_READBLOCKSIZE;
  }

  while (wbytes) {
    if (wbytes <= blocksize) {
      blocksize = wbytes;
    }
    wbytes -= blocksize;

    /* set address (new mode) - if firmware on usbasp support newmode, then they use address from this command */
    unsigned char temp[4];
    memset(temp, 0, sizeof(temp));
    cmd[0] = address & 0xFF;
    cmd[1] = address >> 8;
    cmd[2] = address >> 16;
    cmd[3] = address >> 24;
    usbasp_transmit(pgm, 1, USBASP_FUNC_SETLONGADDRESS, cmd, temp, sizeof(temp));

    /* send command with address (compatibility mode) - if firmware on
	  usbasp doesn't support newmode, then they use address from this */
    cmd[0] = address & 0xFF;
    cmd[1] = address >> 8;
    // for compatibility - previous version of usbasp.c doesn't initialize this fields (firmware ignore it)
    cmd[2] = 0;
    cmd[3] = 0;

    n = usbasp_transmit(pgm, 1, function, cmd, buffer, blocksize);

    if (n != blocksize) {
      avrdude_message(MSG_INFO, "%s: error: wrong reading bytes %x\n",
	      progname, n);
      return -3;
    }

    buffer += blocksize;
    address += blocksize;
  }

  return n_bytes;
}

static int usbasp_spi_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned int page_size,
                                  unsigned int address, unsigned int n_bytes)
{
  int n;
  unsigned char cmd[4];
  int wbytes = n_bytes;
  int blocksize;
  unsigned char *buffer = m->buf + address;
  unsigned char blockflags = USBASP_BLOCKFLAG_FIRST;
  int function;

  avrdude_message(MSG_DEBUG, "%s: usbasp_program_paged_write(\"%s\", 0x%x, %d)\n",
                    progname, m->desc, address, n_bytes);

  if (strcmp(m->desc, "flash") == 0) {
    function = USBASP_FUNC_WRITEFLASH;
  } else if (strcmp(m->desc, "eeprom") == 0) {
    function = USBASP_FUNC_WRITEEEPROM;
  } else {
    return -2;
  }

  /* set blocksize depending on sck frequency */  
  if ((PDATA(pgm)->sckfreq_hz > 0) && (PDATA(pgm)->sckfreq_hz < 10000)) {
     blocksize = USBASP_WRITEBLOCKSIZE / 10;
  } else {
     blocksize = USBASP_WRITEBLOCKSIZE;
  }

  while (wbytes) {

    if (wbytes <= blocksize) {
      blocksize = wbytes;
    }
    wbytes -= blocksize;


    /* set address (new mode) - if firmware on usbasp support newmode, then
      they use address from this command */
    unsigned char temp[4];
    memset(temp, 0, sizeof(temp));
    cmd[0] = address & 0xFF;
    cmd[1] = address >> 8;
    cmd[2] = address >> 16;
    cmd[3] = address >> 24;
    usbasp_transmit(pgm, 1, USBASP_FUNC_SETLONGADDRESS, cmd, temp, sizeof(temp));

    /* normal command - firmware what support newmode - use address from previous command,
      firmware what doesn't support newmode - ignore previous command and use address from this command */

    cmd[0] = address & 0xFF;
    cmd[1] = address >> 8;
    cmd[2] = page_size & 0xFF;
    cmd[3] = (blockflags & 0x0F) + ((page_size & 0xF00) >> 4); //TP: Mega128 fix
    blockflags = 0;

    n = usbasp_transmit(pgm, 0, function, cmd, buffer, blocksize);

    if (n != blocksize) {
      avrdude_message(MSG_INFO, "%s: error: wrong count at writing %x\n",
	      progname, n);
      return -3;        
    }


    buffer += blocksize;
    address += blocksize;
  }

  return n_bytes;
}

/* The list of SCK frequencies in Hz supported by USBasp */
static struct sckoptions_t usbaspSCKoptions[] = {
  { USBASP_ISP_SCK_1500, 1500000 },
  { USBASP_ISP_SCK_750, 750000 },
  { USBASP_ISP_SCK_375, 375000 },
  { USBASP_ISP_SCK_187_5, 187500 },
  { USBASP_ISP_SCK_93_75, 93750 },
  { USBASP_ISP_SCK_32, 32000 },
  { USBASP_ISP_SCK_16, 16000 },
  { USBASP_ISP_SCK_8, 8000 },
  { USBASP_ISP_SCK_4, 4000 },
  { USBASP_ISP_SCK_2, 2000 },
  { USBASP_ISP_SCK_1, 1000 },
  { USBASP_ISP_SCK_0_5, 500 }
};

/*
 * Set sck period (in seconds)
 * Find next possible sck period and write it to the programmer.
 */
static int usbasp_spi_set_sck_period(PROGRAMMER *pgm, double sckperiod)
{
  char clockoption = USBASP_ISP_SCK_AUTO;
  unsigned char res[4];
  unsigned char cmd[4];

  avrdude_message(MSG_DEBUG, "%s: usbasp_spi_set_sck_period(%g)\n",
                    progname, sckperiod);

  memset(cmd, 0, sizeof(cmd));
  memset(res, 0, sizeof(res));

  /* reset global sck frequency to auto */
  PDATA(pgm)->sckfreq_hz = 0;

  if (sckperiod == 0) {
    /* auto sck set */

    avrdude_message(MSG_NOTICE, "%s: auto set sck period (because given equals null)\n", progname);

  } else {

    int sckfreq = 1 / sckperiod; /* sck in Hz */
    int usefreq = 0;

    avrdude_message(MSG_NOTICE2, "%s: try to set SCK period to %g s (= %i Hz)\n", progname, sckperiod, sckfreq);

    if (sckfreq >= usbaspSCKoptions[0].frequency) {
      clockoption = usbaspSCKoptions[0].id;
      usefreq = usbaspSCKoptions[0].frequency;
    } else {

      /* find clock option next to given clock */
      int i;
      for (i = 0; i < sizeof(usbaspSCKoptions) / sizeof(usbaspSCKoptions[0]); i++) {
        if (sckfreq >= usbaspSCKoptions[i].frequency - 1) { /* subtract 1 to compensate round errors */
          clockoption = usbaspSCKoptions[i].id;
          usefreq = usbaspSCKoptions[i].frequency;
          break;
        }
      }
    }

    /* save used sck frequency */
    PDATA(pgm)->sckfreq_hz = usefreq;

    avrdude_message(MSG_INFO, "%s: set SCK frequency to %i Hz\n", progname, usefreq);
  }

  cmd[0] = clockoption;

  int nbytes =
    usbasp_transmit(pgm, 1, USBASP_FUNC_SETISPSCK, cmd, res, sizeof(res));

  if ((nbytes != 1) | (res[0] != 0)) {
    avrdude_message(MSG_INFO, "%s: warning: cannot set sck period. please check for usbasp firmware update.\n",
      progname);
    return -1;
  }

  return 0;
}

/* TPI specific functions */
static void usbasp_tpi_send_byte(PROGRAMMER * pgm, uint8_t b)
{
  unsigned char temp[4];
  memset(temp, 0, sizeof(temp));

  temp[0] = b;

  usbasp_transmit(pgm, 1, USBASP_FUNC_TPI_RAWWRITE, temp, temp, sizeof(temp));
}


static int usbasp_tpi_recv_byte(PROGRAMMER * pgm)
{
  unsigned char temp[4];
  memset(temp, 0, sizeof(temp));

  if(usbasp_transmit(pgm, 1, USBASP_FUNC_TPI_RAWREAD, temp, temp, sizeof(temp)) != 1)
  {
    avrdude_message(MSG_INFO, "%s: error: wrong responds size\n", progname);
    return -1;
  }

  return temp[0];
}


static int usbasp_tpi_nvm_waitbusy(PROGRAMMER * pgm)
{
  int retry;

  avrdude_message(MSG_DEBUG, "%s: usbasp_tpi_nvm_waitbusy() ...", progname);

  for(retry=50; retry>0; retry--)
  {
    usbasp_tpi_send_byte(pgm, TPI_OP_SIN(NVMCSR));
    if(usbasp_tpi_recv_byte(pgm) & NVMCSR_BSY)
      continue;

    avrdude_message(MSG_DEBUG, " ready\n");

    return 0;
  }

  avrdude_message(MSG_DEBUG, " failure\n");

  return -1;
}

static int usbasp_tpi_cmd(PROGRAMMER * pgm, const unsigned char *cmd, unsigned char *res)
{
  avrdude_message(MSG_INFO, "%s: error: spi_cmd used in TPI mode: not allowed\n", progname);
  return -1;
}

static int usbasp_tpi_program_enable(PROGRAMMER * pgm, AVRPART * p)
{
  int retry;

  avrdude_message(MSG_DEBUG, "%s: usbasp_tpi_program_enable()\n", progname);

  /* change guard time */
  usbasp_tpi_send_byte(pgm, TPI_OP_SSTCS(TPIPCR));
  usbasp_tpi_send_byte(pgm, TPIPCR_GT_2b);

  /* send SKEY */
  usbasp_tpi_send_byte(pgm, 0xE0);
  usbasp_tpi_send_byte(pgm, 0xFF);
  usbasp_tpi_send_byte(pgm, 0x88);
  usbasp_tpi_send_byte(pgm, 0xD8);
  usbasp_tpi_send_byte(pgm, 0xCD);
  usbasp_tpi_send_byte(pgm, 0x45);
  usbasp_tpi_send_byte(pgm, 0xAB);
  usbasp_tpi_send_byte(pgm, 0x89);
  usbasp_tpi_send_byte(pgm, 0x12);

  /* check if device is ready */
  for(retry=0; retry<10; retry++)
  {
    usbasp_tpi_send_byte(pgm, TPI_OP_SLDCS(TPIIR));
    if(usbasp_tpi_recv_byte(pgm) != 0x80)
      continue;
    usbasp_tpi_send_byte(pgm, TPI_OP_SLDCS(TPISR));
    if((usbasp_tpi_recv_byte(pgm) & TPISR_NVMEN) == 0)
      continue;
    break;
  }
  if(retry >= 10)
  {
    avrdude_message(MSG_INFO, "%s: error: program enable: target doesn't answer.\n", progname);
    return -1;
  }

  return 0;
}

static int usbasp_tpi_chip_erase(PROGRAMMER * pgm, AVRPART * p)
{
  avrdude_message(MSG_DEBUG, "%s: usbasp_tpi_chip_erase()\n", progname);

  /* Set PR to flash */
  usbasp_tpi_send_byte(pgm, TPI_OP_SSTPR(0));
  usbasp_tpi_send_byte(pgm, 0x01);
  usbasp_tpi_send_byte(pgm, TPI_OP_SSTPR(1));
  usbasp_tpi_send_byte(pgm, 0x40);
  /* select ERASE */
  usbasp_tpi_send_byte(pgm, TPI_OP_SOUT(NVMCMD));
  usbasp_tpi_send_byte(pgm, NVMCMD_CHIP_ERASE);
  /* dummy write */
  usbasp_tpi_send_byte(pgm, TPI_OP_SST_INC);
  usbasp_tpi_send_byte(pgm, 0x00);
  usbasp_tpi_nvm_waitbusy(pgm);
  
  usleep(p->chip_erase_delay);
  pgm->initialize(pgm, p);

  return 0;
}

static int usbasp_tpi_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                 unsigned int page_size,
                                 unsigned int addr, unsigned int n_bytes)
{
  unsigned char cmd[4];
  unsigned char* dptr;
  int readed, clen, n;
  uint16_t pr;


  avrdude_message(MSG_DEBUG, "%s: usbasp_tpi_paged_load(\"%s\", 0x%0x, %d)\n",
	    progname, m->desc, addr, n_bytes);

  dptr = addr + m->buf;
  pr = addr + m->offset;
  readed = 0;

  while(readed < n_bytes)
  {
    clen = n_bytes - readed;
    if(clen > 32)
      clen = 32;

    /* prepare READBLOCK cmd */
    cmd[0] = pr & 0xFF;
    cmd[1] = pr >> 8;
    cmd[2] = 0;
    cmd[3] = 0;
    n = usbasp_transmit(pgm, 1, USBASP_FUNC_TPI_READBLOCK, cmd, dptr, clen);
    if(n != clen)
    {
      avrdude_message(MSG_INFO, "%s: error: wrong reading bytes %x\n", progname, n);
      return -3;
    }
    
    readed += clen;
    pr += clen;
    dptr += clen;
  }

  return n_bytes;
}

static int usbasp_tpi_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned int page_size,
                                  unsigned int addr, unsigned int n_bytes)
{
  unsigned char cmd[4];
  unsigned char* sptr;
  int writed, clen, n;
  uint16_t pr;


  avrdude_message(MSG_DEBUG, "%s: usbasp_tpi_paged_write(\"%s\", 0x%0x, %d)\n",
	    progname, m->desc, addr, n_bytes);

  sptr = addr + m->buf;
  pr = addr + m->offset;
  writed = 0;

  /* Set PR to flash */
  usbasp_tpi_send_byte(pgm, TPI_OP_SSTPR(0));
  usbasp_tpi_send_byte(pgm, (pr & 0xFF) | 1 );
  usbasp_tpi_send_byte(pgm, TPI_OP_SSTPR(1));
  usbasp_tpi_send_byte(pgm, (pr >> 8) );

  while(writed < n_bytes)
  {
    clen = n_bytes - writed;
    if(clen > 32)
      clen = 32;

    /* prepare WRITEBLOCK cmd */
    cmd[0] = pr & 0xFF;
    cmd[1] = pr >> 8;
    cmd[2] = 0;
    cmd[3] = 0;
    n = usbasp_transmit(pgm, 0, USBASP_FUNC_TPI_WRITEBLOCK, cmd, sptr, clen);
    if(n != clen)
    {
      avrdude_message(MSG_INFO, "%s: error: wrong count at writing %x\n", progname, n);
      return -3;
    }
    
    writed += clen;
    pr += clen;
    sptr += clen;
  }

  return n_bytes;
}

static int usbasp_tpi_set_sck_period(PROGRAMMER *pgm, double sckperiod)
{
  return 0;
}
static int usbasp_tpi_read_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, unsigned long addr, unsigned char * value)
{
  unsigned char cmd[4];
  int n;
  uint16_t pr;


  avrdude_message(MSG_DEBUG, "%s: usbasp_tpi_read_byte(\"%s\", 0x%0lx)\n",
	    progname, m->desc, addr);

  pr = m->offset + addr;

  /* READBLOCK */
  cmd[0] = pr & 0xFF;
  cmd[1] = pr >> 8;
  cmd[2] = 0;
  cmd[3] = 0;
  n = usbasp_transmit(pgm, 1, USBASP_FUNC_TPI_READBLOCK, cmd, value, 1);
  if(n != 1)
  {
    avrdude_message(MSG_INFO, "%s: error: wrong reading bytes %x\n", progname, n);
    return -3;
  }
  return 0;
}

static int usbasp_tpi_write_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m, unsigned long addr, unsigned char data)
{
  avrdude_message(MSG_INFO, "%s: error: usbasp_write_byte in TPI mode: all writes have to be done at page level\n", progname);
  return -1;
}


void usbasp_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "usbasp");

  /*
   * mandatory functions
   */

  pgm->initialize     = usbasp_initialize;
  pgm->display        = usbasp_display;
  pgm->enable         = usbasp_enable;
  pgm->disable        = usbasp_disable;
  pgm->program_enable = usbasp_spi_program_enable;
  pgm->chip_erase     = usbasp_spi_chip_erase;
  pgm->cmd            = usbasp_spi_cmd;
  pgm->open           = usbasp_open;
  pgm->close          = usbasp_close;
  pgm->read_byte      = avr_read_byte_default;
  pgm->write_byte     = avr_write_byte_default;

  /*
   * optional functions
   */

  pgm->paged_write    = usbasp_spi_paged_write;
  pgm->paged_load     = usbasp_spi_paged_load;
  pgm->setup          = usbasp_setup;
  pgm->teardown       = usbasp_teardown;
  pgm->set_sck_period = usbasp_spi_set_sck_period;

}


#else /* HAVE_LIBUSB */

static int usbasp_nousb_open (struct programmer_t *pgm, char * name)
{
  avrdude_message(MSG_INFO, "%s: error: no usb support. please compile again with libusb installed.\n",
	  progname);

  return -1;
}

void usbasp_initpgm(PROGRAMMER * pgm)
{
  strcpy(pgm->type, "usbasp");

  pgm->open           = usbasp_nousb_open;
}

#endif  /* HAVE_LIBUSB */

const char usbasp_desc[] = "USBasp programmer, see http://www.fischl.de/usbasp/";

