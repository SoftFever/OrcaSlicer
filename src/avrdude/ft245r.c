
/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2003-2004  Theodore A. Roth  <troth@openavr.org>
 * some code:
 * Copyright (C) 2011-2012 Roger E. Wolff <R.E.Wolff@BitWizard.nl>
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

/* ft245r -- FT245R/FT232R Synchronous BitBangMode Programmer
  default pin assign
               FT232R / FT245R
  miso  = 1;  # RxD   / D1
  sck   = 0;  # RTS   / D0
  mosi  = 2;  # TxD   / D2
  reset = 4;  # DTR   / D4
*/

/*
  The ft232r is very similar, or even "identical" in the synchronous
  bitbang mode that we use here.

  This allows boards that have an ft232r for communication and an avr
  as the processor to function as their own "ICSP". Boards that fit
  this description include the Arduino Duemilanove, Arduino Diecimila,
  Arduino NG (http://arduino.cc/it/main/boards) and the BitWizard
  ftdi_atmega board (http://www.bitwizard.nl/wiki/index.php/FTDI_ATmega)

  The Arduinos have to be patched to bring some of the control lines
  to the ICSP header. The BitWizard board already has the neccessary
  wiring on the PCB.

  How to add the wires to an arduino is documented here:
  http://www.geocities.jp/arduino_diecimila/bootloader/index_en.html
*/


#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "bitbang.h"
#include "ft245r.h"
#include "usbdevs.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(HAVE_LIBFTDI1) && defined(HAVE_LIBUSB_1_0)
# if defined(HAVE_LIBUSB_1_0_LIBUSB_H)
#  include <libusb-1.0/libusb.h>
# else
#  include <libusb.h>
# endif
# include <libftdi1/ftdi.h>
#elif defined(HAVE_LIBFTDI) && defined(HAVE_USB_H)
/* ftdi.h includes usb.h */
#include <ftdi.h>
#else 
#warning No libftdi or libusb support. Install libftdi1/libusb-1.0 or libftdi/libusb and run configure/make again.
#define DO_NOT_BUILD_FT245R
#endif

#ifndef HAVE_PTHREAD_H

static int ft245r_nopthread_open (struct programmer_t *pgm, char * name) {
    avrdude_message(MSG_INFO, "%s: error: no pthread support. Please compile again with pthread installed."
#if defined(_WIN32)
            " See http://sourceware.org/pthreads-win32/."
#endif
            "\n",
            progname);

    return -1;
}

void ft245r_initpgm(PROGRAMMER * pgm) {
    strcpy(pgm->type, "ftdi_syncbb");
    pgm->open = ft245r_nopthread_open;
}

#elif defined(DO_NOT_BUILD_FT245R)

static int ft245r_noftdi_open (struct programmer_t *pgm, char * name) {
    avrdude_message(MSG_INFO, "%s: error: no libftdi or libusb support. Install libftdi1/libusb-1.0 or libftdi/libusb and run configure/make again.\n",
                    progname);

    return -1;
}

void ft245r_initpgm(PROGRAMMER * pgm) {
    strcpy(pgm->type, "ftdi_syncbb");
    pgm->open = ft245r_noftdi_open;
}

#else

#include <pthread.h>

#ifdef __APPLE__
/* Mac OS X defines sem_init but actually does not implement them */
#include <dispatch/dispatch.h>

typedef dispatch_semaphore_t	sem_t;

#define sem_init(psem,x,val)	*psem = dispatch_semaphore_create(val)
#define sem_post(psem)		dispatch_semaphore_signal(*psem)
#define sem_wait(psem)		dispatch_semaphore_wait(*psem, DISPATCH_TIME_FOREVER)
#else
#include <semaphore.h>
#endif

#define FT245R_CYCLES	2
#define FT245R_FRAGMENT_SIZE  512
#define REQ_OUTSTANDINGS	10
//#define USE_INLINE_WRITE_PAGE

