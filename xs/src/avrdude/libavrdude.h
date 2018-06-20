/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) Joerg Wunsch
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

#ifndef libavrdude_h
#define libavrdude_h

/* XXX should go away */
#include "ac_cfg.h"

#include <stdio.h>
#include <limits.h>
#include <stdbool.h>

/* lets try to select at least 32 bits */
#ifdef HAVE_STDINT_H
#include <stdint.h>
typedef uint32_t pinmask_t;
#else
#if UINT_MAX >= 0xFFFFFFFF
typedef unsigned int pinmask_t;
#else
typedef unsigned long pinmask_t;
#endif
#endif


// PATH_MAX is used throughout avrdude for various purposes.
// It is problematic though as it may or may not be defined on various systems
// and even when it is, it tends to be somewhat arbitrary.
// So instead we just define a value here that should be fine in most cases.
#ifdef PATH_MAX
#undef PATH_MAX
#endif
#define PATH_MAX 4096


/* formerly lists.h */

/*----------------------------------------------------------------------
  General purpose linked list routines - header file declarations.

  Author : Brian Dean
  Date   : 10 January, 1990
  ----------------------------------------------------------------------*/

typedef void * LISTID;
typedef void * LNODEID;


/*----------------------------------------------------------------------
  several defines to access the LIST structure as as stack or a queue
  --- use for program readability
  ----------------------------------------------------------------------*/
#define STACKID LISTID
#define SNODEID LNODEID
#define QUEUEID LISTID
#define QNODEID LNODEID


#define PUSH(s,d)    lins_n(s,d,1)   /* push 'd' onto the stack */
#define POP(s)       lrmv_n(s,1)     /* pop the stack */
#define LOOKSTACK(s) lget_n(s,1)     /* look at the top of the stack, 
					but don't pop */


#define ENQUEUE(q,d) lins_n(q,d,1)   /* put 'd' on the end of the queue */
#define DEQUEUE(q)   lrmv(q)         /* remove next item from the front of 
					the queue */
#define REQUEUE(q,d) ladd(q,d)       /* re-insert (push) item back on the
					front of the queue */
#define LOOKQUEUE(q) lget(q)         /* return next item on the queue, 
					but don't dequeue */
#define QUEUELEN(q)  lsize(q)       /* length of the queue */


#define LISTADD(l,d) ladd(l,d)       /* add to end of the list */
#define LISTRMV(l,d) lrmv_d(l,d)     /* remove from end of the list */


#ifdef __cplusplus
extern "C" {
#endif

/* .................... Function Prototypes .................... */

LISTID     lcreat      ( void * liststruct, int poolsize );
void       ldestroy    ( LISTID lid );
void       ldestroy_cb ( LISTID lid, void (*ucleanup)(void * data_ptr) );

LNODEID    lfirst ( LISTID  ); /* head of the list */
LNODEID    llast  ( LISTID  ); /* tail of the list */
LNODEID    lnext  ( LNODEID ); /* next item in the list */
LNODEID    lprev  ( LNODEID ); /* previous item in the list */
void     * ldata  ( LNODEID ); /* data at the current position */
int        lsize  ( LISTID  ); /* number of elements in the list */

int        ladd     ( LISTID lid, void * p );
int        laddo    ( LISTID lid, void *p, 
		      int (*compare)(const void *p1,const void *p2),
		      LNODEID * firstdup );
int        laddu    ( LISTID lid, void * p, 
		      int (*compare)(const void *p1,const void *p2));
int        lins_n   ( LISTID lid, void * d, unsigned int n );
int        lins_ln  ( LISTID lid, LNODEID lnid, void * data_ptr );

void     * lget    ( LISTID lid );
void     * lget_n  ( LISTID lid, unsigned int n );
LNODEID    lget_ln ( LISTID lid, unsigned int n );

void     * lrmv    ( LISTID lid );
void     * lrmv_n  ( LISTID lid, unsigned int n );
void     * lrmv_ln ( LISTID lid, LNODEID lnid );
void     * lrmv_d  ( LISTID lid, void * data_ptr );

LISTID     lcat    ( LISTID lid1, LISTID lid2 );

void       lsort   ( LISTID lid, int (*compare)(void * p1, void * p2));

void     * lsrch   ( LISTID lid, void * p, int (*compare)(void *p1,void *p2));

int        lprint  ( FILE * f, LISTID lid );

#ifdef __cplusplus
}
#endif

/* formerly avrpart.h */

/*
 * AVR serial programming instructions
 */
