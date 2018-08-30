/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000-2005  Brian S. Dean <bsd@bsdhome.com>
 * Copyright 2007-2014 Joerg Wunsch <j@uriah.heep.sax.de>
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
 * Code to program an Atmel AVR device through one of the supported
 * programmers.
 *
 * For parallel port connected programmers, the pin definitions can be
 * changed via a config file.  See the config file for instructions on
 * how to add a programmer definition.
 *  
 */

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>

#if !defined(WIN32NATIVE)
#  include <sys/time.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#endif

#include "avrdude.h"
#include "libavrdude.h"

#include "term.h"


/* Get VERSION from ac_cfg.h */
char * version      = VERSION "-prusa3d";

char * progname;
char   progbuf[PATH_MAX]; /* temporary buffer of spaces the same
                             length as progname; used for lining up
                             multiline messages */

#define MSGBUFFER_SIZE 4096
char msgbuffer[MSGBUFFER_SIZE];

bool cancel_flag = false;

static void avrdude_message_handler_null(const char *msg, unsigned size, void *user_p)
{
    // Output to stderr by default
    (void)size;
    (void)user_p;
    fputs(msg, stderr);
}

static void *avrdude_message_handler_user_p = NULL;
static avrdude_message_handler_t avrdude_message_handler = avrdude_message_handler_null;

void avrdude_message_handler_set(avrdude_message_handler_t newhandler, void *user_p)
{
    if (newhandler != NULL) {
        avrdude_message_handler = newhandler;
        avrdude_message_handler_user_p = user_p;
    } else {
        avrdude_message_handler = avrdude_message_handler_null;
        avrdude_message_handler_user_p = NULL;
    }
}

int avrdude_message(const int msglvl, const char *format, ...)
{
    static const char *format_error = "avrdude_message: Could not format message";

    int rc = 0;
    va_list ap;
    if (verbose >= msglvl) {
        va_start(ap, format);
        rc = vsnprintf(msgbuffer, MSGBUFFER_SIZE, format, ap);

        if (rc > 0 && rc < MSGBUFFER_SIZE) {
            avrdude_message_handler(msgbuffer, rc, avrdude_message_handler_user_p);
        } else {
            rc = snprintf(msgbuffer, MSGBUFFER_SIZE, "%s: %s", format_error, format);
            if (rc > 0 && rc < MSGBUFFER_SIZE) {
                avrdude_message_handler(msgbuffer, rc, avrdude_message_handler_user_p);
            } else {
                avrdude_message_handler(format_error, strlen(format_error), avrdude_message_handler_user_p);
            }
        }

        va_end(ap);
    }
    return rc;
}


static void avrdude_progress_handler_null(const char *task, unsigned progress, void *user_p)
{
    // By default do nothing
    (void)task;
    (void)progress;
    (void)user_p;
}

static void *avrdude_progress_handler_user_p = NULL;
static avrdude_progress_handler_t avrdude_progress_handler = avrdude_progress_handler_null;

void avrdude_progress_handler_set(avrdude_progress_handler_t newhandler, void *user_p)
{
    if (newhandler != NULL) {
        avrdude_progress_handler = newhandler;
        avrdude_progress_handler_user_p = user_p;
    } else {
        avrdude_progress_handler = avrdude_progress_handler_null;
        avrdude_progress_handler_user_p = NULL;
    }
}

void avrdude_progress_external(const char *task, unsigned progress)
{
    avrdude_progress_handler(task, progress, avrdude_progress_handler_user_p);
}

static void avrdude_oom_handler_null(const char *context, void *user_p)
{
    // Output a message and just exit
    fputs("avrdude: Out of memory: ", stderr);
    fputs(context, stderr);
    exit(99);
}

static void *avrdude_oom_handler_user_p = NULL;
static avrdude_oom_handler_t avrdude_oom_handler = avrdude_oom_handler_null;

void avrdude_oom_handler_set(avrdude_oom_handler_t newhandler, void *user_p)
{
    if (newhandler != NULL) {
        avrdude_oom_handler = newhandler;
        avrdude_oom_handler_user_p = user_p;
    } else {
        avrdude_oom_handler = avrdude_oom_handler_null;
        avrdude_oom_handler_user_p = NULL;
    }
}

void avrdude_oom(const char *context)
{
    avrdude_oom_handler(context, avrdude_oom_handler_user_p);
}

void avrdude_cancel()
{
    cancel_flag = true;
}


struct list_walk_cookie
{
    FILE *f;
    const char *prefix;
};

static LISTID updates = NULL;

static LISTID extended_params = NULL;

static LISTID additional_config_files = NULL;

static PROGRAMMER * pgm;
static bool pgm_setup = false;

/*
 * global options
 */
int    verbose;     /* verbose output */
int    quell_progress; /* un-verebose output */
int    ovsigck;     /* 1=override sig check, 0=don't */




/*
 * usage message
 */
static void usage(void)
{
  avrdude_message(MSG_INFO, 
 "Usage: %s [options]\n"
 "Options:\n"
 "  -p <partno>                Required. Specify AVR device.\n"
 "  -b <baudrate>              Override RS-232 baud rate.\n"
 "  -B <bitclock>              Specify JTAG/STK500v2 bit clock period (us).\n"
 "  -C <config-file>           Specify location of configuration file.\n"
 "  -c <programmer>            Specify programmer type.\n"
 "  -D                         Disable auto erase for flash memory\n"
 "  -i <delay>                 ISP Clock Delay [in microseconds]\n"
 "  -P <port>                  Specify connection port.\n"
 "  -F                         Override invalid signature check.\n"
 "  -e                         Perform a chip erase.\n"
 "  -O                         Perform RC oscillator calibration (see AVR053). \n"
 "  -U <memtype>:r|w|v:<section>:<filename>[:format]\n"
 "                             Memory operation specification.\n"
 "                             Multiple -U options are allowed, each request\n"
 "                             is performed in the order specified.\n"
 "  -n                         Do not write anything to the device.\n"
 "  -V                         Do not verify.\n"
 "  -u                         Disable safemode, default when running from a script.\n"
 "  -s                         Silent safemode operation, will not ask you if\n"
 "                             fuses should be changed back.\n"
 "  -t                         Enter terminal mode.\n"
 "  -E <exitspec>[,<exitspec>] List programmer exit specifications.\n"
 "  -x <extended_param>        Pass <extended_param> to programmer.\n"
 "  -y                         Count # erase cycles in EEPROM.\n"
 "  -Y <number>                Initialize erase cycle # in EEPROM.\n"
 "  -v                         Verbose output. -v -v for more.\n"
 "  -q                         Quell progress output. -q -q for less.\n"
//  "  -l logfile                 Use logfile rather than stderr for diagnostics.\n"
 "  -?                         Display this usage.\n"
 "\navrdude version %s, URL: <http://savannah.nongnu.org/projects/avrdude/>\n"
          ,progname, version);
}