#define FT245R_DEBUG	0

static struct ftdi_context *handle;

static unsigned char ft245r_ddr;
static unsigned char ft245r_out;
static unsigned char ft245r_in;

#define BUFSIZE 0x2000

// libftdi / libftd2xx compatibility functions.

static pthread_t readerthread;
static sem_t buf_data, buf_space;
static unsigned char buffer[BUFSIZE];
static int head, tail;

static void add_to_buf (unsigned char c) {
    int nh;

    sem_wait (&buf_space);
    if (head == (BUFSIZE -1)) nh = 0;
    else                      nh = head + 1;

    if (nh == tail) {
        avrdude_message(MSG_INFO, "buffer overflow. Cannot happen!\n");
    }
    buffer[head] = c;
    head = nh;
    sem_post (&buf_data);
}

static void *reader (void *arg) {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL);
    struct ftdi_context *handle = (struct ftdi_context *)(arg);
    unsigned char buf[0x1000];
    int br, i;

    while (1) {
        pthread_testcancel();
        br = ftdi_read_data (handle, buf, sizeof(buf));
        for (i=0; i<br; i++)
            add_to_buf (buf[i]);
    }
    return NULL;
}

static int ft245r_send(PROGRAMMER * pgm, unsigned char * buf, size_t len) {
    int rv;

    rv = ftdi_write_data(handle, buf, len);
    if (len != rv) return -1;
    return 0;
}

static int ft245r_recv(PROGRAMMER * pgm, unsigned char * buf, size_t len) {
    int i;

    // Copy over data from the circular buffer..
    // XXX This should timeout, and return error if there isn't enough
    // data.
    for (i=0; i<len; i++) {
        sem_wait (&buf_data);
        buf[i] = buffer[tail];
        if (tail == (BUFSIZE -1)) tail = 0;
        else                      tail++;
        sem_post (&buf_space);
    }

    return 0;
}


static int ft245r_drain(PROGRAMMER * pgm, int display) {
    int r;
    unsigned char t;

    // flush the buffer in the chip by changing the mode.....
    r = ftdi_set_bitmode(handle, 0, BITMODE_RESET); 	// reset
    if (r) return -1;
    r = ftdi_set_bitmode(handle, ft245r_ddr, BITMODE_SYNCBB); // set Synchronuse BitBang
    if (r) return -1;

    // drain our buffer.
    while (head != tail) {
        ft245r_recv (pgm, &t, 1);
    }
    return 0;
}


static int ft245r_chip_erase(PROGRAMMER * pgm, AVRPART * p) {
    unsigned char cmd[4] = {0,0,0,0};
    unsigned char res[4];

    if (p->op[AVR_OP_CHIP_ERASE] == NULL) {
        avrdude_message(MSG_INFO, "chip erase instruction not defined for part \"%s\"\n",
                p->desc);
        return -1;
    }

    avr_set_bits(p->op[AVR_OP_CHIP_ERASE], cmd);
    pgm->cmd(pgm, cmd, res);
    usleep(p->chip_erase_delay);
    return pgm->initialize(pgm, p);
}


static int ft245r_set_bitclock(PROGRAMMER * pgm) {
    int r;
    int rate = 0;

    /* bitclock is second. 1us = 0.000001. Max rate for ft232r 750000 */
    if(pgm->bitclock) {
        rate = (uint32_t)(1.0/pgm->bitclock) * 2;
    } else if (pgm->baudrate) {
        rate = pgm->baudrate * 2;
    } else {
        rate = 150000; /* should work for all ftdi chips and the avr default internal clock of 1MHz */
    }

    if (FT245R_DEBUG) {
        avrdude_message(MSG_NOTICE2, " ft245r:  spi bitclk %d -> ft baudrate %d\n",
                rate / 2, rate);
    }
    r = ftdi_set_baudrate(handle, rate);
    if (r) {
        avrdude_message(MSG_INFO, "Set baudrate (%d) failed with error '%s'.\n",
                rate, ftdi_get_error_string (handle));
        return -1;
    }
    return 0;
}