enum {
  AVR_OP_READ,
  AVR_OP_WRITE,
  AVR_OP_READ_LO,
  AVR_OP_READ_HI,
  AVR_OP_WRITE_LO,
  AVR_OP_WRITE_HI,
  AVR_OP_LOADPAGE_LO,
  AVR_OP_LOADPAGE_HI,
  AVR_OP_LOAD_EXT_ADDR,
  AVR_OP_WRITEPAGE,
  AVR_OP_CHIP_ERASE,
  AVR_OP_PGM_ENABLE,
  AVR_OP_MAX
};


enum {
  AVR_CMDBIT_IGNORE,  /* bit is ignored on input and output */
  AVR_CMDBIT_VALUE,   /* bit is set to 0 or 1 for input or output */
  AVR_CMDBIT_ADDRESS, /* this bit represents an input address bit */
  AVR_CMDBIT_INPUT,   /* this bit is an input bit */
  AVR_CMDBIT_OUTPUT   /* this bit is an output bit */
};

enum { /* these are assigned to reset_disposition of AVRPART */
  RESET_DEDICATED,    /* reset pin is dedicated */
  RESET_IO            /* reset pin might be configured as an I/O pin */
};

enum ctl_stack_t {
  CTL_STACK_NONE,     /* no control stack defined */
  CTL_STACK_PP,	      /* parallel programming control stack */
  CTL_STACK_HVSP      /* high voltage serial programming control stack */
};

/*
 * serial programming instruction bit specifications
 */
typedef struct cmdbit {
  int          type;  /* AVR_CMDBIT_* */
  int          bitno; /* which input bit to use for this command bit */
  int          value; /* bit value if type == AVR_CMDBIT_VALUD */
} CMDBIT;

typedef struct opcode {
  CMDBIT        bit[32]; /* opcode bit specs */
} OPCODE;


#define AVRPART_SERIALOK       0x0001  /* part supports serial programming */
#define AVRPART_PARALLELOK     0x0002  /* part supports parallel programming */
#define AVRPART_PSEUDOPARALLEL 0x0004  /* part has pseudo parallel support */
#define AVRPART_HAS_JTAG       0x0008  /* part has a JTAG i/f */
#define AVRPART_ALLOWFULLPAGEBITSTREAM 0x0010 /* JTAG ICE mkII param. */
#define AVRPART_ENABLEPAGEPROGRAMMING 0x0020 /* JTAG ICE mkII param. */
#define AVRPART_HAS_DW         0x0040  /* part has a debugWire i/f */
#define AVRPART_HAS_PDI        0x0080  /* part has PDI i/f rather than ISP (ATxmega) */
#define AVRPART_AVR32          0x0100  /* part is in AVR32 family */
#define AVRPART_INIT_SMC       0x0200  /* part will undergo chip erase */
#define AVRPART_WRITE          0x0400  /* at least one write operation specified */
#define AVRPART_HAS_TPI        0x0800  /* part has TPI i/f rather than ISP (ATtiny4/5/9/10) */
#define AVRPART_IS_AT90S1200   0x1000  /* part is an AT90S1200 (needs special treatment) */

#define AVR_DESCLEN 64
#define AVR_IDLEN   32
#define CTL_STACK_SIZE 32
#define FLASH_INSTR_SIZE 3
#define EEPROM_INSTR_SIZE 20

#define TAG_ALLOCATED          1    /* memory byte is allocated */

