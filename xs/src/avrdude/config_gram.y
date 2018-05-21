/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000-2004  Brian S. Dean <bsd@bsdhome.com>
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
%{

#include "ac_cfg.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "avrdude.h"
#include "libavrdude.h"
#include "config.h"

#if defined(WIN32NATIVE)
#define strtok_r( _s, _sep, _lasts ) \
    ( *(_lasts) = strtok( (_s), (_sep) ) )
#endif

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

int yylex(void);
int yyerror(char * errmsg, ...);
int yywarning(char * errmsg, ...);

static int assign_pin(int pinno, TOKEN * v, int invert);
static int assign_pin_list(int invert);
static int which_opcode(TOKEN * opcode);
static int parse_cmdbits(OPCODE * op);

static int pin_name;
%}

%token K_READ
%token K_WRITE
%token K_READ_LO
%token K_READ_HI
%token K_WRITE_LO
%token K_WRITE_HI
%token K_LOADPAGE_LO
%token K_LOADPAGE_HI
%token K_LOAD_EXT_ADDR
%token K_WRITEPAGE
%token K_CHIP_ERASE
%token K_PGM_ENABLE

%token K_MEMORY

%token K_PAGE_SIZE
%token K_PAGED

%token K_BAUDRATE
%token K_BS2
%token K_BUFF
%token K_CHIP_ERASE_DELAY
%token K_CONNTYPE
%token K_DEDICATED
%token K_DEFAULT_BITCLOCK
%token K_DEFAULT_PARALLEL
%token K_DEFAULT_PROGRAMMER
%token K_DEFAULT_SAFEMODE
%token K_DEFAULT_SERIAL
%token K_DESC
%token K_DEVICECODE
%token K_STK500_DEVCODE
%token K_AVR910_DEVCODE
%token K_EEPROM
%token K_ERRLED
%token K_FLASH
%token K_ID
%token K_IO
%token K_LOADPAGE
%token K_MAX_WRITE_DELAY
%token K_MCU_BASE
%token K_MIN_WRITE_DELAY
%token K_MISO
%token K_MOSI
%token K_NUM_PAGES
%token K_NVM_BASE
%token K_OCDREV
%token K_OFFSET
%token K_PAGEL
%token K_PARALLEL
%token K_PARENT
%token K_PART
%token K_PGMLED
%token K_PROGRAMMER
%token K_PSEUDO
%token K_PWROFF_AFTER_WRITE
%token K_RDYLED
%token K_READBACK_P1
%token K_READBACK_P2
%token K_READMEM
%token K_RESET
%token K_RETRY_PULSE
%token K_SERIAL
%token K_SCK
%token K_SIGNATURE
%token K_SIZE
%token K_USB
%token K_USBDEV
%token K_USBSN
%token K_USBPID
%token K_USBPRODUCT
%token K_USBVENDOR
%token K_USBVID
%token K_TYPE
%token K_VCC
%token K_VFYLED

%token K_NO
%token K_YES

/* stk500 v2 xml file parameters */
/* ISP */
%token K_TIMEOUT
%token K_STABDELAY
%token K_CMDEXEDELAY
%token K_HVSPCMDEXEDELAY
%token K_SYNCHLOOPS
%token K_BYTEDELAY
%token K_POLLVALUE
%token K_POLLINDEX
%token K_PREDELAY
%token K_POSTDELAY
%token K_POLLMETHOD
%token K_MODE
%token K_DELAY
%token K_BLOCKSIZE
%token K_READSIZE
/* HV mode */
%token K_HVENTERSTABDELAY
%token K_PROGMODEDELAY
%token K_LATCHCYCLES
%token K_TOGGLEVTG
%token K_POWEROFFDELAY
%token K_RESETDELAYMS
%token K_RESETDELAYUS
%token K_HVLEAVESTABDELAY
%token K_RESETDELAY
%token K_SYNCHCYCLES
%token K_HVCMDEXEDELAY

%token K_CHIPERASEPULSEWIDTH
%token K_CHIPERASEPOLLTIMEOUT
%token K_CHIPERASETIME
%token K_PROGRAMFUSEPULSEWIDTH
%token K_PROGRAMFUSEPOLLTIMEOUT
%token K_PROGRAMLOCKPULSEWIDTH
%token K_PROGRAMLOCKPOLLTIMEOUT

%token K_PP_CONTROLSTACK
%token K_HVSP_CONTROLSTACK

/* JTAG ICE mkII specific parameters */
%token K_ALLOWFULLPAGEBITSTREAM	/*
				 * Internal parameter for the JTAG
				 * ICE; describes the internal JTAG
				 * streaming behaviour inside the MCU.
				 * 1 for all older chips, 0 for newer
				 * MCUs.
				 */
%token K_ENABLEPAGEPROGRAMMING	/* ? yes for mega256*, mega406 */
%token K_HAS_JTAG		/* MCU has JTAG i/f. */
%token K_HAS_DW			/* MCU has debugWire i/f. */
%token K_HAS_PDI                /* MCU has PDI i/f rather than ISP (ATxmega). */
%token K_HAS_TPI                /* MCU has TPI i/f rather than ISP (ATtiny4/5/9/10). */
%token K_IDR			/* address of OCD register in IO space */
%token K_IS_AT90S1200		/* chip is an AT90S1200 (needs special treatment) */
%token K_IS_AVR32               /* chip is in the avr32 family */
%token K_RAMPZ			/* address of RAMPZ reg. in IO space */
%token K_SPMCR			/* address of SPMC[S]R in memory space */
%token K_EECR    		/* address of EECR in memory space */
%token K_FLASH_INSTR		/* flash instructions */
%token K_EEPROM_INSTR		/* EEPROM instructions */