static int set_pin(PROGRAMMER * pgm, int pinname, int val) {
    unsigned char buf[1];

    if (pgm->pin[pinname].mask[0] == 0) {
        // ignore not defined pins (might be the led or vcc or buff if not needed)
        return 0;
    }

    ft245r_out = SET_BITS_0(ft245r_out,pgm,pinname,val);
    buf[0] = ft245r_out;

    ft245r_send (pgm, buf, 1);
    ft245r_recv (pgm, buf, 1);

    ft245r_in = buf[0];
    return 0;
}

static int set_sck(PROGRAMMER * pgm, int value) {
    return set_pin(pgm, PIN_AVR_SCK, value);
}

static int set_reset(PROGRAMMER * pgm, int value) {
    return set_pin(pgm, PIN_AVR_RESET, value);
}

static int set_buff(PROGRAMMER * pgm, int value) {
    return set_pin(pgm, PPI_AVR_BUFF, value);
}

static int set_vcc(PROGRAMMER * pgm, int value) {
    return set_pin(pgm, PPI_AVR_VCC, value);
}

/* these functions are callbacks, which go into the
 * PROGRAMMER data structure ("optional functions")
 */
static int set_led_pgm(struct programmer_t * pgm, int value) {
    return set_pin(pgm, PIN_LED_PGM, value);
}

static int set_led_rdy(struct programmer_t * pgm, int value) {
    return set_pin(pgm, PIN_LED_RDY, value);
}

static int set_led_err(struct programmer_t * pgm, int value) {
    return set_pin(pgm, PIN_LED_ERR, value);
}

static int set_led_vfy(struct programmer_t * pgm, int value) {
    return set_pin(pgm, PIN_LED_VFY, value);
}

/*
 * apply power to the AVR processor
 */
static void ft245r_powerup(PROGRAMMER * pgm)
{
    set_vcc(pgm, ON); /* power up */
    usleep(100);
}


/*
 * remove power from the AVR processor
 */
static void ft245r_powerdown(PROGRAMMER * pgm)
{
    set_vcc(pgm, OFF); /* power down */
}


static void ft245r_disable(PROGRAMMER * pgm) {
    set_buff(pgm, OFF);
}


static void ft245r_enable(PROGRAMMER * pgm) {
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
    set_reset(pgm, OFF);
    usleep(1);
    set_buff(pgm, ON);
}

static int ft245r_cmd(PROGRAMMER * pgm, const unsigned char *cmd,
                      unsigned char *res);
/*
 * issue the 'program enable' command to the AVR device
 */
static int ft245r_program_enable(PROGRAMMER * pgm, AVRPART * p) {
    unsigned char cmd[4] = {0,0,0,0};
    unsigned char res[4];
    int i;

    if (p->op[AVR_OP_PGM_ENABLE] == NULL) {
        avrdude_message(MSG_INFO, "%s: AVR_OP_PGM_ENABLE command not defined for %s\n",
                        progname, p->desc);
        fflush(stderr);
        return -1;
    }

    avr_set_bits(p->op[AVR_OP_PGM_ENABLE], cmd);

    for(i = 0; i < 4; i++) {
        ft245r_cmd(pgm, cmd, res);

        if (res[p->pollindex-1] == p->pollvalue) return 0;

        if (FT245R_DEBUG) {
            avrdude_message(MSG_NOTICE, "%s: Program enable command not successful. Retrying.\n",
                            progname);
            fflush(stderr);
        }
        set_pin(pgm, PIN_AVR_RESET, ON);
        usleep(20);
        set_pin(pgm, PIN_AVR_RESET, OFF);

        if (i == 3) {
            ft245r_drain(pgm, 0);
            tail = head;
        }
    }

    avrdude_message(MSG_INFO, "%s: Device is not responding to program enable. Check connection.\n",
                    progname);
    fflush(stderr);

    return -1;
}