// static void update_progress_tty (int percent, double etime, char *hdr)
// {
//   static char hashes[51];
//   static char *header;
//   static int last = 0;
//   int i;

//   setvbuf(stderr, (char*)NULL, _IONBF, 0);

//   hashes[50] = 0;

//   memset (hashes, ' ', 50);
//   for (i=0; i<percent; i+=2) {
//     hashes[i/2] = '#';
//   }

//   if (hdr) {
//     avrdude_message(MSG_INFO, "\n");
//     last = 0;
//     header = hdr;
//   }

//   if (last == 0) {
//     avrdude_message(MSG_INFO, "\r%s | %s | %d%% %0.2fs",
//             header, hashes, percent, etime);
//   }

//   if (percent == 100) {
//     if (!last) avrdude_message(MSG_INFO, "\n\n");
//     last = 1;
//   }

//   setvbuf(stderr, (char*)NULL, _IOLBF, 0);
// }

static void update_progress_no_tty (int percent, double etime, char *hdr)
{
  static int done = 0;
  static int last = 0;
  static char *header = NULL;
  int cnt = (percent>>1)*2;

  // setvbuf(stderr, (char*)NULL, _IONBF, 0);

  if (hdr) {
    avrdude_message(MSG_INFO, "\n%s | ", hdr);
    last = 0;
    done = 0;
    header = hdr;
    avrdude_progress_external(header, 0);
  }
  else {
    while ((cnt > last) && (done == 0)) {
      avrdude_message(MSG_INFO, "#");
      cnt -=  2;
    }

    if (done == 0) {
      avrdude_progress_external(header, percent > 99 ? 99 : percent);
    }
  }

  if ((percent == 100) && (done == 0)) {
    avrdude_message(MSG_INFO, " | 100%% %0.2fs\n\n", etime);
    avrdude_progress_external(header, 100);
    last = 0;
    done = 1;
  }
  else
    last = (percent>>1)*2;    /* Make last a multiple of 2. */

  // setvbuf(stderr, (char*)NULL, _IOLBF, 0);
}

static void list_programmers_callback(const char *name, const char *desc,
                                      const char *cfgname, int cfglineno,
                                      void *cookie)
{
    struct list_walk_cookie *c = (struct list_walk_cookie *)cookie;
    if (verbose){
        fprintf(c->f, "%s%-16s = %-30s [%s:%d]\n",
                c->prefix, name, desc, cfgname, cfglineno);
    } else {
        fprintf(c->f, "%s%-16s = %-s\n",
                c->prefix, name, desc);
    }
}

static void list_programmers(FILE * f, const char *prefix, LISTID programmers)
{
    struct list_walk_cookie c;

    c.f = f;
    c.prefix = prefix;

    sort_programmers(programmers);

    walk_programmers(programmers, list_programmers_callback, &c);
}

static void list_programmer_types_callback(const char *name, const char *desc,
                                      void *cookie)
{
    struct list_walk_cookie *c = (struct list_walk_cookie *)cookie;
    fprintf(c->f, "%s%-16s = %-s\n",
                c->prefix, name, desc);
}

static void list_programmer_types(FILE * f, const char *prefix)
{
    struct list_walk_cookie c;

    c.f = f;
    c.prefix = prefix;

    walk_programmer_types(list_programmer_types_callback, &c);
}

static void list_avrparts_callback(const char *name, const char *desc,
                                   const char *cfgname, int cfglineno,
                                   void *cookie)
{
    struct list_walk_cookie *c = (struct list_walk_cookie *)cookie;

    /* hide ids starting with '.' */
    if ((verbose < 2) && (name[0] == '.'))
        return;

    if (verbose) {
        fprintf(c->f, "%s%-8s = %-18s [%s:%d]\n",
                c->prefix, name, desc, cfgname, cfglineno);
    } else {
        fprintf(c->f, "%s%-8s = %s\n",
                c->prefix, name, desc);
    }
}

static void list_parts(FILE * f, const char *prefix, LISTID avrparts)
{
    struct list_walk_cookie c;

    c.f = f;
    c.prefix = prefix;

    sort_avrparts(avrparts);

    walk_avrparts(avrparts, list_avrparts_callback, &c);
}

// static void exithook(void)
// {
//     if (pgm->teardown)
//         pgm->teardown(pgm);
// }

static int cleanup_main(int status)
{
    if (pgm_setup && pgm != NULL && pgm->teardown) {
        pgm->teardown(pgm);
    }

    if (updates) {
        ldestroy_cb(updates, (void(*)(void*))free_update);
        updates = NULL;
    }
    if (extended_params) {
        ldestroy(extended_params);
        extended_params = NULL;
    }
    if (additional_config_files) {
        ldestroy(additional_config_files);
        additional_config_files = NULL;
    }

    cleanup_config();

    return status;
}

/*
 * main routine
 */