%token TKN_COMMA
%token TKN_EQUAL
%token TKN_SEMI
%token TKN_TILDE
%token TKN_LEFT_PAREN
%token TKN_RIGHT_PAREN
%token TKN_NUMBER
%token TKN_NUMBER_REAL
%token TKN_STRING

%start configuration

%%

number_real : 
 TKN_NUMBER {
    $$ = $1;
    /* convert value to real */
    $$->value.number_real = $$->value.number;
    $$->value.type = V_NUM_REAL;
  } |
  TKN_NUMBER_REAL {
    $$ = $1;
  }

configuration :
  /* empty */ | config
;

config :
  def |
  config def
;


def :
  prog_def TKN_SEMI |

  part_def TKN_SEMI |

  K_DEFAULT_PROGRAMMER TKN_EQUAL TKN_STRING TKN_SEMI {
    strncpy(default_programmer, $3->value.string, MAX_STR_CONST);
    default_programmer[MAX_STR_CONST-1] = 0;
    free_token($3);
  } |

  K_DEFAULT_PARALLEL TKN_EQUAL TKN_STRING TKN_SEMI {
    strncpy(default_parallel, $3->value.string, PATH_MAX);
    default_parallel[PATH_MAX-1] = 0;
    free_token($3);
  } |

  K_DEFAULT_SERIAL TKN_EQUAL TKN_STRING TKN_SEMI {
    strncpy(default_serial, $3->value.string, PATH_MAX);
    default_serial[PATH_MAX-1] = 0;
    free_token($3);
  } |

  K_DEFAULT_BITCLOCK TKN_EQUAL number_real TKN_SEMI {
    default_bitclock = $3->value.number_real;
    free_token($3);
  } |

  K_DEFAULT_SAFEMODE TKN_EQUAL yesno TKN_SEMI {
    if ($3->primary == K_YES)
      default_safemode = 1;
    else if ($3->primary == K_NO)
      default_safemode = 0;
    free_token($3);
  }
;


prog_def :
  prog_decl prog_parms
    {
      PROGRAMMER * existing_prog;
      char * id;
      if (lsize(current_prog->id) == 0) {
        yyerror("required parameter id not specified");
        YYABORT;
      }
      if (current_prog->initpgm == NULL) {
        yyerror("programmer type not specified");
        YYABORT;
      }
      id = ldata(lfirst(current_prog->id));
      existing_prog = locate_programmer(programmers, id);
      if (existing_prog) {
        { /* temporarly set lineno to lineno of programmer start */
          int temp = lineno; lineno = current_prog->lineno;
          yywarning("programmer %s overwrites previous definition %s:%d.",
                id, existing_prog->config_file, existing_prog->lineno);
          lineno = temp;
        }
        lrmv_d(programmers, existing_prog);
        pgm_free(existing_prog);
      }
      PUSH(programmers, current_prog);
//      pgm_fill_old_pins(current_prog); // TODO to be removed if old pin data no longer needed
//      pgm_display_generic(current_prog, id);
      current_prog = NULL;
    }
;


prog_decl :
  K_PROGRAMMER
    { current_prog = pgm_new();
      if (current_prog == NULL) {
        yyerror("could not create pgm instance");
        YYABORT;
      }
      strcpy(current_prog->config_file, infile);
      current_prog->lineno = lineno;
    }
    |
  K_PROGRAMMER K_PARENT TKN_STRING
    {
      struct programmer_t * pgm = locate_programmer(programmers, $3->value.string);
      if (pgm == NULL) {
        yyerror("parent programmer %s not found", $3->value.string);
        free_token($3);
        YYABORT;
      }
      current_prog = pgm_dup(pgm);
      if (current_prog == NULL) {
        yyerror("could not duplicate pgm instance");
        free_token($3);
        YYABORT;
      }
      strcpy(current_prog->config_file, infile);
      current_prog->lineno = lineno;
      free_token($3);
    }
;


part_def :
  part_decl part_parms 
    { 
      LNODEID ln;
      AVRMEM * m;
      AVRPART * existing_part;

      if (current_part->id[0] == 0) {
        yyerror("required parameter id not specified");
        YYABORT;
      }

      /*
       * perform some sanity checking, and compute the number of bits
       * to shift a page for constructing the page address for
       * page-addressed memories.
       */
      for (ln=lfirst(current_part->mem); ln; ln=lnext(ln)) {
        m = ldata(ln);
        if (m->paged) {
          if (m->page_size == 0) {
            yyerror("must specify page_size for paged memory");
            YYABORT;
          }
          if (m->num_pages == 0) {
            yyerror("must specify num_pages for paged memory");
            YYABORT;
          }
          if (m->size != m->page_size * m->num_pages) {
            yyerror("page size (%u) * num_pages (%u) = "
                    "%u does not match memory size (%u)",
                    m->page_size,
                    m->num_pages,
                    m->page_size * m->num_pages,
                    m->size);
            YYABORT;
          }

        }
      }

      existing_part = locate_part(part_list, current_part->id);
      if (existing_part) {
        { /* temporarly set lineno to lineno of part start */
          int temp = lineno; lineno = current_part->lineno;
          yywarning("part %s overwrites previous definition %s:%d.",
                current_part->id,
                existing_part->config_file, existing_part->lineno);
          lineno = temp;
        }
        lrmv_d(part_list, existing_part);
        avr_free_part(existing_part);
      }
      PUSH(part_list, current_part); 
      current_part = NULL; 
    }