/*
 * initialize the AVR device and prepare it to accept commands
 */
static int ft245r_initialize(PROGRAMMER * pgm, AVRPART * p) {

    /* Apply power between VCC and GND while RESET and SCK are set to “0”. In some systems,
     * the programmer can not guarantee that SCK is held low during power-up. In this
     * case, RESET must be given a positive pulse of at least two CPU clock cycles duration
     * after SCK has been set to “0”.
     */
    set_sck(pgm, OFF);
    ft245r_powerup(pgm);

    set_reset(pgm, OFF);
    usleep(5000); // 5ms
    set_reset(pgm, ON);
    usleep(5000); // 5ms
    set_reset(pgm, OFF);

    /* Wait for at least 20 ms and enable serial programming by sending the Programming
     * Enable serial instruction to pin MOSI.
     */
    usleep(20000); // 20ms

    return ft245r_program_enable(pgm, p);
}

static inline int set_data(PROGRAMMER * pgm, unsigned char *buf, unsigned char data) {
    int j;
    int buf_pos = 0;
    unsigned char bit = 0x80;

    for (j=0; j<8; j++) {
        ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_AVR_MOSI,data & bit);

        ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_AVR_SCK,0);
        buf[buf_pos] = ft245r_out;
        buf_pos++;

        ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_AVR_SCK,1);
        buf[buf_pos] = ft245r_out;
        buf_pos++;

        bit >>= 1;
    }
    return buf_pos;
}

static inline unsigned char extract_data(PROGRAMMER * pgm, unsigned char *buf, int offset) {
    int j;
    int buf_pos = 1;
    unsigned char bit = 0x80;
    unsigned char r = 0;

    buf += offset * (8 * FT245R_CYCLES);
    for (j=0; j<8; j++) {
        if (GET_BITS_0(buf[buf_pos],pgm,PIN_AVR_MISO)) {
            r |= bit;
        }
        buf_pos += FT245R_CYCLES;
        bit >>= 1;
    }
    return r;
}

/* to check data */
static inline unsigned char extract_data_out(PROGRAMMER * pgm, unsigned char *buf, int offset) {
    int j;
    int buf_pos = 1;
    unsigned char bit = 0x80;
    unsigned char r = 0;

    buf += offset * (8 * FT245R_CYCLES);
    for (j=0; j<8; j++) {
        if (GET_BITS_0(buf[buf_pos],pgm,PIN_AVR_MOSI)) {
            r |= bit;
        }
        buf_pos += FT245R_CYCLES;
        bit >>= 1;
    }
    return r;
}


/*
 * transmit an AVR device command and return the results; 'cmd' and
 * 'res' must point to at least a 4 byte data buffer
 */
static int ft245r_cmd(PROGRAMMER * pgm, const unsigned char *cmd,
                      unsigned char *res) {
    int i,buf_pos;
    unsigned char buf[128];

    buf_pos = 0;
    for (i=0; i<4; i++) {
        buf_pos += set_data(pgm, buf+buf_pos, cmd[i]);
    }
    buf[buf_pos] = 0;
    buf_pos++;

    ft245r_send (pgm, buf, buf_pos);
    ft245r_recv (pgm, buf, buf_pos);
    res[0] = extract_data(pgm, buf, 0);
    res[1] = extract_data(pgm, buf, 1);
    res[2] = extract_data(pgm, buf, 2);
    res[3] = extract_data(pgm, buf, 3);

    return 0;
}

/* lower 8 pins are accepted, they might be also inverted */
static const struct pindef_t valid_pins = {{0xff},{0xff}} ;

static const struct pin_checklist_t pin_checklist[] = {
    { PIN_AVR_SCK,  1, &valid_pins},
    { PIN_AVR_MOSI, 1, &valid_pins},
    { PIN_AVR_MISO, 1, &valid_pins},
    { PIN_AVR_RESET,1, &valid_pins},
    { PPI_AVR_BUFF, 0, &valid_pins},
};