int avrdude_main(int argc, char * argv [], const char *sys_config)
{
  int              rc;          /* general return code checking */
  int              exitrc;      /* exit code for main() */
  int              i;           /* general loop counter */
  int              ch;          /* options flag */
  int              len;         /* length for various strings */
  struct avrpart * p;           /* which avr part we are programming */
  AVRMEM         * sig;         /* signature data */
  struct stat      sb;
  UPDATE         * upd;
  LNODEID        * ln;


  /* options / operating mode variables */
  int     erase;       /* 1=erase chip, 0=don't */
  int     calibrate;   /* 1=calibrate RC oscillator, 0=don't */
  char  * port;        /* device port (/dev/xxx) */
  int     terminal;    /* 1=enter terminal mode, 0=don't */
  int     verify;      /* perform a verify operation */
  char  * exitspecs;   /* exit specs string from command line */
  char  * programmer;  /* programmer id */
  char  * partdesc;    /* part id */
  // char    sys_config[PATH_MAX]; /* system wide config file */
  char    usr_config[PATH_MAX]; /* per-user config file */
  char  * e;           /* for strtol() error checking */
  int     baudrate;    /* override default programmer baud rate */
  double  bitclock;    /* Specify programmer bit clock (JTAG ICE) */
  int     ispdelay;    /* Specify the delay for ISP clock */
  int     safemode;    /* Enable safemode, 1=safemode on, 0=normal */
  int     silentsafe;  /* Don't ask about fuses, 1=silent, 0=normal */
  int     init_ok;     /* Device initialization worked well */
  int     is_open;     /* Device open succeeded */
  // char  * logfile;     /* Use logfile rather than stderr for diagnostics */
  enum updateflags uflags = UF_AUTO_ERASE; /* Flags for do_op() */
  unsigned char safemode_lfuse = 0xff;
  unsigned char safemode_hfuse = 0xff;
  unsigned char safemode_efuse = 0xff;
  unsigned char safemode_fuse = 0xff;

  char * safemode_response;
  int fuses_specified = 0;
  int fuses_updated = 0;
// #if !defined(WIN32NATIVE)
//   char  * homedir;
// #endif

  /*
   * Set line buffering for file descriptors so we see stdout and stderr
   * properly interleaved.
   */
  // setvbuf(stdout, (char*)NULL, _IOLBF, 0);
  // setvbuf(stderr, (char*)NULL, _IOLBF, 0);

  progname = strrchr(argv[0],'/');

  cancel_flag = false;

#if defined (WIN32NATIVE)
  /* take care of backslash as dir sep in W32 */
  if (!progname) progname = strrchr(argv[0],'\\');
#endif /* WIN32NATIVE */

  if (progname)
    progname++;
  else
    progname = argv[0];

  default_parallel[0] = 0;
  default_serial[0]   = 0;
  default_bitclock    = 0.0;
  default_safemode    = -1;

  init_config();

  // atexit(cleanup_main);

  updates = lcreat(NULL, 0);
  if (updates == NULL) {
    avrdude_message(MSG_INFO, "%s: cannot initialize updater list\n", progname);
    return cleanup_main(1);
  }

  extended_params = lcreat(NULL, 0);
  if (extended_params == NULL) {
    avrdude_message(MSG_INFO, "%s: cannot initialize extended parameter list\n", progname);
    return cleanup_main(1);
  }

  additional_config_files = lcreat(NULL, 0);
  if (additional_config_files == NULL) {
    avrdude_message(MSG_INFO, "%s: cannot initialize additional config files list\n", progname);
    return cleanup_main(1);
  }

  partdesc      = NULL;
  port          = NULL;
  erase         = 0;
  calibrate     = 0;
  p             = NULL;
  ovsigck       = 0;
  terminal      = 0;
  verify        = 1;        /* on by default */
  quell_progress = 0;
  exitspecs     = NULL;
  pgm           = NULL;
  programmer    = default_programmer;
  verbose       = 0;
  baudrate      = 0;
  bitclock      = 0.0;
  ispdelay      = 0;
  safemode      = 1;       /* Safemode on by default */
  silentsafe    = 0;       /* Ask by default */
  is_open       = 0;
  // logfile       = NULL;

// #if defined(WIN32NATIVE)

//   win_sys_config_set(sys_config);
//   win_usr_config_set(usr_config);

// #else

//   strcpy(sys_config, CONFIG_DIR);
//   i = strlen(sys_config);
//   if (i && (sys_config[i-1] != '/'))
//     strcat(sys_config, "/");
//   strcat(sys_config, "avrdude.conf");

  usr_config[0] = 0;
//   homedir = getenv("HOME");
//   if (homedir != NULL) {
//     strcpy(usr_config, homedir);
//     i = strlen(usr_config);
//     if (i && (usr_config[i-1] != '/'))
//       strcat(usr_config, "/");
//     strcat(usr_config, ".avrduderc");
//   }

// #endif

  len = strlen(progname) + 2;
  for (i=0; i<len; i++)
    progbuf[i] = ' ';
  progbuf[i] = 0;

  /*
   * check for no arguments
   */
  if (argc == 1) {
    usage();
    return 0;
  }


  /*
   * process command line arguments
   */
  optind = 1;    // Reset getopt, makes it possible to use it multiple times
  while ((ch = getopt(argc,argv,"?b:B:c:C:DeE:Fi:l:np:OP:qstU:uvVx:yY:")) != -1) {

    switch (ch) {
      case 'b': /* override default programmer baud rate */
        baudrate = strtol(optarg, &e, 0);
        if ((e == optarg) || (*e != 0)) {
          avrdude_message(MSG_INFO, "%s: invalid baud rate specified '%s'\n",
                  progname, optarg);
          return cleanup_main(1);
        }
        break;

      case 'B':	/* specify JTAG ICE bit clock period */
	bitclock = strtod(optarg, &e);
	if (*e != 0) {
	  /* trailing unit of measure present */
	  int suffixlen = strlen(e);
	  switch (suffixlen) {
	  case 2:
	    if ((e[0] != 'h' && e[0] != 'H') || e[1] != 'z')
	      bitclock = 0.0;
	    else
	      /* convert from Hz to microseconds */
	      bitclock = 1E6 / bitclock;
	    break;

	  case 3:
	    if ((e[1] != 'h' && e[1] != 'H') || e[2] != 'z')
	      bitclock = 0.0;
	    else {
	      switch (e[0]) {
	      case 'M':
	      case 'm':		/* no Millihertz here :) */
		bitclock = 1.0 / bitclock;
		break;

	      case 'k':
		bitclock = 1E3 / bitclock;
		break;

	      default:
		bitclock = 0.0;
		break;
	      }
	    }
	    break;

	  default:
	    bitclock = 0.0;
	    break;
	  }
	  if (bitclock == 0.0)
	    avrdude_message(MSG_INFO, "%s: invalid bit clock unit of measure '%s'\n",
			    progname, e);
	}
	if ((e == optarg) || bitclock == 0.0) {
	  avrdude_message(MSG_INFO, "%s: invalid bit clock period specified '%s'\n",
                  progname, optarg);
          return cleanup_main(1);
        }
        break;

      case 'i':	/* specify isp clock delay */
	ispdelay = strtol(optarg, &e,10);
	if ((e == optarg) || (*e != 0) || ispdelay == 0) {
	  avrdude_message(MSG_INFO, "%s: invalid isp clock delay specified '%s'\n",
                  progname, optarg);
          return cleanup_main(1);
        }
        break;

      case 'c': /* programmer id */
        programmer = optarg;
        break;

      // case 'C': /* system wide configuration file */
      //   if (optarg[0] == '+') {
      //     ladd(additional_config_files, optarg+1);
      //   } else {
      //     strncpy(sys_config, optarg, PATH_MAX);
      //     sys_config[PATH_MAX-1] = 0;
      //   }
      //   break;

      case 'D': /* disable auto erase */
        uflags &= ~UF_AUTO_ERASE;
        break;

      case 'e': /* perform a chip erase */
        erase = 1;
        uflags &= ~UF_AUTO_ERASE;
        break;

      case 'E':
        exitspecs = optarg;
        break;

      case 'F': /* override invalid signature check */
        ovsigck = 1;
        break;

  //     case 'l':
	// logfile = optarg;
	// break;

      case 'n':
        uflags |= UF_NOWRITE;
        break;

      case 'O': /* perform RC oscillator calibration */
	calibrate = 1;
	break;

      case 'p' : /* specify AVR part */
        partdesc = optarg;
        break;

      case 'P':
        port = optarg;
        break;

      case 'q' : /* Quell progress output */
        quell_progress++ ;
        break;

      case 's' : /* Silent safemode */
        silentsafe = 1;
        safemode = 1;
        break;
        
      case 't': /* enter terminal mode */
        terminal = 1;
        break;

      case 'u' : /* Disable safemode */
        safemode = 0;
        break;

      case 'U':
        upd = parse_op(optarg);
        if (upd == NULL) {
          avrdude_message(MSG_INFO, "%s: error parsing update operation '%s'\n",
                  progname, optarg);
          return cleanup_main(1);
        }
        ladd(updates, upd);

        if (verify && upd->op == DEVICE_WRITE) {
          upd = dup_update(upd);
          upd->op = DEVICE_VERIFY;
          ladd(updates, upd);
        }
        break;

      case 'v':
        verbose++;
        break;

      case 'V':
        verify = 0;
        break;

      case 'x':
        ladd(extended_params, optarg);
        break;

      case 'y':
        avrdude_message(MSG_INFO, "%s: erase cycle counter no longer supported\n",
                progname);
        break;

      case 'Y':
        avrdude_message(MSG_INFO, "%s: erase cycle counter no longer supported\n",
                progname);
        break;

      case '?': /* help */
        usage();
        return cleanup_main(0);
        break;

      default:
        avrdude_message(MSG_INFO, "%s: invalid option -%c\n\n", progname, ch);
        usage();
        return cleanup_main(1);
        break;
    }

  }

  // if (logfile != NULL) {
  //   FILE *newstderr = freopen(logfile, "w", stderr);
  //   if (newstderr == NULL) {
  //     /* Help!  There's no stderr to complain to anymore now. */
  //     printf("Cannot create logfile \"%s\": %s\n",
	//      logfile, strerror(errno));
  //     return 1;
  //   }
  // }

  if (quell_progress == 0) {
    // if (isatty (STDERR_FILENO))
    //   update_progress = update_progress_tty;
    // else {
    //   update_progress = update_progress_no_tty;
    //   /* disable all buffering of stderr for compatibility with
    //      software that captures and redirects output to a GUI
    //      i.e. Programmers Notepad */
    //   setvbuf( stderr, NULL, _IONBF, 0 );
    //   setvbuf( stdout, NULL, _IONBF, 0 );
    // }
    update_progress = update_progress_no_tty;
  }

  /*
   * Print out an identifying string so folks can tell what version
   * they are running
   */
  avrdude_message(MSG_NOTICE, "\n%s: Version %s, compiled on %s at %s\n"
                    "%sCopyright (c) 2000-2005 Brian Dean, http://www.bdmicro.com/\n"
                    "%sCopyright (c) 2007-2014 Joerg Wunsch\n\n",
                    progname, version, __DATE__, __TIME__, progbuf, progbuf);
  avrdude_message(MSG_NOTICE, "%sSystem wide configuration file is \"%s\"\n",
            progbuf, sys_config);

  rc = read_config(sys_config);
  if (rc) {
    avrdude_message(MSG_INFO, "%s: error reading system wide configuration file \"%s\"\n",
                    progname, sys_config);
    return cleanup_main(1);
  }

  if (usr_config[0] != 0) {
    avrdude_message(MSG_NOTICE, "%sUser configuration file is \"%s\"\n",
              progbuf, usr_config);

    rc = stat(usr_config, &sb);
    if ((rc < 0) || ((sb.st_mode & S_IFREG) == 0)) {
      avrdude_message(MSG_NOTICE, "%sUser configuration file does not exist or is not a "
                      "regular file, skipping\n",
                      progbuf);
    }
    else {
      rc = read_config(usr_config);
      if (rc) {
        avrdude_message(MSG_INFO, "%s: error reading user configuration file \"%s\"\n",
                progname, usr_config);
        return cleanup_main(1);
      }
    }
  }

  if (lsize(additional_config_files) > 0) {
    LNODEID ln1;
    const char * p = NULL;

    for (ln1=lfirst(additional_config_files); ln1; ln1=lnext(ln1)) {
      p = ldata(ln1);
      avrdude_message(MSG_NOTICE, "%sAdditional configuration file is \"%s\"\n",
                      progbuf, p);

      rc = read_config(p);
      if (rc) {
        avrdude_message(MSG_INFO, "%s: error reading additional configuration file \"%s\"\n",
                        progname, p);
        return cleanup_main(1);
      }
    }
  }

  // set bitclock from configuration files unless changed by command line
  if (default_bitclock > 0 && bitclock == 0.0) {
    bitclock = default_bitclock;
  }

  avrdude_message(MSG_NOTICE, "\n");

  if (partdesc) {
    if (strcmp(partdesc, "?") == 0) {
      avrdude_message(MSG_INFO, "\n");
      avrdude_message(MSG_INFO, "Valid parts are:\n");
      list_parts(stderr, "  ", part_list);
      avrdude_message(MSG_INFO, "\n");
      return cleanup_main(1);
    }
  }

  if (programmer) {
    if (strcmp(programmer, "?") == 0) {
      avrdude_message(MSG_INFO, "\n");
      avrdude_message(MSG_INFO, "Valid programmers are:\n");
      list_programmers(stderr, "  ", programmers);
      avrdude_message(MSG_INFO, "\n");
      return cleanup_main(1);
    }
    if (strcmp(programmer, "?type") == 0) {
      avrdude_message(MSG_INFO, "\n");
      avrdude_message(MSG_INFO, "Valid programmer types are:\n");
      list_programmer_types(stderr, "  ");
      avrdude_message(MSG_INFO, "\n");
      return cleanup_main(1);
    }
  }


  if (programmer[0] == 0) {
    avrdude_message(MSG_INFO, "\n%s: no programmer has been specified on the command line "
                    "or the config file\n",
                    progname);
    avrdude_message(MSG_INFO, "%sSpecify a programmer using the -c option and try again\n\n",
                    progbuf);
    return cleanup_main(1);
  }

  pgm = locate_programmer(programmers, programmer);
  if (pgm == NULL) {
    avrdude_message(MSG_INFO, "\n");
    avrdude_message(MSG_INFO, "%s: Can't find programmer id \"%s\"\n",
                    progname, programmer);
    avrdude_message(MSG_INFO, "\nValid programmers are:\n");
    list_programmers(stderr, "  ", programmers);
    avrdude_message(MSG_INFO, "\n");
    return cleanup_main(1);
  }

  if (pgm->initpgm) {
    pgm->initpgm(pgm);
  } else {
    avrdude_message(MSG_INFO, "\n%s: Can't initialize the programmer.\n\n",
                    progname);
    return cleanup_main(1);
  }

  if (pgm->setup) {
    pgm->setup(pgm);
  }
  pgm_setup = true;   // Replaces the atexit hook
  // if (pgm->teardown) {
  //   atexit(exithook);
  // }

  if (lsize(extended_params) > 0) {
    if (pgm->parseextparams == NULL) {
      avrdude_message(MSG_INFO, "%s: WARNING: Programmer doesn't support extended parameters,"
                      " -x option(s) ignored\n",
                      progname);
    } else {
      if (pgm->parseextparams(pgm, extended_params) < 0) {
        avrdude_message(MSG_INFO, "%s: Error parsing extended parameter list\n",
                        progname);
        return cleanup_main(1);
      }
    }
  }

  if (port == NULL) {
    switch (pgm->conntype)
    {
      case CONNTYPE_PARALLEL:
        port = default_parallel;
        break;

      case CONNTYPE_SERIAL:
        port = default_serial;
        break;

      case CONNTYPE_USB:
        port = DEFAULT_USB;
        break;
    }
  }

  if (partdesc == NULL) {
    avrdude_message(MSG_INFO, "%s: No AVR part has been specified, use \"-p Part\"\n\n",
                    progname);
    avrdude_message(MSG_INFO, "Valid parts are:\n");
    list_parts(stderr, "  ", part_list);
    avrdude_message(MSG_INFO, "\n");
    return cleanup_main(1);
  }


  p = locate_part(part_list, partdesc);
  if (p == NULL) {
    avrdude_message(MSG_INFO, "%s: AVR Part \"%s\" not found.\n\n",
                    progname, partdesc);
    avrdude_message(MSG_INFO, "Valid parts are:\n");
    list_parts(stderr, "  ", part_list);
    avrdude_message(MSG_INFO, "\n");
    return cleanup_main(1);
  }


  if (exitspecs != NULL) {
    if (pgm->parseexitspecs == NULL) {
      avrdude_message(MSG_INFO, "%s: WARNING: -E option not supported by this programmer type\n",
                      progname);
      exitspecs = NULL;
    }
    else if (pgm->parseexitspecs(pgm, exitspecs) < 0) {
      usage();
      return cleanup_main(1);
    }
  }

  if (default_safemode == 0) {
    /* configuration disables safemode: revert meaning of -u */
    if (safemode == 0)
      /* -u was given: enable safemode */
      safemode = 1;
    else
      /* -u not given: turn off */
      safemode = 0;
  }

  if (isatty(STDIN_FILENO) == 0 && silentsafe == 0)
    safemode  = 0;       /* Turn off safemode if this isn't a terminal */


  if(p->flags & AVRPART_AVR32) {
    safemode = 0;
  }

  if(p->flags & (AVRPART_HAS_PDI | AVRPART_HAS_TPI)) {
    safemode = 0;
  }


  if (avr_initmem(p) != 0)
  {
    avrdude_message(MSG_INFO, "\n%s: failed to initialize memories\n",
            progname);
    return cleanup_main(1);
  }

  /*
   * Now that we know which part we are going to program, locate any
   * -U options using the default memory region, and fill in the
   * device-dependent default region name, either "application" (for
   * Xmega devices), or "flash" (everything else).
   */
  for (ln=lfirst(updates); ln; ln=lnext(ln)) {
    upd = ldata(ln);
    if (upd->memtype == NULL) {
      const char *mtype = (p->flags & AVRPART_HAS_PDI)? "application": "flash";
      avrdude_message(MSG_NOTICE2, "%s: defaulting memtype in -U %c:%s option to \"%s\"\n",
                      progname,
                      (upd->op == DEVICE_READ)? 'r': (upd->op == DEVICE_WRITE)? 'w': 'v',
                      upd->filename, mtype);
      if ((upd->memtype = strdup(mtype)) == NULL) {
        avrdude_message(MSG_INFO, "%s: out of memory\n", progname);
        return cleanup_main(1);
      }
    }
  }

  /*
   * open the programmer
   */
  if (port[0] == 0) {
    avrdude_message(MSG_INFO, "\n%s: no port has been specified on the command line "
            "or the config file\n",
            progname);
    avrdude_message(MSG_INFO, "%sSpecify a port using the -P option and try again\n\n",
            progbuf);
    return cleanup_main(1);
  }

  if (verbose) {
    avrdude_message(MSG_NOTICE, "%sUsing Port                    : %s\n", progbuf, port);
    avrdude_message(MSG_NOTICE, "%sUsing Programmer              : %s\n", progbuf, programmer);
    if ((strcmp(pgm->type, "avr910") == 0)) {
	  avrdude_message(MSG_NOTICE, "%savr910_devcode (avrdude.conf) : ", progbuf);
      if(p->avr910_devcode)avrdude_message(MSG_INFO, "0x%x\n", p->avr910_devcode);
	  else avrdude_message(MSG_NOTICE, "none\n");
    }
  }

  if (baudrate != 0) {
    avrdude_message(MSG_NOTICE, "%sOverriding Baud Rate          : %d\n", progbuf, baudrate);
    pgm->baudrate = baudrate;
  }

  if (bitclock != 0.0) {
    avrdude_message(MSG_NOTICE, "%sSetting bit clk period        : %.1f\n", progbuf, bitclock);
    pgm->bitclock = bitclock * 1e-6;
  }

  if (ispdelay != 0) {
    avrdude_message(MSG_NOTICE, "%sSetting isp clock delay        : %3i\n", progbuf, ispdelay);
    pgm->ispdelay = ispdelay;
  }

  rc = pgm->open(pgm, port);
  if (rc < 0) {
    exitrc = 1;
    pgm->ppidata = 0; /* clear all bits at exit */
    goto main_exit;
  }
  is_open = 1;

  if (calibrate) {
    /*
     * perform an RC oscillator calibration
     * as outlined in appnote AVR053
     */
    if (pgm->perform_osccal == 0) {
      avrdude_message(MSG_INFO, "%s: programmer does not support RC oscillator calibration\n",
                      progname);
      exitrc = 1;
    } else {
      avrdude_message(MSG_INFO, "%s: performing RC oscillator calibration\n", progname);
      exitrc = pgm->perform_osccal(pgm);
    }
    if (exitrc == 0 && quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: calibration value is now stored in EEPROM at address 0\n",
                      progname);
    }
    goto main_exit;
  }

  if (verbose) {
    avr_display(stderr, p, progbuf, verbose);
    avrdude_message(MSG_NOTICE, "\n");
    programmer_display(pgm, progbuf);
  }

  if (quell_progress < 2) {
    avrdude_message(MSG_INFO, "\n");
  }

  exitrc = 0;

  /*
   * enable the programmer
   */
  pgm->enable(pgm);

  /*
   * turn off all the status leds
   */
  pgm->rdy_led(pgm, OFF);
  pgm->err_led(pgm, OFF);
  pgm->pgm_led(pgm, OFF);
  pgm->vfy_led(pgm, OFF);

  /*
   * initialize the chip in preperation for accepting commands
   */
  init_ok = (rc = pgm->initialize(pgm, p)) >= 0;
  if (!init_ok) {
    avrdude_message(MSG_INFO, "%s: initialization failed, rc=%d\n", progname, rc);
    if (!ovsigck) {
      avrdude_message(MSG_INFO, "%sDouble check connections and try again, "
              "or use -F to override\n"
              "%sthis check.\n\n",
              progbuf, progbuf);
      exitrc = 1;
      goto main_exit;
    }
  }

  /* indicate ready */
  pgm->rdy_led(pgm, ON);

  if (quell_progress < 2) {
    avrdude_message(MSG_INFO, "%s: AVR device initialized and ready to accept instructions\n",
                    progname);
  }

  /*
   * Let's read the signature bytes to make sure there is at least a
   * chip on the other end that is responding correctly.  A check
   * against 0xffffff / 0x000000 should ensure that the signature bytes
   * are valid.
   */
  if(!(p->flags & AVRPART_AVR32)) {
    int attempt = 0;
    int waittime = 10000;       /* 10 ms */

  sig_again:
    usleep(waittime);
    if (init_ok) {
      rc = avr_signature(pgm, p);
      if (rc != 0) {
        avrdude_message(MSG_INFO, "%s: error reading signature data, rc=%d\n",
          progname, rc);
        exitrc = 1;
        goto main_exit;
      }
    }
  
    sig = avr_locate_mem(p, "signature");
    if (sig == NULL) {
      avrdude_message(MSG_INFO, "%s: WARNING: signature data not defined for device \"%s\"\n",
                      progname, p->desc);
    }

    if (sig != NULL) {
      int ff, zz;

      if (quell_progress < 2) {
        avrdude_message(MSG_INFO, "%s: Device signature = 0x", progname);
      }
      ff = zz = 1;
      for (i=0; i<sig->size; i++) {
        if (quell_progress < 2) {
          avrdude_message(MSG_INFO, "%02x", sig->buf[i]);
        }
        if (sig->buf[i] != 0xff)
          ff = 0;
        if (sig->buf[i] != 0x00)
          zz = 0;
      }
      if (quell_progress < 2) {
        AVRPART * part;

        part = locate_part_by_signature(part_list, sig->buf, sig->size);
        if (part) {
          avrdude_message(MSG_INFO, " (probably %s)", part->id);
        }
      }
      if (ff || zz) {
        if (++attempt < 3) {
          waittime *= 5;
          if (quell_progress < 2) {
              avrdude_message(MSG_INFO, " (retrying)\n");
          }
          goto sig_again;
        }
        if (quell_progress < 2) {
            avrdude_message(MSG_INFO, "\n");
        }
        avrdude_message(MSG_INFO, "%s: Yikes!  Invalid device signature.\n", progname);
        if (!ovsigck) {
          avrdude_message(MSG_INFO, "%sDouble check connections and try again, "
                  "or use -F to override\n"
                  "%sthis check.\n\n",
                  progbuf, progbuf);
          exitrc = 1;
          goto main_exit;
        }
      } else {
        if (quell_progress < 2) {
          avrdude_message(MSG_INFO, "\n");
        }
      }

      if (sig->size != 3 ||
          sig->buf[0] != p->signature[0] ||
          sig->buf[1] != p->signature[1] ||
          sig->buf[2] != p->signature[2]) {
        avrdude_message(MSG_INFO, "%s: Expected signature for %s is %02X %02X %02X\n",
                        progname, p->desc,
                        p->signature[0], p->signature[1], p->signature[2]);
        if (!ovsigck) {
          avrdude_message(MSG_INFO, "%sDouble check chip, "
                  "or use -F to override this check.\n",
                  progbuf);
          exitrc = 1;
          goto main_exit;
        }
      }
    }
  }

  if (init_ok && safemode == 1) {
    /* If safemode is enabled, go ahead and read the current low, high,
       and extended fuse bytes as needed */

    rc = safemode_readfuses(&safemode_lfuse, &safemode_hfuse,
                           &safemode_efuse, &safemode_fuse, pgm, p);

    if (rc != 0) {

	  //Check if the programmer just doesn't support reading
  	  if (rc == -5)
			{
				avrdude_message(MSG_NOTICE, "%s: safemode: Fuse reading not support by programmer.\n"
						"              Safemode disabled.\n", progname);
			}
      else
			{

      		avrdude_message(MSG_INFO, "%s: safemode: To protect your AVR the programming "
            				    "will be aborted\n",
               					 progname);
      		exitrc = 1;
		    goto main_exit;
			}
    } else {
      //Save the fuses as default
      safemode_memfuses(1, &safemode_lfuse, &safemode_hfuse, &safemode_efuse, &safemode_fuse);
    }
  }

  if (uflags & UF_AUTO_ERASE) {
    if ((p->flags & AVRPART_HAS_PDI) && pgm->page_erase != NULL &&
        lsize(updates) > 0) {
      if (quell_progress < 2) {
        avrdude_message(MSG_INFO, "%s: NOTE: Programmer supports page erase for Xmega devices.\n"
                        "%sEach page will be erased before programming it, but no chip erase is performed.\n"
                        "%sTo disable page erases, specify the -D option; for a chip-erase, use the -e option.\n",
                        progname, progbuf, progbuf);
      }
    } else {
      AVRMEM * m;
      const char *memname = (p->flags & AVRPART_HAS_PDI)? "application": "flash";

      uflags &= ~UF_AUTO_ERASE;
      for (ln=lfirst(updates); ln; ln=lnext(ln)) {
        upd = ldata(ln);
        m = avr_locate_mem(p, upd->memtype);
        if (m == NULL)
          continue;
        if ((strcasecmp(m->desc, memname) == 0) && (upd->op == DEVICE_WRITE)) {
          erase = 1;
          if (quell_progress < 2) {
            avrdude_message(MSG_INFO, "%s: NOTE: \"%s\" memory has been specified, an erase cycle "
                            "will be performed\n"
                            "%sTo disable this feature, specify the -D option.\n",
                            progname, memname, progbuf);
          }
          break;
        }
      }
    }
  }

  if (init_ok && erase) {
    /*
     * erase the chip's flash and eeprom memories, this is required
     * before the chip can accept new programming
     */
    if (uflags & UF_NOWRITE) {
      avrdude_message(MSG_INFO, "%s: conflicting -e and -n options specified, NOT erasing chip\n",
                      progname);
    } else {
      if (quell_progress < 2) {
      	avrdude_message(MSG_INFO, "%s: erasing chip\n", progname);
      }
      exitrc = avr_chip_erase(pgm, p);
      if(exitrc) goto main_exit;
    }
  }

  if (terminal) {
    /*
     * terminal mode
     */
    exitrc = terminal_mode(pgm, p);
  }

  if (!init_ok) {
    /*
     * If we came here by the -tF options, bail out now.
     */
    exitrc = 1;
    goto main_exit;
  }


  for (ln=lfirst(updates); ln; ln=lnext(ln)) {
    upd = ldata(ln);
    rc = do_op(pgm, p, upd, uflags);
    if (rc) {
      exitrc = 1;
      break;
    }
  }

  /* Right before we exit programming mode, which will make the fuse
     bits active, check to make sure they are still correct */
  if (safemode == 1) {
    /* If safemode is enabled, go ahead and read the current low,
     * high, and extended fuse bytes as needed */
    unsigned char safemodeafter_lfuse = 0xff;
    unsigned char safemodeafter_hfuse = 0xff;
    unsigned char safemodeafter_efuse = 0xff;
    unsigned char safemodeafter_fuse  = 0xff;
    unsigned char failures = 0;
    char yes[1] = {'y'};

    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "\n");
    }

    //Restore the default fuse values
    safemode_memfuses(0, &safemode_lfuse, &safemode_hfuse, &safemode_efuse, &safemode_fuse);

    /* Try reading back fuses, make sure they are reliable to read back */
    if (safemode_readfuses(&safemodeafter_lfuse, &safemodeafter_hfuse,
                           &safemodeafter_efuse, &safemodeafter_fuse, pgm, p) != 0) {
      /* Uh-oh.. try once more to read back fuses */
      if (safemode_readfuses(&safemodeafter_lfuse, &safemodeafter_hfuse,
                             &safemodeafter_efuse, &safemodeafter_fuse, pgm, p) != 0) {
        avrdude_message(MSG_INFO, "%s: safemode: Sorry, reading back fuses was unreliable. "
                        "I have given up and exited programming mode\n",
                        progname);
        exitrc = 1;
        goto main_exit;        
      }
    }
    
    /* Now check what fuses are against what they should be */
    if (safemodeafter_fuse != safemode_fuse) {
      fuses_updated = 1;
      avrdude_message(MSG_INFO, "%s: safemode: fuse changed! Was %x, and is now %x\n",
              progname, safemode_fuse, safemodeafter_fuse);

              
      /* Ask user - should we change them */
       
       if (silentsafe == 0)
            safemode_response = terminal_get_input("Would you like this fuse to be changed back? [y/n] ");
       else
            safemode_response = yes;
       
       if (tolower((int)(safemode_response[0])) == 'y') {
              
            /* Enough chit-chat, time to program some fuses and check them */
            if (safemode_writefuse (safemode_fuse, "fuse", pgm, p,
                                    10) == 0) {
                avrdude_message(MSG_INFO, "%s: safemode: and is now rescued\n", progname);
            }
            else {
                avrdude_message(MSG_INFO, "%s: and COULD NOT be changed\n", progname);
                failures++;
            }
      }
    }

    /* Now check what fuses are against what they should be */
    if (safemodeafter_lfuse != safemode_lfuse) {
      fuses_updated = 1;
      avrdude_message(MSG_INFO, "%s: safemode: lfuse changed! Was %x, and is now %x\n",
              progname, safemode_lfuse, safemodeafter_lfuse);

              
      /* Ask user - should we change them */
       
       if (silentsafe == 0)
            safemode_response = terminal_get_input("Would you like this fuse to be changed back? [y/n] ");
       else
            safemode_response = yes;
       
       if (tolower((int)(safemode_response[0])) == 'y') {
              
            /* Enough chit-chat, time to program some fuses and check them */
            if (safemode_writefuse (safemode_lfuse, "lfuse", pgm, p,
                                    10) == 0) {
                avrdude_message(MSG_INFO, "%s: safemode: and is now rescued\n", progname);
            }
            else {
                avrdude_message(MSG_INFO, "%s: and COULD NOT be changed\n", progname);
                failures++;
            }
      }
    }

    /* Now check what fuses are against what they should be */
    if (safemodeafter_hfuse != safemode_hfuse) {
      fuses_updated = 1;
      avrdude_message(MSG_INFO, "%s: safemode: hfuse changed! Was %x, and is now %x\n",
              progname, safemode_hfuse, safemodeafter_hfuse);
              
      /* Ask user - should we change them */
       if (silentsafe == 0)
            safemode_response = terminal_get_input("Would you like this fuse to be changed back? [y/n] ");
       else
            safemode_response = yes;
       if (tolower((int)(safemode_response[0])) == 'y') {

            /* Enough chit-chat, time to program some fuses and check them */
            if (safemode_writefuse(safemode_hfuse, "hfuse", pgm, p,
                                    10) == 0) {
                avrdude_message(MSG_INFO, "%s: safemode: and is now rescued\n", progname);
            }
            else {
                avrdude_message(MSG_INFO, "%s: and COULD NOT be changed\n", progname);
                failures++;
            }
      }
    }

    /* Now check what fuses are against what they should be */
    if (safemodeafter_efuse != safemode_efuse) {
      fuses_updated = 1;
      avrdude_message(MSG_INFO, "%s: safemode: efuse changed! Was %x, and is now %x\n",
              progname, safemode_efuse, safemodeafter_efuse);

      /* Ask user - should we change them */
       if (silentsafe == 0)
            safemode_response = terminal_get_input("Would you like this fuse to be changed back? [y/n] ");
       else
            safemode_response = yes;
       if (tolower((int)(safemode_response[0])) == 'y') {
              
            /* Enough chit-chat, time to program some fuses and check them */
            if (safemode_writefuse (safemode_efuse, "efuse", pgm, p,
                                    10) == 0) {
                avrdude_message(MSG_INFO, "%s: safemode: and is now rescued\n", progname);
            }
            else {
                avrdude_message(MSG_INFO, "%s: and COULD NOT be changed\n", progname);
                failures++;
            }
       }
    }

    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: safemode: ", progname);
      if (failures == 0) {
        avrdude_message(MSG_INFO, "Fuses OK (E:%02X, H:%02X, L:%02X)\n",
                safemode_efuse, safemode_hfuse, safemode_lfuse);
      }
      else {
        avrdude_message(MSG_INFO, "Fuses not recovered, sorry\n");
      }
    }

    if (fuses_updated && fuses_specified) {
      exitrc = 1;
    }

  }


main_exit:

  /*
   * program complete
   */

  if (is_open) {
    pgm->powerdown(pgm);

    pgm->disable(pgm);

    pgm->rdy_led(pgm, OFF);

    pgm->close(pgm);
  }

  if (quell_progress < 2) {
    avrdude_message(MSG_INFO, "\n%s done.  Thank you.\n\n", progname);
  }

  return cleanup_main(exitrc);
}