typedef struct avrpart {
  char          desc[AVR_DESCLEN];  /* long part name */
  char          id[AVR_IDLEN];      /* short part name */
  int           stk500_devcode;     /* stk500 device code */
  int           avr910_devcode;     /* avr910 device code */
  int           chip_erase_delay;   /* microseconds */
  unsigned char pagel;              /* for parallel programming */
  unsigned char bs2;                /* for parallel programming */
  unsigned char signature[3];       /* expected value of signature bytes */
  unsigned short usbpid;            /* USB DFU product ID (0 = none) */
  int           reset_disposition;  /* see RESET_ enums */
  int           retry_pulse;        /* retry program enable by pulsing
                                       this pin (PIN_AVR_*) */
  unsigned      flags;              /* see AVRPART_ masks */

  int           timeout;            /* stk500 v2 xml file parameter */
  int           stabdelay;          /* stk500 v2 xml file parameter */
  int           cmdexedelay;        /* stk500 v2 xml file parameter */
  int           synchloops;         /* stk500 v2 xml file parameter */
  int           bytedelay;          /* stk500 v2 xml file parameter */
  int           pollindex;          /* stk500 v2 xml file parameter */
  unsigned char pollvalue;          /* stk500 v2 xml file parameter */
  int           predelay;           /* stk500 v2 xml file parameter */
  int           postdelay;          /* stk500 v2 xml file parameter */
  int           pollmethod;         /* stk500 v2 xml file parameter */

  enum ctl_stack_t ctl_stack_type;  /* what to use the ctl stack for */
  unsigned char controlstack[CTL_STACK_SIZE]; /* stk500v2 PP/HVSP ctl stack */
  unsigned char flash_instr[FLASH_INSTR_SIZE]; /* flash instructions (debugWire, JTAG) */
  unsigned char eeprom_instr[EEPROM_INSTR_SIZE]; /* EEPROM instructions (debugWire, JTAG) */

  int           hventerstabdelay;   /* stk500 v2 hv mode parameter */
  int           progmodedelay;      /* stk500 v2 hv mode parameter */
  int           latchcycles;        /* stk500 v2 hv mode parameter */
  int           togglevtg;          /* stk500 v2 hv mode parameter */
  int           poweroffdelay;      /* stk500 v2 hv mode parameter */
  int           resetdelayms;       /* stk500 v2 hv mode parameter */
  int           resetdelayus;       /* stk500 v2 hv mode parameter */
  int           hvleavestabdelay;   /* stk500 v2 hv mode parameter */
  int           resetdelay;         /* stk500 v2 hv mode parameter */
  int           chiperasepulsewidth; /* stk500 v2 hv mode parameter */
  int           chiperasepolltimeout; /* stk500 v2 hv mode parameter */
  int           chiperasetime;      /* stk500 v2 hv mode parameter */
  int           programfusepulsewidth; /* stk500 v2 hv mode parameter */
  int           programfusepolltimeout; /* stk500 v2 hv mode parameter */
  int           programlockpulsewidth; /* stk500 v2 hv mode parameter */
  int           programlockpolltimeout; /* stk500 v2 hv mode parameter */
  int           synchcycles;        /* stk500 v2 hv mode parameter */
  int           hvspcmdexedelay;    /* stk500 v2 xml file parameter */

  unsigned char idr;                /* JTAG ICE mkII XML file parameter */
  unsigned char rampz;              /* JTAG ICE mkII XML file parameter */
  unsigned char spmcr;              /* JTAG ICE mkII XML file parameter */
  unsigned short eecr;              /* JTAC ICE mkII XML file parameter */
  unsigned int mcu_base;            /* Base address of MCU control block in ATxmega devices */
  unsigned int nvm_base;            /* Base address of NVM controller in ATxmega devices */
  int           ocdrev;             /* OCD revision (JTAGICE3 parameter, from AS6 XML files) */

  OPCODE      * op[AVR_OP_MAX];     /* opcodes */

  LISTID        mem;                /* avr memory definitions */
  char          config_file[PATH_MAX]; /* config file where defined */
  int           lineno;                /* config file line number */
} AVRPART;

#define AVR_MEMDESCLEN 64
typedef struct avrmem {
  char desc[AVR_MEMDESCLEN];  /* memory description ("flash", "eeprom", etc) */
  int paged;                  /* page addressed (e.g. ATmega flash) */
  int size;                   /* total memory size in bytes */
  int page_size;              /* size of memory page (if page addressed) */
  int num_pages;              /* number of pages (if page addressed) */
  unsigned int offset;        /* offset in IO memory (ATxmega) */
  int min_write_delay;        /* microseconds */
  int max_write_delay;        /* microseconds */
  int pwroff_after_write;     /* after this memory type is written to,
                                 the device must be powered off and
                                 back on, see errata
                                 http://www.atmel.com/dyn/resources/prod_documents/doc1280.pdf */
  unsigned char readback[2];  /* polled read-back values */

  int mode;                   /* stk500 v2 xml file parameter */
  int delay;                  /* stk500 v2 xml file parameter */
  int blocksize;              /* stk500 v2 xml file parameter */
  int readsize;               /* stk500 v2 xml file parameter */
  int pollindex;              /* stk500 v2 xml file parameter */

  unsigned char * buf;        /* pointer to memory buffer */
  unsigned char * tags;       /* allocation tags */
  OPCODE * op[AVR_OP_MAX];    /* opcodes */
} AVRMEM;