static int ft245r_open(PROGRAMMER * pgm, char * port) {
    int rv;
    int devnum = -1;

    rv = pins_check(pgm,pin_checklist,sizeof(pin_checklist)/sizeof(pin_checklist[0]), true);
    if(rv) {
        pgm->display(pgm, progbuf);
        return rv;
    }

    strcpy(pgm->port, port);

    if (strcmp(port,DEFAULT_USB) != 0) {
        if (strncasecmp("ft", port, 2) == 0) {
            char *startptr = port + 2;
            char *endptr = NULL;
            devnum = strtol(startptr,&endptr,10);
            if ((startptr==endptr) || (*endptr != '\0')) {
                devnum = -1;
            }
        }
        if (devnum < 0) {
            avrdude_message(MSG_INFO, "%s: invalid portname '%s': use 'ft[0-9]+'\n",
                            progname,port);
            return -1;
        }
    } else {
        devnum = 0;
    }

    handle = malloc (sizeof (struct ftdi_context));
    ftdi_init(handle);
    LNODEID usbpid = lfirst(pgm->usbpid);
    int pid;
    if (usbpid) {
      pid = *(int *)(ldata(usbpid));
      if (lnext(usbpid))
	avrdude_message(MSG_INFO, "%s: Warning: using PID 0x%04x, ignoring remaining PIDs in list\n",
		progname, pid);
    } else {
      pid = USB_DEVICE_FT245;
    }
    rv = ftdi_usb_open_desc_index(handle,
                                  pgm->usbvid?pgm->usbvid:USB_VENDOR_FTDI,
                                  pid,
                                  pgm->usbproduct[0]?pgm->usbproduct:NULL,
                                  pgm->usbsn[0]?pgm->usbsn:NULL,
                                  devnum);
    if (rv) {
        avrdude_message(MSG_INFO, "can't open ftdi device %d. (%s)\n", devnum, ftdi_get_error_string(handle));
        goto cleanup_no_usb;
    }

    ft245r_ddr = 
         pgm->pin[PIN_AVR_SCK].mask[0]
       | pgm->pin[PIN_AVR_MOSI].mask[0]
       | pgm->pin[PIN_AVR_RESET].mask[0]
       | pgm->pin[PPI_AVR_BUFF].mask[0]
       | pgm->pin[PPI_AVR_VCC].mask[0]
       | pgm->pin[PIN_LED_ERR].mask[0]
       | pgm->pin[PIN_LED_RDY].mask[0]
       | pgm->pin[PIN_LED_PGM].mask[0]
       | pgm->pin[PIN_LED_VFY].mask[0];

    /* set initial values for outputs, no reset everything else is off */
    ft245r_out = 0;
    ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_AVR_RESET,1);
    ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_AVR_SCK,0);
    ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_AVR_MOSI,0);
    ft245r_out = SET_BITS_0(ft245r_out,pgm,PPI_AVR_BUFF,0);
    ft245r_out = SET_BITS_0(ft245r_out,pgm,PPI_AVR_VCC,0);
    ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_LED_ERR,0);
    ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_LED_RDY,0);
    ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_LED_PGM,0);
    ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_LED_VFY,0);


    rv = ftdi_set_bitmode(handle, ft245r_ddr, BITMODE_SYNCBB); // set Synchronous BitBang
    if (rv) {
        avrdude_message(MSG_INFO, "%s: Synchronous BitBangMode is not supported (%s)\n",
                        progname, ftdi_get_error_string(handle));
        goto cleanup;
    }

    rv = ft245r_set_bitclock(pgm);
    if (rv) {
        goto cleanup;
    }

    /* We start a new thread to read the output from the FTDI. This is
     * necessary because otherwise we'll deadlock. We cannot finish
     * writing because the ftdi cannot send the results because we
     * haven't provided a read buffer yet. */

    sem_init (&buf_data, 0, 0);
    sem_init (&buf_space, 0, BUFSIZE);
    pthread_create (&readerthread, NULL, reader, handle);

    /*
     * drain any extraneous input
     */
    ft245r_drain (pgm, 0);

    ft245r_send (pgm, &ft245r_out, 1);
    ft245r_recv (pgm, &ft245r_in, 1);

    return 0;