;

part_decl :
  K_PART
    {
      current_part = avr_new_part();
      if (current_part == NULL) {
        yyerror("could not create part instance");
        YYABORT;
      }
      strcpy(current_part->config_file, infile);
      current_part->lineno = lineno;
    } |
  K_PART K_PARENT TKN_STRING 
    {
      AVRPART * parent_part = locate_part(part_list, $3->value.string);
      if (parent_part == NULL) {
        yyerror("can't find parent part");
        free_token($3);
        YYABORT;
      }

      current_part = avr_dup_part(parent_part);
      if (current_part == NULL) {
        yyerror("could not duplicate part instance");
        free_token($3);
        YYABORT;
      }
      strcpy(current_part->config_file, infile);
      current_part->lineno = lineno;

      free_token($3);
    }
;

string_list :
  TKN_STRING { ladd(string_list, $1); } |
  string_list TKN_COMMA TKN_STRING { ladd(string_list, $3); }
;


num_list :
  TKN_NUMBER { ladd(number_list, $1); } |
  num_list TKN_COMMA TKN_NUMBER { ladd(number_list, $3); }
;

prog_parms :
  prog_parm TKN_SEMI |
  prog_parms prog_parm TKN_SEMI
;

prog_parm :
  K_ID TKN_EQUAL string_list {
    {
      TOKEN * t;
      char *s;
      int do_yyabort = 0;
      while (lsize(string_list)) {
        t = lrmv_n(string_list, 1);
        if (!do_yyabort) {
          s = dup_string(t->value.string);
          if (s == NULL) {
            do_yyabort = 1;
          } else {
            ladd(current_prog->id, s);
          }
        }
        /* if do_yyabort == 1 just make the list empty */
        free_token(t);
      }
      if (do_yyabort) {
        YYABORT;
      }
    }
  } |
  prog_parm_type
  |
  prog_parm_pins
  |
  prog_parm_usb
  |
  prog_parm_conntype
  |
  K_DESC TKN_EQUAL TKN_STRING {
    strncpy(current_prog->desc, $3->value.string, PGM_DESCLEN);
    current_prog->desc[PGM_DESCLEN-1] = 0;
    free_token($3);
  } |
  K_BAUDRATE TKN_EQUAL TKN_NUMBER {
    {
      current_prog->baudrate = $3->value.number;
      free_token($3);
    }
  }
;

prog_parm_type:
  K_TYPE TKN_EQUAL prog_parm_type_id
;

prog_parm_type_id:
  TKN_STRING        {
  const struct programmer_type_t * pgm_type = locate_programmer_type($1->value.string);
    if (pgm_type == NULL) {
        yyerror("programmer type %s not found", $1->value.string);
        free_token($1); 
        YYABORT;
    }
    current_prog->initpgm = pgm_type->initpgm;
    free_token($1); 
}
  | error
{
        yyerror("programmer type must be written as \"id_type\"");
        YYABORT;
}
;

prog_parm_conntype:
  K_CONNTYPE TKN_EQUAL prog_parm_conntype_id
;

prog_parm_conntype_id:
  K_PARALLEL        { current_prog->conntype = CONNTYPE_PARALLEL; } |
  K_SERIAL          { current_prog->conntype = CONNTYPE_SERIAL; } |
  K_USB             { current_prog->conntype = CONNTYPE_USB; }
;

prog_parm_usb:
  K_USBDEV TKN_EQUAL TKN_STRING {
    {
      strncpy(current_prog->usbdev, $3->value.string, PGM_USBSTRINGLEN);
      current_prog->usbdev[PGM_USBSTRINGLEN-1] = 0;
      free_token($3);
    }
  } |
  K_USBVID TKN_EQUAL TKN_NUMBER {
    {
      current_prog->usbvid = $3->value.number;
      free_token($3);
    }
  } |
  K_USBPID TKN_EQUAL usb_pid_list |
  K_USBSN TKN_EQUAL TKN_STRING {
    {
      strncpy(current_prog->usbsn, $3->value.string, PGM_USBSTRINGLEN);
      current_prog->usbsn[PGM_USBSTRINGLEN-1] = 0;
      free_token($3);
    }
  } |
  K_USBVENDOR TKN_EQUAL TKN_STRING {
    {
      strncpy(current_prog->usbvendor, $3->value.string, PGM_USBSTRINGLEN);
      current_prog->usbvendor[PGM_USBSTRINGLEN-1] = 0;
      free_token($3);
    }
  } |
  K_USBPRODUCT TKN_EQUAL TKN_STRING {
    {
      strncpy(current_prog->usbproduct, $3->value.string, PGM_USBSTRINGLEN);
      current_prog->usbproduct[PGM_USBSTRINGLEN-1] = 0;
      free_token($3);
    }
  }
;

usb_pid_list:
  TKN_NUMBER {
    {
      /* overwrite pids, so clear the existing entries */
      ldestroy_cb(current_prog->usbpid, free);
      current_prog->usbpid = lcreat(NULL, 0);
    }
    {
      int *ip = malloc(sizeof(int));
      if (ip) {
        *ip = $1->value.number;
        ladd(current_prog->usbpid, ip);
      }
      free_token($1);
    }
  } |
  usb_pid_list TKN_COMMA TKN_NUMBER {
    {
      int *ip = malloc(sizeof(int));
      if (ip) {
        *ip = $3->value.number;
        ladd(current_prog->usbpid, ip);
      }
      free_token($3);
    }
  }