#ifdef __cplusplus
extern "C" {
#endif

/* Functions for OPCODE structures */
OPCODE * avr_new_opcode(void);
void     avr_free_opcode(OPCODE * op);
int avr_set_bits(OPCODE * op, unsigned char * cmd);
int avr_set_addr(OPCODE * op, unsigned char * cmd, unsigned long addr);
int avr_set_input(OPCODE * op, unsigned char * cmd, unsigned char data);
int avr_get_output(OPCODE * op, unsigned char * res, unsigned char * data);
int avr_get_output_index(OPCODE * op);

/* Functions for AVRMEM structures */
AVRMEM * avr_new_memtype(void);
int avr_initmem(AVRPART * p);
AVRMEM * avr_dup_mem(AVRMEM * m);
void     avr_free_mem(AVRMEM * m);
AVRMEM * avr_locate_mem(AVRPART * p, char * desc);
void avr_mem_display(const char * prefix, FILE * f, AVRMEM * m, int type,
                     int verbose);

/* Functions for AVRPART structures */
AVRPART * avr_new_part(void);
AVRPART * avr_dup_part(AVRPART * d);
void      avr_free_part(AVRPART * d);
AVRPART * locate_part(LISTID parts, char * partdesc);
AVRPART * locate_part_by_avr910_devcode(LISTID parts, int devcode);
AVRPART * locate_part_by_signature(LISTID parts, unsigned char * sig,
                                   int sigsize);
void avr_display(FILE * f, AVRPART * p, const char * prefix, int verbose);

typedef void (*walk_avrparts_cb)(const char *name, const char *desc,
                                 const char *cfgname, int cfglineno,
                                 void *cookie);
void walk_avrparts(LISTID avrparts, walk_avrparts_cb cb, void *cookie);
void sort_avrparts(LISTID avrparts);
#ifdef __cplusplus
}
#endif

/* formerly pindefs.h */

enum {
  PPI_AVR_VCC = 1,
  PPI_AVR_BUFF,
  PIN_AVR_RESET,
  PIN_AVR_SCK,
  PIN_AVR_MOSI,
  PIN_AVR_MISO,
  PIN_LED_ERR,
  PIN_LED_RDY,
  PIN_LED_PGM,
  PIN_LED_VFY,
  N_PINS
};

#define PIN_MASK    (UINT_MAX>>1)
#define PIN_INVERSE (~(PIN_MASK))	/* flag for inverted pin in serbb */
#define PIN_MIN     0   /* smallest allowed pin number */
#define PIN_MAX     31  /* largest allowed pin number */

#ifdef HAVE_LINUXGPIO
/* Embedded systems might have a lot more gpio than only 0-31 */
#undef PIN_MAX
#define PIN_MAX     255 /* largest allowed pin number */
#endif

/** Number of pins in each element of the bitfield */
#define PIN_FIELD_ELEMENT_SIZE (sizeof(pinmask_t) * 8)
/** Numer of elements to store the complete bitfield of all pins */
#define PIN_FIELD_SIZE ((PIN_MAX + PIN_FIELD_ELEMENT_SIZE)/PIN_FIELD_ELEMENT_SIZE)

/**
 * This sets the corresponding bits to 1 or 0, the inverse mask is used to invert the value in necessary.
 * It uses only the lowest element (index=0) of the bitfield, which should be enough for most
 * programmers.
 *
 * @param[in] x       input value
 * @param[in] pgm     the programmer whose pin definitions to use
 * @param[in] pinname the logical name of the pin (PIN_AVR_*, ...)
 * @param[in] level   the logical level (level != 0 => 1, level == 0 => 0),
 *                      if the pin is defined as inverted the resulting bit is also inverted
 * @returns           the input value with the relevant bits modified
 */
#define SET_BITS_0(x,pgm,pinname,level) (((x) & ~(pgm)->pin[pinname].mask[0]) \
    | (\
        (pgm)->pin[pinname].mask[0] & ( \
             (level) \
             ?~((pgm)->pin[pinname].inverse[0]) \
             : ((pgm)->pin[pinname].inverse[0]) \
        ) \
    ) \
)

/**
 * Check if the corresponding bit is set (returns != 0) or cleared.
 * The inverse mask is used, to invert the relevant bits.
 * If the pin definition contains multiple pins, then a single set pin leads to return value != 0.
 * Then you have to check the relevant bits of the returned value, if you need more information.
 * It uses only the lowest element (index=0) of the bitfield, which should be enough for most
 * programmers.
 *
 * @param[in] x       input value
 * @param[in] pgm     the programmer whose pin definitions to use
 * @param[in] pinname the logical name of the pin (PIN_AVR_*, ...)
 * @returns           the input value with only the relevant bits (which are already inverted,
 *                      so you get always the logical level)
 */
#define GET_BITS_0(x,pgm,pinname)       (((x) ^ (pgm)->pin[pinname].inverse[0]) & (pgm)->pin[pinname].mask[0])

/**
 * Data structure to hold used pins by logical function (PIN_AVR_*, ...)
 */
struct pindef_t {
  pinmask_t mask[PIN_FIELD_SIZE]; ///< bitfield of used pins
  pinmask_t inverse[PIN_FIELD_SIZE]; ///< bitfield of inverse/normal usage of used pins
};

/**
 * Data structure to define a checklist of valid pins for each function.
 */