cleanup:
    ftdi_usb_close(handle);
cleanup_no_usb:
    ftdi_deinit (handle);
    free(handle);
    handle = NULL;
    return -1;
}


static void ft245r_close(PROGRAMMER * pgm) {
    if (handle) {
        // I think the switch to BB mode and back flushes the buffer.
        ftdi_set_bitmode(handle, 0, BITMODE_SYNCBB); // set Synchronous BitBang, all in puts
        ftdi_set_bitmode(handle, 0, BITMODE_RESET); // disable Synchronous BitBang
        ftdi_usb_close(handle);
        ftdi_deinit (handle);
        pthread_cancel(readerthread);
        pthread_join(readerthread, NULL);
        free(handle);
        handle = NULL;
    }
}

static void ft245r_display(PROGRAMMER * pgm, const char * p) {
    avrdude_message(MSG_INFO, "%sPin assignment  : 0..7 = DBUS0..7\n",p);/* , 8..11 = GPIO0..3\n",p);*/
    pgm_display_generic_mask(pgm, p, SHOW_ALL_PINS);
}

static int ft245r_paged_write_gen(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                  unsigned int page_size, unsigned int addr,
                                  unsigned int n_bytes) {
    unsigned long i, pa;
    int rc;

    for (i=0; i<n_bytes; i++, addr++) {
        rc = avr_write_byte_default(pgm, p, m, addr, m->buf[addr]);
        if (rc != 0) {
            return -2;
        }

        if (m->paged) {
            // Can this piece of code ever be activated?? Do AVRs exist that
            // have paged non-flash memories? -- REW
            // XXX Untested code below.
            /*
             * check to see if it is time to flush the page with a page
             * write
             */

            if (((addr % m->page_size) == m->page_size-1) || (i == n_bytes-1)) {
                pa = addr - (addr % m->page_size);

                rc = avr_write_page(pgm, p, m, pa);
                if (rc != 0) {
                    return -2;
                }
            }
        }
    }
    return i;
}

static struct ft245r_request {
    int addr;
    int bytes;
    int n;
    struct ft245r_request *next;
} *req_head,*req_tail,*req_pool;

static void put_request(int addr, int bytes, int n) {
    struct ft245r_request *p;
    if (req_pool) {
        p = req_pool;
        req_pool = p->next;
    } else {
        p = malloc(sizeof(struct ft245r_request));
        if (!p) {
            avrdude_message(MSG_INFO, "can't alloc memory\n");
            exit(1);
        }
    }
    memset(p, 0, sizeof(struct ft245r_request));
    p->addr = addr;
    p->bytes = bytes;
    p->n = n;
    if (req_tail) {
        req_tail->next = p;
        req_tail = p;
    } else {
        req_head = req_tail = p;
    }
}

static int do_request(PROGRAMMER * pgm, AVRMEM *m) {
    struct ft245r_request *p;
    int addr, bytes, j, n;
    unsigned char buf[FT245R_FRAGMENT_SIZE+1+128];

    if (!req_head) return 0;
    p = req_head;
    req_head = p->next;
    if (!req_head) req_tail = req_head;

    addr = p->addr;
    bytes = p->bytes;
    n = p->n;
    memset(p, 0, sizeof(struct ft245r_request));
    p->next = req_pool;
    req_pool = p;

    ft245r_recv(pgm, buf, bytes);
    for (j=0; j<n; j++) {
        m->buf[addr++] = extract_data(pgm, buf , (j * 4 + 3));
    }
    return 1;
}