;

pin_number_non_empty:
  TKN_NUMBER { if(0 != assign_pin(pin_name, $1, 0)) YYABORT;  }
  |
  TKN_TILDE TKN_NUMBER { if(0 != assign_pin(pin_name, $2, 1)) YYABORT; }
;

pin_number:
  pin_number_non_empty
  |
  /* empty */ { pin_clear_all(&(current_prog->pin[pin_name])); }
;

pin_list_element:
  pin_number_non_empty
  |
  TKN_TILDE TKN_LEFT_PAREN num_list TKN_RIGHT_PAREN { if(0 != assign_pin_list(1)) YYABORT; }
;

pin_list_non_empty:
  pin_list_element
  |
  pin_list_non_empty TKN_COMMA pin_list_element
;


pin_list:
  pin_list_non_empty
  |
  /* empty */ { pin_clear_all(&(current_prog->pin[pin_name])); }
;

prog_parm_pins:
  K_VCC    TKN_EQUAL {pin_name = PPI_AVR_VCC;  } pin_list |
  K_BUFF   TKN_EQUAL {pin_name = PPI_AVR_BUFF; } pin_list |
  K_RESET  TKN_EQUAL {pin_name = PIN_AVR_RESET;} pin_number { free_token($1); } |
  K_SCK    TKN_EQUAL {pin_name = PIN_AVR_SCK;  } pin_number { free_token($1); } |
  K_MOSI   TKN_EQUAL {pin_name = PIN_AVR_MOSI; } pin_number |
  K_MISO   TKN_EQUAL {pin_name = PIN_AVR_MISO; } pin_number |
  K_ERRLED TKN_EQUAL {pin_name = PIN_LED_ERR;  } pin_number |
  K_RDYLED TKN_EQUAL {pin_name = PIN_LED_RDY;  } pin_number |
  K_PGMLED TKN_EQUAL {pin_name = PIN_LED_PGM;  } pin_number |
  K_VFYLED TKN_EQUAL {pin_name = PIN_LED_VFY;  } pin_number
;

opcode :
  K_READ         |
  K_WRITE        |
  K_READ_LO      |
  K_READ_HI      |
  K_WRITE_LO     |
  K_WRITE_HI     |
  K_LOADPAGE_LO  |
  K_LOADPAGE_HI  |
  K_LOAD_EXT_ADDR |
  K_WRITEPAGE    |
  K_CHIP_ERASE   |
  K_PGM_ENABLE
;


part_parms :
  part_parm TKN_SEMI |
  part_parms part_parm TKN_SEMI
;


reset_disposition :
  K_DEDICATED | K_IO
;

parallel_modes :
  yesno | K_PSEUDO
;

retry_lines :
  K_RESET | K_SCK
;