struct pin_checklist_t {
  int pinname; ///< logical pinname eg. PIN_AVR_SCK
  int mandatory; ///< is this a mandatory pin
  const struct pindef_t* valid_pins; ///< mask defines allowed pins, inverse define is they might be used inverted
};

/**
 * Adds a pin in the pin definition as normal or inverse pin.
 *
 * @param[out] pindef pin definition to update
 * @param[in] pin number of pin [0..PIN_MAX]
 * @param[in] inverse inverse (true) or normal (false) pin
 */
void pin_set_value(struct pindef_t * const pindef, const int pin, const bool inverse);

/**
 * Clear all defined pins in pindef.
 *
 * @param[out] pindef pin definition to clear
 */
void pin_clear_all(struct pindef_t * const pindef);

struct programmer_t; /* forward declaration */

/**
 * Convert for given programmer new pin definitions to old pin definitions.
 *
 * @param[inout] pgm programmer whose pins shall be converted.
 */
int pgm_fill_old_pins(struct programmer_t * const pgm);

/**
 * This function checks all pin of pgm against the constraints given in the checklist.
 * It checks if 
 * @li any invalid pins are used
 * @li valid pins are used inverted when not allowed
 * @li any pins are used by more than one function
 * @li any mandatory pin is not set all.
 *
 * In case of any error it report the wrong function and the pin numbers.
 * For verbose >= 2 it also reports the possible correct values.
 * For verbose >=3 it shows also which pins were ok.
 *
 * @param[in] pgm the programmer to check
 * @param[in] checklist the constraint for the pins
 * @param[in] size the number of entries in checklist
 * @param[in] output false suppresses error messages to the user
 * @returns 0 if all pin definitions are valid, -1 otherwise
 */
int pins_check(const struct programmer_t * const pgm, const struct pin_checklist_t * const checklist, const int size, const bool output);

/**
 * Returns the name of the pin as string.
 * 
 * @param pinname the pinname which we want as string.
 * @returns a string with the pinname, or <unknown> if pinname is invalid.
 */
const char * avr_pin_name(int pinname);

/**
 * This function returns a string representation of defined pins eg. ~1,2,~4,~5,7
 * Another execution of this function will overwrite the previous result in the static buffer.
 *
 * @param[in] pindef the pin definition for which we want the string representation
 * @returns pointer to a static string.
 */
const char * pins_to_str(const struct pindef_t * const pindef);

/**
 * This function returns a string representation of pins in the mask eg. 1,3,5-7,9,12
 * Another execution of this function will overwrite the previous result in the static buffer.
 * Consecutive pin number are representated as start-end.
 *
 * @param[in] pinmask the pin mask for which we want the string representation
 * @returns pointer to a static string.
 */
const char * pinmask_to_str(const pinmask_t * const pinmask);

/* formerly serial.h */

/* This is the API for the generic serial interface. The implementations are
   actually provided by the target dependant files:

   ser_posix.c : posix serial interface.
   ser_win32.c : native win32 serial interface.

   The target file will be selected at configure time. */

extern long serial_recv_timeout;
union filedescriptor
{
  int ifd;
  void *pfd;
  struct
  {
    void *handle;
    int rep;                    /* bulk read endpoint */
    int wep;                    /* bulk write endpoint */
    int eep;                    /* event read endpoint */
    int max_xfer;               /* max transfer size */
    int use_interrupt_xfer;     /* device uses interrupt transfers */
  } usb;
};

union pinfo
{
  long baud;
  struct
  {
    unsigned short vid;
    unsigned short pid;
    unsigned short flags;
#define PINFO_FL_USEHID         0x0001
#define PINFO_FL_SILENT         0x0002  /* don't complain if not found */
  } usbinfo;
};


struct serial_device
{
  // open should return -1 on error, other values on success
  int (*open)(char * port, union pinfo pinfo, union filedescriptor *fd); 
  int (*setspeed)(union filedescriptor *fd, long baud);
  void (*close)(union filedescriptor *fd);

  int (*send)(union filedescriptor *fd, const unsigned char * buf, size_t buflen);
  int (*recv)(union filedescriptor *fd, unsigned char * buf, size_t buflen);
  int (*drain)(union filedescriptor *fd, int display);

  int (*set_dtr_rts)(union filedescriptor *fd, int is_on);

  int flags;
#define SERDEV_FL_NONE         0x0000 /* no flags */
#define SERDEV_FL_CANSETSPEED  0x0001 /* device can change speed */
};

extern struct serial_device *serdev;
extern struct serial_device serial_serdev;
extern struct serial_device usb_serdev;
extern struct serial_device usb_serdev_frame;
extern struct serial_device avrdoper_serdev;
extern struct serial_device usbhid_serdev;

