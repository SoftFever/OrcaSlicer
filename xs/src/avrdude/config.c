/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000-2004  Brian S. Dean <bsd@bsdhome.com>
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "avrdude.h"
#include "libavrdude.h"
#include "config.h"

#include "config_gram.h"

char default_programmer[MAX_STR_CONST];
char default_parallel[PATH_MAX];
char default_serial[PATH_MAX];
double default_bitclock;
int default_safemode;

char string_buf[MAX_STR_CONST];
char *string_buf_ptr;

LISTID       string_list;
LISTID       number_list;
PROGRAMMER * current_prog;
AVRPART    * current_part;
AVRMEM     * current_mem;
LISTID       part_list;
LISTID       programmers;

int    lineno;
const char * infile;

extern char * yytext;

#define DEBUG 0

void cleanup_config(void)
{
  ldestroy_cb(part_list, (void(*)(void*))avr_free_part);
  ldestroy_cb(programmers, (void(*)(void*))pgm_free);
  ldestroy_cb(string_list, (void(*)(void*))free_token);
  ldestroy_cb(number_list, (void(*)(void*))free_token);
}

int init_config(void)
{
  string_list  = lcreat(NULL, 0);
  number_list  = lcreat(NULL, 0);
  current_prog = NULL;
  current_part = NULL;
  current_mem  = NULL;
  part_list    = lcreat(NULL, 0);
  programmers  = lcreat(NULL, 0);

  lineno       = 1;
  infile       = NULL;

  return 0;
}



int yywrap()
{
  return 1;
}


int yyerror(char * errmsg, ...)
{
  va_list args;

  char message[512];

  va_start(args, errmsg);

  vsnprintf(message, sizeof(message), errmsg, args);
  avrdude_message(MSG_INFO, "%s: error at %s:%d: %s\n", progname, infile, lineno, message);

  va_end(args);

  return 0;
}


int yywarning(char * errmsg, ...)
{
  va_list args;

  char message[512];

  va_start(args, errmsg);

  vsnprintf(message, sizeof(message), errmsg, args);
  avrdude_message(MSG_INFO, "%s: warning at %s:%d: %s\n", progname, infile, lineno, message);

  va_end(args);

  return 0;
}


TOKEN * new_token(int primary)
{
  TOKEN * tkn;

  tkn = (TOKEN *)malloc(sizeof(TOKEN));
  if (tkn == NULL) {
    yyerror("new_token(): out of memory");
    return NULL;
  }

  memset(tkn, 0, sizeof(TOKEN));

  tkn->primary = primary;

  return tkn;
}


void free_token(TOKEN * tkn)
{
  if (tkn) {
    switch (tkn->value.type) {
      case V_STR:
        if (tkn->value.string)
          free(tkn->value.string);
        tkn->value.string = NULL;
        break;
    }

    free(tkn);
  }
}


void free_tokens(int n, ...)
{
  TOKEN * t;
  va_list ap;

  va_start(ap, n);
  while (n--) {
    t = va_arg(ap, TOKEN *);
    free_token(t);
  }
  va_end(ap);
}



TOKEN * number(char * text)
{
  struct token_t * tkn;

  tkn = new_token(TKN_NUMBER);
  if (tkn == NULL) {
      return NULL; /* yyerror already called */
  }
  tkn->value.type   = V_NUM;
  tkn->value.number = atoi(text);

#if DEBUG
  avrdude_message(MSG_INFO, "NUMBER(%d)\n", tkn->value.number);
#endif

  return tkn;
}

TOKEN * number_real(char * text)
{
  struct token_t * tkn;

  tkn = new_token(TKN_NUMBER);
  tkn->value.type   = V_NUM_REAL;
  tkn->value.number_real = atof(text);

#if DEBUG
  avrdude_message(MSG_INFO, "NUMBER(%g)\n", tkn->value.number_real);
#endif

  return tkn;
}

TOKEN * hexnumber(char * text)
{
  struct token_t * tkn;
  char * e;

  tkn = new_token(TKN_NUMBER);
  if (tkn == NULL) {
      return NULL; /* yyerror already called */
  }
  tkn->value.type   = V_NUM;
  tkn->value.number = strtoul(text, &e, 16);
  if ((e == text) || (*e != 0)) {
    yyerror("can't scan hex number \"%s\"", text);
    return NULL;
  }
  
#if DEBUG
  avrdude_message(MSG_INFO, "HEXNUMBER(%g)\n", tkn->value.number);
#endif

  return tkn;
}


TOKEN * string(char * text)
{
  struct token_t * tkn;
  int len;

  tkn = new_token(TKN_STRING);
  if (tkn == NULL) {
      return NULL; /* yyerror already called */
  }

  len = strlen(text);

  tkn->value.type   = V_STR;
  tkn->value.string = (char *) malloc(len+1);
  if (tkn->value.string == NULL) {
    yyerror("string(): out of memory");
    return NULL;
  }
  strcpy(tkn->value.string, text);

#if DEBUG
  avrdude_message(MSG_INFO, "STRING(%s)\n", tkn->value.string);
#endif

  return tkn;
}


TOKEN * keyword(int primary)
{
  struct token_t * tkn;

  tkn = new_token(primary);

  return tkn;
}


void print_token(TOKEN * tkn)
{
  if (!tkn)
    return;

  avrdude_message(MSG_INFO, "token = %d = ", tkn->primary);
  switch (tkn->value.type) {
    case V_NUM:
      avrdude_message(MSG_INFO, "NUMBER, value=%d", tkn->value.number);
      break;

    case V_NUM_REAL:
      avrdude_message(MSG_INFO, "NUMBER, value=%g", tkn->value.number_real);
      break;

    case V_STR:
      avrdude_message(MSG_INFO, "STRING, value=%s", tkn->value.string);
      break;

    default:
      avrdude_message(MSG_INFO, "<other>");
      break;
  }

  avrdude_message(MSG_INFO, "\n");
}


void pyytext(void)
{
#if DEBUG
  avrdude_message(MSG_INFO, "TOKEN: \"%s\"\n", yytext);
#endif
}


char * dup_string(const char * str)
{
  char * s;

  s = strdup(str);
  if (s == NULL) {
    yyerror("dup_string(): out of memory");
    return NULL;
  }

  return s;
}

#ifdef HAVE_YYLEX_DESTROY
/* reset lexer and free any allocated memory */
extern int yylex_destroy(void);
#endif

int read_config(const char * file)
{
  FILE * f;
  int r;

  f = fopen(file, "r");
  if (f == NULL) {
    avrdude_message(MSG_INFO, "%s: can't open config file \"%s\": %s\n",
            progname, file, strerror(errno));
    return -1;
  }

  lineno = 1;
  infile = file;
  yyin   = f;

  r = yyparse();

#ifdef HAVE_YYLEX_DESTROY
  /* reset lexer and free any allocated memory */
  yylex_destroy();
#endif

  fclose(f);

  return r;
}