part_parm :
  K_ID TKN_EQUAL TKN_STRING 
    {
      strncpy(current_part->id, $3->value.string, AVR_IDLEN);
      current_part->id[AVR_IDLEN-1] = 0;
      free_token($3);
    } |

  K_DESC TKN_EQUAL TKN_STRING 
    {
      strncpy(current_part->desc, $3->value.string, AVR_DESCLEN);
      current_part->desc[AVR_DESCLEN-1] = 0;
      free_token($3);
    } |

  K_DEVICECODE TKN_EQUAL TKN_NUMBER {
    {
      yyerror("devicecode is deprecated, use "
              "stk500_devcode instead");
      YYABORT;
    }
  } |

  K_STK500_DEVCODE TKN_EQUAL TKN_NUMBER {
    {
      current_part->stk500_devcode = $3->value.number;
      free_token($3);
    }
  } |

  K_AVR910_DEVCODE TKN_EQUAL TKN_NUMBER {
    {
      current_part->avr910_devcode = $3->value.number;
      free_token($3);
    }
  } |

  K_SIGNATURE TKN_EQUAL TKN_NUMBER TKN_NUMBER TKN_NUMBER {
    {
      current_part->signature[0] = $3->value.number;
      current_part->signature[1] = $4->value.number;
      current_part->signature[2] = $5->value.number;
      free_token($3);
      free_token($4);
      free_token($5);
    }
  } |

 K_USBPID TKN_EQUAL TKN_NUMBER {
    {
      current_part->usbpid = $3->value.number;
      free_token($3);
    }
  } |

  K_PP_CONTROLSTACK TKN_EQUAL num_list {
    {
      TOKEN * t;
      unsigned nbytes;
      int ok;

      current_part->ctl_stack_type = CTL_STACK_PP;
      nbytes = 0;
      ok = 1;

      memset(current_part->controlstack, 0, CTL_STACK_SIZE);
      while (lsize(number_list)) {
        t = lrmv_n(number_list, 1);
	if (nbytes < CTL_STACK_SIZE)
	  {
	    current_part->controlstack[nbytes] = t->value.number;
	    nbytes++;
	  }
	else
	  {
	    ok = 0;
	  }
        free_token(t);
      }
      if (!ok)
	{
	  yywarning("too many bytes in control stack");
        }
    }
  } |

  K_HVSP_CONTROLSTACK TKN_EQUAL num_list {
    {
      TOKEN * t;
      unsigned nbytes;
      int ok;

      current_part->ctl_stack_type = CTL_STACK_HVSP;
      nbytes = 0;
      ok = 1;

      memset(current_part->controlstack, 0, CTL_STACK_SIZE);
      while (lsize(number_list)) {
        t = lrmv_n(number_list, 1);
	if (nbytes < CTL_STACK_SIZE)
	  {
	    current_part->controlstack[nbytes] = t->value.number;
	    nbytes++;
	  }
	else
	  {
	    ok = 0;
	  }
        free_token(t);
      }
      if (!ok)
	{
	  yywarning("too many bytes in control stack");
        }
    }
  } |

  K_FLASH_INSTR TKN_EQUAL num_list {
    {
      TOKEN * t;
      unsigned nbytes;
      int ok;

      nbytes = 0;
      ok = 1;

      memset(current_part->flash_instr, 0, FLASH_INSTR_SIZE);
      while (lsize(number_list)) {
        t = lrmv_n(number_list, 1);
	if (nbytes < FLASH_INSTR_SIZE)
	  {
	    current_part->flash_instr[nbytes] = t->value.number;
	    nbytes++;
	  }
	else
	  {
	    ok = 0;
	  }
        free_token(t);
      }
      if (!ok)
	{
	  yywarning("too many bytes in flash instructions");
        }
    }
  } |

  K_EEPROM_INSTR TKN_EQUAL num_list {
    {
      TOKEN * t;
      unsigned nbytes;
      int ok;

      nbytes = 0;
      ok = 1;

      memset(current_part->eeprom_instr, 0, EEPROM_INSTR_SIZE);
      while (lsize(number_list)) {
        t = lrmv_n(number_list, 1);
	if (nbytes < EEPROM_INSTR_SIZE)
	  {
	    current_part->eeprom_instr[nbytes] = t->value.number;
	    nbytes++;
	  }
	else
	  {
	    ok = 0;
	  }
        free_token(t);
      }
      if (!ok)
	{
	  yywarning("too many bytes in EEPROM instructions");
        }
    }
  } |

  K_CHIP_ERASE_DELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->chip_erase_delay = $3->value.number;
      free_token($3);
    } |

  K_PAGEL TKN_EQUAL TKN_NUMBER
    {
      current_part->pagel = $3->value.number;
      free_token($3);
    } |

  K_BS2 TKN_EQUAL TKN_NUMBER
    {
      current_part->bs2 = $3->value.number;
      free_token($3);
    } |

  K_RESET TKN_EQUAL reset_disposition
    {
      if ($3->primary == K_DEDICATED)
        current_part->reset_disposition = RESET_DEDICATED;
      else if ($3->primary == K_IO)
        current_part->reset_disposition = RESET_IO;

      free_tokens(2, $1, $3);
    } |

  K_TIMEOUT TKN_EQUAL TKN_NUMBER
    {
      current_part->timeout = $3->value.number;
      free_token($3);
    } |

  K_STABDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->stabdelay = $3->value.number;
      free_token($3);
    } |

  K_CMDEXEDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->cmdexedelay = $3->value.number;
      free_token($3);
    } |

  K_HVSPCMDEXEDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->hvspcmdexedelay = $3->value.number;
      free_token($3);
    } |

  K_SYNCHLOOPS TKN_EQUAL TKN_NUMBER
    {
      current_part->synchloops = $3->value.number;
      free_token($3);
    } |

  K_BYTEDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->bytedelay = $3->value.number;
      free_token($3);
    } |

  K_POLLVALUE TKN_EQUAL TKN_NUMBER
    {
      current_part->pollvalue = $3->value.number;
      free_token($3);
    } |

  K_POLLINDEX TKN_EQUAL TKN_NUMBER
    {
      current_part->pollindex = $3->value.number;
      free_token($3);
    } |

  K_PREDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->predelay = $3->value.number;
      free_token($3);
    } |

  K_POSTDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->postdelay = $3->value.number;
      free_token($3);
    } |

  K_POLLMETHOD TKN_EQUAL TKN_NUMBER
    {
      current_part->pollmethod = $3->value.number;
      free_token($3);
    } |

  K_HVENTERSTABDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->hventerstabdelay = $3->value.number;
      free_token($3);
    } |

  K_PROGMODEDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->progmodedelay = $3->value.number;
      free_token($3);
    } |

  K_LATCHCYCLES TKN_EQUAL TKN_NUMBER
    {
      current_part->latchcycles = $3->value.number;
      free_token($3);
    } |

  K_TOGGLEVTG TKN_EQUAL TKN_NUMBER
    {
      current_part->togglevtg = $3->value.number;
      free_token($3);
    } |

  K_POWEROFFDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->poweroffdelay = $3->value.number;
      free_token($3);
    } |

  K_RESETDELAYMS TKN_EQUAL TKN_NUMBER
    {
      current_part->resetdelayms = $3->value.number;
      free_token($3);
    } |

  K_RESETDELAYUS TKN_EQUAL TKN_NUMBER
    {
      current_part->resetdelayus = $3->value.number;
      free_token($3);
    } |

  K_HVLEAVESTABDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->hvleavestabdelay = $3->value.number;
      free_token($3);
    } |

  K_RESETDELAY TKN_EQUAL TKN_NUMBER
    {
      current_part->resetdelay = $3->value.number;
      free_token($3);
    } |

  K_CHIPERASEPULSEWIDTH TKN_EQUAL TKN_NUMBER
    {
      current_part->chiperasepulsewidth = $3->value.number;
      free_token($3);
    } |

  K_CHIPERASEPOLLTIMEOUT TKN_EQUAL TKN_NUMBER
    {
      current_part->chiperasepolltimeout = $3->value.number;
      free_token($3);
    } |

  K_CHIPERASETIME TKN_EQUAL TKN_NUMBER
    {
      current_part->chiperasetime = $3->value.number;
      free_token($3);
    } |

  K_PROGRAMFUSEPULSEWIDTH TKN_EQUAL TKN_NUMBER
    {
      current_part->programfusepulsewidth = $3->value.number;
      free_token($3);
    } |

  K_PROGRAMFUSEPOLLTIMEOUT TKN_EQUAL TKN_NUMBER
    {
      current_part->programfusepolltimeout = $3->value.number;
      free_token($3);
    } |

  K_PROGRAMLOCKPULSEWIDTH TKN_EQUAL TKN_NUMBER
    {
      current_part->programlockpulsewidth = $3->value.number;
      free_token($3);
    } |

  K_PROGRAMLOCKPOLLTIMEOUT TKN_EQUAL TKN_NUMBER
    {
      current_part->programlockpolltimeout = $3->value.number;
      free_token($3);
    } |

  K_SYNCHCYCLES TKN_EQUAL TKN_NUMBER
    {
      current_part->synchcycles = $3->value.number;
      free_token($3);
    } |

  K_HAS_JTAG TKN_EQUAL yesno
    {
      if ($3->primary == K_YES)
        current_part->flags |= AVRPART_HAS_JTAG;
      else if ($3->primary == K_NO)
        current_part->flags &= ~AVRPART_HAS_JTAG;

      free_token($3);
    } |

  K_HAS_DW TKN_EQUAL yesno
    {
      if ($3->primary == K_YES)
        current_part->flags |= AVRPART_HAS_DW;
      else if ($3->primary == K_NO)
        current_part->flags &= ~AVRPART_HAS_DW;

      free_token($3);
    } |

  K_HAS_PDI TKN_EQUAL yesno
    {
      if ($3->primary == K_YES)
        current_part->flags |= AVRPART_HAS_PDI;
      else if ($3->primary == K_NO)
        current_part->flags &= ~AVRPART_HAS_PDI;

      free_token($3);
    } |

  K_HAS_TPI TKN_EQUAL yesno
    {
      if ($3->primary == K_YES)
        current_part->flags |= AVRPART_HAS_TPI;
      else if ($3->primary == K_NO)
        current_part->flags &= ~AVRPART_HAS_TPI;

      free_token($3);
    } |

  K_IS_AT90S1200 TKN_EQUAL yesno
    {
      if ($3->primary == K_YES)
        current_part->flags |= AVRPART_IS_AT90S1200;
      else if ($3->primary == K_NO)
        current_part->flags &= ~AVRPART_IS_AT90S1200;

      free_token($3);
    } |

  K_IS_AVR32 TKN_EQUAL yesno
    {
      if ($3->primary == K_YES)
        current_part->flags |= AVRPART_AVR32;
      else if ($3->primary == K_NO)
        current_part->flags &= ~AVRPART_AVR32;

      free_token($3);
    } |

  K_ALLOWFULLPAGEBITSTREAM TKN_EQUAL yesno
    {
      if ($3->primary == K_YES)
        current_part->flags |= AVRPART_ALLOWFULLPAGEBITSTREAM;
      else if ($3->primary == K_NO)
        current_part->flags &= ~AVRPART_ALLOWFULLPAGEBITSTREAM;

      free_token($3);
    } |

  K_ENABLEPAGEPROGRAMMING TKN_EQUAL yesno
    {
      if ($3->primary == K_YES)
        current_part->flags |= AVRPART_ENABLEPAGEPROGRAMMING;
      else if ($3->primary == K_NO)
        current_part->flags &= ~AVRPART_ENABLEPAGEPROGRAMMING;

      free_token($3);
    } |

  K_IDR TKN_EQUAL TKN_NUMBER
    {
      current_part->idr = $3->value.number;
      free_token($3);
    } |

  K_RAMPZ TKN_EQUAL TKN_NUMBER
    {
      current_part->rampz = $3->value.number;
      free_token($3);
    } |

  K_SPMCR TKN_EQUAL TKN_NUMBER
    {
      current_part->spmcr = $3->value.number;
      free_token($3);
    } |

  K_EECR TKN_EQUAL TKN_NUMBER
    {
      current_part->eecr = $3->value.number;
      free_token($3);
    } |

  K_MCU_BASE TKN_EQUAL TKN_NUMBER
    {
      current_part->mcu_base = $3->value.number;
      free_token($3);
    } |

  K_NVM_BASE TKN_EQUAL TKN_NUMBER
    {
      current_part->nvm_base = $3->value.number;
      free_token($3);
    } |

  K_OCDREV          TKN_EQUAL TKN_NUMBER
    {
      current_part->ocdrev = $3->value.number;
      free_token($3);
    } |

  K_SERIAL TKN_EQUAL yesno
    {
      if ($3->primary == K_YES)
        current_part->flags |= AVRPART_SERIALOK;
      else if ($3->primary == K_NO)
        current_part->flags &= ~AVRPART_SERIALOK;

      free_token($3);
    } |

  K_PARALLEL TKN_EQUAL parallel_modes
    {
      if ($3->primary == K_YES) {
        current_part->flags |= AVRPART_PARALLELOK;
        current_part->flags &= ~AVRPART_PSEUDOPARALLEL;
      }
      else if ($3->primary == K_NO) {
        current_part->flags &= ~AVRPART_PARALLELOK;
        current_part->flags &= ~AVRPART_PSEUDOPARALLEL;
      }
      else if ($3->primary == K_PSEUDO) {
        current_part->flags |= AVRPART_PARALLELOK;
        current_part->flags |= AVRPART_PSEUDOPARALLEL;
      }


      free_token($3);
    } |

  K_RETRY_PULSE TKN_EQUAL retry_lines
    {
      switch ($3->primary) {
        case K_RESET :
          current_part->retry_pulse = PIN_AVR_RESET;
          break;
        case K_SCK :
          current_part->retry_pulse = PIN_AVR_SCK;
          break;
      }

      free_token($1);
    } |