#define serial_open (serdev->open)
#define serial_setspeed (serdev->setspeed)
#define serial_close (serdev->close)
#define serial_send (serdev->send)
#define serial_recv (serdev->recv)
#define serial_drain (serdev->drain)
#define serial_set_dtr_rts (serdev->set_dtr_rts)

/* formerly pgm.h */

#define ON  1
#define OFF 0

#define PGM_DESCLEN 80
#define PGM_PORTLEN PATH_MAX
#define PGM_TYPELEN 32
#define PGM_USBSTRINGLEN 256

typedef enum {
  EXIT_VCC_UNSPEC,
  EXIT_VCC_ENABLED,
  EXIT_VCC_DISABLED
} exit_vcc_t;

typedef enum {
  EXIT_RESET_UNSPEC,
  EXIT_RESET_ENABLED,
  EXIT_RESET_DISABLED
} exit_reset_t;

typedef enum {
  EXIT_DATAHIGH_UNSPEC,
  EXIT_DATAHIGH_ENABLED,
  EXIT_DATAHIGH_DISABLED
} exit_datahigh_t;

typedef enum {
  CONNTYPE_PARALLEL,
  CONNTYPE_SERIAL,
  CONNTYPE_USB
} conntype_t;

typedef struct programmer_t {
  LISTID id;
  char desc[PGM_DESCLEN];
  char type[PGM_TYPELEN];
  char port[PGM_PORTLEN];
  void (*initpgm)(struct programmer_t * pgm);
  unsigned int pinno[N_PINS];
  struct pindef_t pin[N_PINS];
  exit_vcc_t exit_vcc;
  exit_reset_t exit_reset;
  exit_datahigh_t exit_datahigh;
  conntype_t conntype;
  int ppidata;
  int ppictrl;
  int baudrate;
  int usbvid;
  LISTID usbpid;
  char usbdev[PGM_USBSTRINGLEN], usbsn[PGM_USBSTRINGLEN];
  char usbvendor[PGM_USBSTRINGLEN], usbproduct[PGM_USBSTRINGLEN];
  double bitclock;    /* JTAG ICE clock period in microseconds */
  int ispdelay;    /* ISP clock delay */
  union filedescriptor fd;
  int  page_size;  /* page size if the programmer supports paged write/load */
  int  (*rdy_led)        (struct programmer_t * pgm, int value);
  int  (*err_led)        (struct programmer_t * pgm, int value);
  int  (*pgm_led)        (struct programmer_t * pgm, int value);
  int  (*vfy_led)        (struct programmer_t * pgm, int value);
  int  (*initialize)     (struct programmer_t * pgm, AVRPART * p);
  void (*display)        (struct programmer_t * pgm, const char * p);
  void (*enable)         (struct programmer_t * pgm);
  void (*disable)        (struct programmer_t * pgm);
  void (*powerup)        (struct programmer_t * pgm);
  void (*powerdown)      (struct programmer_t * pgm);
  int  (*program_enable) (struct programmer_t * pgm, AVRPART * p);
  int  (*chip_erase)     (struct programmer_t * pgm, AVRPART * p);
  int  (*cmd)            (struct programmer_t * pgm, const unsigned char *cmd,
                          unsigned char *res);
  int  (*cmd_tpi)        (struct programmer_t * pgm, const unsigned char *cmd,
                          int cmd_len, unsigned char res[], int res_len);
  int  (*spi)            (struct programmer_t * pgm, const unsigned char *cmd,
                          unsigned char *res, int count);
  int  (*open)           (struct programmer_t * pgm, char * port);
  void (*close)          (struct programmer_t * pgm);
  int  (*paged_write)    (struct programmer_t * pgm, AVRPART * p, AVRMEM * m, 
                          unsigned int page_size, unsigned int baseaddr,
                          unsigned int n_bytes);
  int  (*paged_load)     (struct programmer_t * pgm, AVRPART * p, AVRMEM * m,
                          unsigned int page_size, unsigned int baseaddr,
                          unsigned int n_bytes);
  int  (*page_erase)     (struct programmer_t * pgm, AVRPART * p, AVRMEM * m,
                          unsigned int baseaddr);
  void (*write_setup)    (struct programmer_t * pgm, AVRPART * p, AVRMEM * m);
  int  (*write_byte)     (struct programmer_t * pgm, AVRPART * p, AVRMEM * m,
                          unsigned long addr, unsigned char value);
  int  (*read_byte)      (struct programmer_t * pgm, AVRPART * p, AVRMEM * m,
                          unsigned long addr, unsigned char * value);
  int  (*read_sig_bytes) (struct programmer_t * pgm, AVRPART * p, AVRMEM * m);
  void (*print_parms)    (struct programmer_t * pgm);
  int  (*set_vtarget)    (struct programmer_t * pgm, double v);
  int  (*set_varef)      (struct programmer_t * pgm, unsigned int chan, double v);
  int  (*set_fosc)       (struct programmer_t * pgm, double v);
  int  (*set_sck_period) (struct programmer_t * pgm, double v);
  int  (*setpin)         (struct programmer_t * pgm, int pinfunc, int value);
  int  (*getpin)         (struct programmer_t * pgm, int pinfunc);
  int  (*highpulsepin)   (struct programmer_t * pgm, int pinfunc);
  int  (*parseexitspecs) (struct programmer_t * pgm, char *s);
  int  (*perform_osccal) (struct programmer_t * pgm);
  int  (*parseextparams) (struct programmer_t * pgm, LISTID xparams);
  void (*setup)          (struct programmer_t * pgm);
  void (*teardown)       (struct programmer_t * pgm);
  void (*set_upload_size)(struct programmer_t * pgm, int size);
  char config_file[PATH_MAX]; /* config file where defined */
  int  lineno;                /* config file line number */
  void *cookie;		      /* for private use by the programmer */
  char flag;		      /* for private use of the programmer */
} PROGRAMMER;