static int ft245r_paged_write_flash(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                    int page_size, int addr, int n_bytes) {
    unsigned int    i,j;
    int addr_save,buf_pos,do_page_write,req_count;
    unsigned char buf[FT245R_FRAGMENT_SIZE+1+128];

    req_count = 0;
    for (i=0; i<n_bytes; ) {
        addr_save = addr;
        buf_pos = 0;
        do_page_write = 0;
        for (j=0; j< FT245R_FRAGMENT_SIZE/8/FT245R_CYCLES/4; j++) {
            buf_pos += set_data(pgm, buf+buf_pos, (addr & 1)?0x48:0x40 );
            buf_pos += set_data(pgm, buf+buf_pos, (addr >> 9) & 0xff );
            buf_pos += set_data(pgm, buf+buf_pos, (addr >> 1) & 0xff );
            buf_pos += set_data(pgm, buf+buf_pos, m->buf[addr]);
            addr ++;
            i++;
            if ( (m->paged) &&
                    (((i % m->page_size) == 0) || (i == n_bytes))) {
                do_page_write = 1;
                break;
            }
        }
#if defined(USE_INLINE_WRITE_PAGE)
        if (do_page_write) {
            int addr_wk = addr_save - (addr_save % m->page_size);
            /* If this device has a "load extended address" command, issue it. */
            if (m->op[AVR_OP_LOAD_EXT_ADDR]) {
                unsigned char cmd[4];
                OPCODE *lext = m->op[AVR_OP_LOAD_EXT_ADDR];

                memset(cmd, 0, 4);
                avr_set_bits(lext, cmd);
                avr_set_addr(lext, cmd, addr_wk/2);
                buf_pos += set_data(pgm, buf+buf_pos, cmd[0]);
                buf_pos += set_data(pgm, buf+buf_pos, cmd[1]);
                buf_pos += set_data(pgm, buf+buf_pos, cmd[2]);
                buf_pos += set_data(pgm, buf+buf_pos, cmd[3]);
            }
            buf_pos += set_data(pgm, buf+buf_pos, 0x4C); /* Issue Page Write */
            buf_pos += set_data(pgm, buf+buf_pos,(addr_wk >> 9) & 0xff);
            buf_pos += set_data(pgm, buf+buf_pos,(addr_wk >> 1) & 0xff);
            buf_pos += set_data(pgm, buf+buf_pos, 0);
        }
#endif
        if (i >= n_bytes) {
            ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_AVR_SCK,0); // sck down
            buf[buf_pos++] = ft245r_out;
        }
        ft245r_send(pgm, buf, buf_pos);
        put_request(addr_save, buf_pos, 0);
        //ft245r_sync(pgm);
#if 0
        avrdude_message(MSG_INFO, "send addr 0x%04x bufsize %d [%02x %02x] page_write %d\n",
                addr_save,buf_pos,
                extract_data_out(pgm, buf , (0*4 + 3) ),
                extract_data_out(pgm, buf , (1*4 + 3) ),
                do_page_write);
#endif
        req_count++;
        if (req_count > REQ_OUTSTANDINGS)
            do_request(pgm, m);
        if (do_page_write) {
#if defined(USE_INLINE_WRITE_PAGE)
            while (do_request(pgm, m))
                ;
            usleep(m->max_write_delay);
#else
            int addr_wk = addr_save - (addr_save % m->page_size);
            int rc;
            while (do_request(pgm, m))
                ;
            rc = avr_write_page(pgm, p, m, addr_wk);
            if (rc != 0) {
                return -2;
            }
#endif
            req_count = 0;
        }
    }
    while (do_request(pgm, m))
        ;
    return i;
}


static int ft245r_paged_write(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                              unsigned int page_size, unsigned int addr, unsigned int n_bytes) {
    if (strcmp(m->desc, "flash") == 0) {
        return ft245r_paged_write_flash(pgm, p, m, page_size, addr, n_bytes);
    } else if (strcmp(m->desc, "eeprom") == 0) {
        return ft245r_paged_write_gen(pgm, p, m, page_size, addr, n_bytes);
    } else {
        return -2;
    }
}