/*
  K_EEPROM { current_mem = AVR_M_EEPROM; }
    mem_specs |

  K_FLASH { current_mem = AVR_M_FLASH; }
    mem_specs |
*/

  K_MEMORY TKN_STRING 
    {
      current_mem = avr_new_memtype();
      if (current_mem == NULL) {
        yyerror("could not create mem instance");
        free_token($2);
        YYABORT;
      }
      strncpy(current_mem->desc, $2->value.string, AVR_MEMDESCLEN);
      current_mem->desc[AVR_MEMDESCLEN-1] = 0;
      free_token($2);
    }
    mem_specs 
    { 
      AVRMEM * existing_mem;

      existing_mem = avr_locate_mem(current_part, current_mem->desc);
      if (existing_mem != NULL) {
        lrmv_d(current_part->mem, existing_mem);
        avr_free_mem(existing_mem);
      }
      ladd(current_part->mem, current_mem); 
      current_mem = NULL; 
    } |

  opcode TKN_EQUAL string_list {
    { 
      int opnum;
      OPCODE * op;

      opnum = which_opcode($1);
      if (opnum < 0) YYABORT;
      op = avr_new_opcode();
      if (op == NULL) {
        yyerror("could not create opcode instance");
        free_token($1);
        YYABORT;
      }
      if(0 != parse_cmdbits(op)) YYABORT;
      if (current_part->op[opnum] != NULL) {
        /*yywarning("operation redefined");*/
        avr_free_opcode(current_part->op[opnum]);
      }
      current_part->op[opnum] = op;

      free_token($1);
    }
  }