#ifdef __cplusplus
extern "C" {
#endif

PROGRAMMER * pgm_new(void);
PROGRAMMER * pgm_dup(const PROGRAMMER * const src);
void         pgm_free(PROGRAMMER * const p);

void programmer_display(PROGRAMMER * pgm, const char * p);

/* show is a mask like this (1<<PIN_AVR_SCK)|(1<<PIN_AVR_MOSI)| ... */
#define SHOW_ALL_PINS (~0u)
#define SHOW_PPI_PINS ((1<<PPI_AVR_VCC)|(1<<PPI_AVR_BUFF))
#define SHOW_AVR_PINS ((1<<PIN_AVR_RESET)|(1<<PIN_AVR_SCK)|(1<<PIN_AVR_MOSI)|(1<<PIN_AVR_MISO))
#define SHOW_LED_PINS ((1<<PIN_LED_ERR)|(1<<PIN_LED_RDY)|(1<<PIN_LED_PGM)|(1<<PIN_LED_VFY))
void pgm_display_generic_mask(PROGRAMMER * pgm, const char * p, unsigned int show);
void pgm_display_generic(PROGRAMMER * pgm, const char * p);

PROGRAMMER * locate_programmer(LISTID programmers, const char * configid);

typedef void (*walk_programmers_cb)(const char *name, const char *desc,
                                    const char *cfgname, int cfglineno,
                                    void *cookie);
void walk_programmers(LISTID programmers, walk_programmers_cb cb, void *cookie);

void sort_programmers(LISTID programmers);

#ifdef __cplusplus
}
#endif

/* formerly avr.h */

typedef void (*FP_UpdateProgress)(int percent, double etime, char *hdr);

extern struct avrpart parts[];

extern FP_UpdateProgress update_progress;

extern bool cancel_flag;
#define RETURN_IF_CANCEL() \
  do { \
    if (cancel_flag) { \
      avrdude_message(MSG_INFO, "avrdude: %s(): Cancelled, exiting...\n", __func__); \
      return -99; \
    } \
  } while (0)


#ifdef __cplusplus
extern "C" {
#endif

int avr_tpi_poll_nvmbsy(PROGRAMMER *pgm);
int avr_tpi_chip_erase(PROGRAMMER * pgm, AVRPART * p);
int avr_tpi_program_enable(PROGRAMMER * pgm, AVRPART * p, unsigned char guard_time);
int avr_read_byte_default(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
			  unsigned long addr, unsigned char * value);

int avr_read(PROGRAMMER * pgm, AVRPART * p, char * memtype, AVRPART * v);

int avr_write_page(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
                   unsigned long addr);

int avr_write_byte(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
                   unsigned long addr, unsigned char data);

int avr_write_byte_default(PROGRAMMER * pgm, AVRPART * p, AVRMEM * mem,
			   unsigned long addr, unsigned char data);

int avr_write(PROGRAMMER * pgm, AVRPART * p, char * memtype, int size,
              int auto_erase);

int avr_signature(PROGRAMMER * pgm, AVRPART * p);

int avr_verify(AVRPART * p, AVRPART * v, char * memtype, int size);

int avr_get_cycle_count(PROGRAMMER * pgm, AVRPART * p, int * cycles);

int avr_put_cycle_count(PROGRAMMER * pgm, AVRPART * p, int cycles);

int avr_mem_hiaddr(AVRMEM * mem);

int avr_chip_erase(PROGRAMMER * pgm, AVRPART * p);

void report_progress (int completed, int total, char *hdr);

#ifdef __cplusplus
}
#endif