static int ft245r_paged_load_gen(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                 unsigned int page_size, unsigned int addr,
                                 int n_bytes) {
    unsigned char    rbyte;
    unsigned long    i;
    int rc;

    for (i=0; i<n_bytes; i++) {
        rc = avr_read_byte_default(pgm, p, m, i+addr, &rbyte);
        if (rc != 0) {
            return -2;
        }
        m->buf[i+addr] = rbyte;
    }
    return 0;
}

static int ft245r_paged_load_flash(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                                   unsigned int page_size, unsigned int addr,
                                   unsigned int n_bytes) {
    unsigned long    i,j,n;
    int addr_save,buf_pos;
    int req_count = 0;
    unsigned char buf[FT245R_FRAGMENT_SIZE+1];

    for (i=0; i<n_bytes; ) {
        buf_pos = 0;
        addr_save = addr;
        for (j=0; j< FT245R_FRAGMENT_SIZE/8/FT245R_CYCLES/4; j++) {
            if (i >= n_bytes) break;
            buf_pos += set_data(pgm, buf+buf_pos, (addr & 1)?0x28:0x20 );
            buf_pos += set_data(pgm, buf+buf_pos, (addr >> 9) & 0xff );
            buf_pos += set_data(pgm, buf+buf_pos, (addr >> 1) & 0xff );
            buf_pos += set_data(pgm, buf+buf_pos, 0);
            addr ++;
            i++;
        }
        if (i >= n_bytes) {
            ft245r_out = SET_BITS_0(ft245r_out,pgm,PIN_AVR_SCK,0); // sck down
            buf[buf_pos++] = ft245r_out;
        }
        n = j;
        ft245r_send(pgm, buf, buf_pos);
        put_request(addr_save, buf_pos, n);
        req_count++;
        if (req_count > REQ_OUTSTANDINGS)
            do_request(pgm, m);

    }
    while (do_request(pgm, m))
        ;
    return 0;
}

static int ft245r_paged_load(PROGRAMMER * pgm, AVRPART * p, AVRMEM * m,
                             unsigned int page_size, unsigned int addr,
                             unsigned int n_bytes) {
    if (strcmp(m->desc, "flash") == 0) {
        return ft245r_paged_load_flash(pgm, p, m, page_size, addr, n_bytes);
    } else if (strcmp(m->desc, "eeprom") == 0) {
        return ft245r_paged_load_gen(pgm, p, m, page_size, addr, n_bytes);
    } else {
        return -2;
    }
}

void ft245r_initpgm(PROGRAMMER * pgm) {
    strcpy(pgm->type, "ftdi_syncbb");

    /*
     * mandatory functions
     */
    pgm->initialize     = ft245r_initialize;
    pgm->display        = ft245r_display;
    pgm->enable         = ft245r_enable;
    pgm->disable        = ft245r_disable;
    pgm->program_enable = ft245r_program_enable;
    pgm->chip_erase     = ft245r_chip_erase;
    pgm->cmd            = ft245r_cmd;
    pgm->open           = ft245r_open;
    pgm->close          = ft245r_close;
    pgm->read_byte      = avr_read_byte_default;
    pgm->write_byte     = avr_write_byte_default;

    /*
     * optional functions
     */
    pgm->paged_write = ft245r_paged_write;
    pgm->paged_load = ft245r_paged_load;

    pgm->rdy_led        = set_led_rdy;
    pgm->err_led        = set_led_err;
    pgm->pgm_led        = set_led_pgm;
    pgm->vfy_led        = set_led_vfy;
    pgm->powerup        = ft245r_powerup;
    pgm->powerdown      = ft245r_powerdown;

    handle = NULL;
}

#endif

const char ft245r_desc[] = "FT245R/FT232R Synchronous BitBangMode Programmer";