;


yesno :
  K_YES | K_NO
;


mem_specs :
  mem_spec TKN_SEMI |
  mem_specs mem_spec TKN_SEMI
;


mem_spec :
  K_PAGED          TKN_EQUAL yesno
    {
      current_mem->paged = $3->primary == K_YES ? 1 : 0;
      free_token($3);
    } |

  K_SIZE            TKN_EQUAL TKN_NUMBER
    {
      current_mem->size = $3->value.number;
      free_token($3);
    } |


  K_PAGE_SIZE       TKN_EQUAL TKN_NUMBER
    {
      current_mem->page_size = $3->value.number;
      free_token($3);
    } |

  K_NUM_PAGES       TKN_EQUAL TKN_NUMBER
    {
      current_mem->num_pages = $3->value.number;
      free_token($3);
    } |

  K_OFFSET          TKN_EQUAL TKN_NUMBER
    {
      current_mem->offset = $3->value.number;
      free_token($3);
    } |

  K_MIN_WRITE_DELAY TKN_EQUAL TKN_NUMBER
    {
      current_mem->min_write_delay = $3->value.number;
      free_token($3);
    } |

  K_MAX_WRITE_DELAY TKN_EQUAL TKN_NUMBER
    {
      current_mem->max_write_delay = $3->value.number;
      free_token($3);
    } |

  K_PWROFF_AFTER_WRITE TKN_EQUAL yesno
    {
      current_mem->pwroff_after_write = $3->primary == K_YES ? 1 : 0;
      free_token($3);
    } |

  K_READBACK_P1     TKN_EQUAL TKN_NUMBER
    {
      current_mem->readback[0] = $3->value.number;
      free_token($3);
    } |

  K_READBACK_P2     TKN_EQUAL TKN_NUMBER
    {
      current_mem->readback[1] = $3->value.number;
      free_token($3);
    } |


  K_MODE TKN_EQUAL TKN_NUMBER
    {
      current_mem->mode = $3->value.number;
      free_token($3);
    } |

  K_DELAY TKN_EQUAL TKN_NUMBER
    {
      current_mem->delay = $3->value.number;
      free_token($3);
    } |

  K_BLOCKSIZE TKN_EQUAL TKN_NUMBER
    {
      current_mem->blocksize = $3->value.number;
      free_token($3);
    } |

  K_READSIZE TKN_EQUAL TKN_NUMBER
    {
      current_mem->readsize = $3->value.number;
      free_token($3);
    } |

  K_POLLINDEX TKN_EQUAL TKN_NUMBER
    {
      current_mem->pollindex = $3->value.number;
      free_token($3);
    } |


  opcode TKN_EQUAL string_list {
    { 
      int opnum;
      OPCODE * op;

      opnum = which_opcode($1);
      if (opnum < 0) YYABORT;
      op = avr_new_opcode();
      if (op == NULL) {
        yyerror("could not create opcode instance");
        free_token($1);
        YYABORT;
      }
      if(0 != parse_cmdbits(op)) YYABORT;
      if (current_mem->op[opnum] != NULL) {
        /*yywarning("operation redefined");*/
        avr_free_opcode(current_mem->op[opnum]);
      }
      current_mem->op[opnum] = op;

      free_token($1);
    }
  }