/* formerly fileio.h */

typedef enum {
  FMT_AUTO,
  FMT_SREC,
  FMT_IHEX,
  FMT_RBIN,
  FMT_IMM,
  FMT_HEX,
  FMT_DEC,
  FMT_OCT,
  FMT_BIN,
  FMT_ELF
} FILEFMT;

struct fioparms {
  int    op;
  char * mode;
  char * iodesc;
  char * dir;
  char * rw;
  unsigned int fileoffset;
};

enum {
  FIO_READ,
  FIO_WRITE
};

#ifdef __cplusplus
extern "C" {
#endif

char * fmtstr(FILEFMT format);

int fileio(int op, char * filename, FILEFMT format,
           struct avrpart * p, char * memtype, int size, size_t offset);

#ifdef __cplusplus
}
#endif


/* formerly safemode.h */

#ifdef __cplusplus
extern "C" {
#endif

/* Writes the specified fuse in fusename (can be "lfuse", "hfuse", or "efuse") and verifies it. Will try up to tries
amount of times before giving up */
int safemode_writefuse (unsigned char fuse, char * fusename, PROGRAMMER * pgm, AVRPART * p, int tries);

/* Reads the fuses three times, checking that all readings are the same. This will ensure that the before values aren't in error! */
int safemode_readfuses (unsigned char * lfuse, unsigned char * hfuse, unsigned char * efuse, unsigned char * fuse, PROGRAMMER * pgm, AVRPART * p);
  
/* This routine will store the current values pointed to by lfuse, hfuse, and efuse into an internal buffer in this routine
when save is set to 1. When save is 0 (or not 1 really) it will copy the values from the internal buffer into the locations
pointed to be lfuse, hfuse, and efuse. This allows you to change the fuse bits if needed from another routine (ie: have it so
if user requests fuse bits are changed, the requested value is now verified */
int safemode_memfuses (int save, unsigned char * lfuse, unsigned char * hfuse, unsigned char * efuse, unsigned char * fuse);

#ifdef __cplusplus
}
#endif


/* formerly update.h */

enum {
  DEVICE_READ,
  DEVICE_WRITE,
  DEVICE_VERIFY
};

enum updateflags {
  UF_NONE = 0,
  UF_NOWRITE = 1,
  UF_AUTO_ERASE = 2,
};


typedef struct update_t {
  char * memtype;
  int    op;
  size_t offset;
  char * filename;
  int    format;
} UPDATE;

#ifdef __cplusplus
extern "C" {
#endif

extern UPDATE * parse_op(char * s);
extern UPDATE * dup_update(UPDATE * upd);
extern UPDATE * new_update(int op, char * memtype, int filefmt,
			   char * filename, size_t offset);
extern void free_update(UPDATE * upd);
extern int do_op(PROGRAMMER * pgm, struct avrpart * p, UPDATE * upd,
		 enum updateflags flags);

#ifdef __cplusplus
}
#endif


/* formerly pgm_type.h */

/*LISTID programmer_types;*/

typedef struct programmer_type_t {
  const char * const id;
  void (*initpgm)(struct programmer_t * pgm);
  const char * const desc;
} PROGRAMMER_TYPE;

#ifdef __cplusplus
extern "C" {
#endif

const PROGRAMMER_TYPE * locate_programmer_type(/*LISTID programmer_types, */const char * id);

typedef void (*walk_programmer_types_cb)(const char *id, const char *desc,
                                    void *cookie);
void walk_programmer_types(/*LISTID programmer_types,*/ walk_programmer_types_cb cb, void *cookie);

#ifdef __cplusplus
}
#endif

/* formerly config.h */

extern LISTID       part_list;
extern LISTID       programmers;
extern char         default_programmer[];
extern char         default_parallel[];
extern char         default_serial[];
extern double       default_bitclock;
extern int          default_safemode;

/* This name is fixed, it's only here for symmetry with
 * default_parallel and default_serial. */
#define DEFAULT_USB "usb"

#ifdef __cplusplus
extern "C" {
#endif

int init_config(void);

void cleanup_config(void);

int read_config(const char * file);

#ifdef __cplusplus
}
#endif

// Header file for alloca()
#if defined(WIN32NATIVE)
#  include <malloc.h>
#else
#  include <alloca.h>
#endif


/* formerly confwin.h */

// #if defined(WIN32NATIVE)

// #ifdef __cplusplus
// extern "C" {
// #endif

// void win_sys_config_set(char sys_config[PATH_MAX]);
// void win_usr_config_set(char usr_config[PATH_MAX]);

// #ifdef __cplusplus
// }
// #endif

// #endif  /* WIN32NATIVE */


#endif  /* libavrdude_h */