;


%%

#if 0
static char * vtypestr(int type)
{
  switch (type) {
    case V_NUM : return "INTEGER";
    case V_NUM_REAL: return "REAL";
    case V_STR : return "STRING";
    default:
      return "<UNKNOWN>";
  }
}
#endif


static int assign_pin(int pinno, TOKEN * v, int invert)
{
  int value;

  value = v->value.number;
  free_token(v);

  if ((value < PIN_MIN) || (value > PIN_MAX)) {
    yyerror("pin must be in the range " TOSTRING(PIN_MIN) "-"  TOSTRING(PIN_MAX));
    return -1;
  }

  pin_set_value(&(current_prog->pin[pinno]), value, invert);

  return 0;
}

static int assign_pin_list(int invert)
{
  TOKEN * t;
  int pin;
  int rv = 0;

  current_prog->pinno[pin_name] = 0;
  while (lsize(number_list)) {
    t = lrmv_n(number_list, 1);
    if (rv == 0) {
      pin = t->value.number;
      if ((pin < PIN_MIN) || (pin > PIN_MAX)) {
        yyerror("pin must be in the range " TOSTRING(PIN_MIN) "-"  TOSTRING(PIN_MAX));
        rv = -1;
      /* loop clears list and frees tokens */
      }
      pin_set_value(&(current_prog->pin[pin_name]), pin, invert);
    }
    free_token(t);
  }
  return rv;
}

static int which_opcode(TOKEN * opcode)
{
  switch (opcode->primary) {
    case K_READ        : return AVR_OP_READ; break;
    case K_WRITE       : return AVR_OP_WRITE; break;
    case K_READ_LO     : return AVR_OP_READ_LO; break;
    case K_READ_HI     : return AVR_OP_READ_HI; break;
    case K_WRITE_LO    : return AVR_OP_WRITE_LO; break;
    case K_WRITE_HI    : return AVR_OP_WRITE_HI; break;
    case K_LOADPAGE_LO : return AVR_OP_LOADPAGE_LO; break;
    case K_LOADPAGE_HI : return AVR_OP_LOADPAGE_HI; break;
    case K_LOAD_EXT_ADDR : return AVR_OP_LOAD_EXT_ADDR; break;
    case K_WRITEPAGE   : return AVR_OP_WRITEPAGE; break;
    case K_CHIP_ERASE  : return AVR_OP_CHIP_ERASE; break;
    case K_PGM_ENABLE  : return AVR_OP_PGM_ENABLE; break;
    default :
      yyerror("invalid opcode");
      return -1;
      break;
  }
}


static int parse_cmdbits(OPCODE * op)
{
  TOKEN * t;
  int bitno;
  char ch;
  char * e;
  char * q;
  int len;
  char * s, *brkt = NULL;
  int rv = 0;

  bitno = 32;
  while (lsize(string_list)) {

    t = lrmv_n(string_list, 1);

    s = strtok_r(t->value.string, " ", &brkt);
    while (rv == 0 && s != NULL) {

      bitno--;
      if (bitno < 0) {
        yyerror("too many opcode bits for instruction");
        rv = -1;
        break;
      }

      len = strlen(s);

      if (len == 0) {
        yyerror("invalid bit specifier \"\"");
        rv = -1;
        break;
      }

      ch = s[0];

      if (len == 1) {
        switch (ch) {
          case '1':
            op->bit[bitno].type  = AVR_CMDBIT_VALUE;
            op->bit[bitno].value = 1;
            op->bit[bitno].bitno = bitno % 8;
            break;
          case '0':
            op->bit[bitno].type  = AVR_CMDBIT_VALUE;
            op->bit[bitno].value = 0;
            op->bit[bitno].bitno = bitno % 8;
            break;
          case 'x':
            op->bit[bitno].type  = AVR_CMDBIT_IGNORE;
            op->bit[bitno].value = 0;
            op->bit[bitno].bitno = bitno % 8;
            break;
          case 'a':
            op->bit[bitno].type  = AVR_CMDBIT_ADDRESS;
            op->bit[bitno].value = 0;
            op->bit[bitno].bitno = 8*(bitno/8) + bitno % 8;
            break;
          case 'i':
            op->bit[bitno].type  = AVR_CMDBIT_INPUT;
            op->bit[bitno].value = 0;
            op->bit[bitno].bitno = bitno % 8;
            break;
          case 'o':
            op->bit[bitno].type  = AVR_CMDBIT_OUTPUT;
            op->bit[bitno].value = 0;
            op->bit[bitno].bitno = bitno % 8;
            break;
          default :
            yyerror("invalid bit specifier '%c'", ch);
            rv = -1;
            break;
        }
      }
      else {
        if (ch == 'a') {
          q = &s[1];
          op->bit[bitno].bitno = strtol(q, &e, 0);
          if ((e == q)||(*e != 0)) {
            yyerror("can't parse bit number from \"%s\"", q);
            rv = -1;
            break;
          }
          op->bit[bitno].type = AVR_CMDBIT_ADDRESS;
          op->bit[bitno].value = 0;
        }
        else {
          yyerror("invalid bit specifier \"%s\"", s);
          rv = -1;
          break;
        }
      }

      s = strtok_r(NULL, " ", &brkt);
    } /* while */

    free_token(t);

  }  /* while */

  return rv;
}
