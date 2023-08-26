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

/* These are the internal definitions needed for config parsing */

#ifndef config_h
#define config_h

#include "libavrdude.h"


#define MAX_STR_CONST 1024

enum { V_NONE, V_NUM, V_NUM_REAL, V_STR };
typedef struct value_t {
  int      type;
  /*union { TODO: use an anonymous union here ? */
    int      number;
    double   number_real;
    char   * string;
  /*};*/
} VALUE;


typedef struct token_t {
  int primary;
  VALUE value;
} TOKEN;
typedef struct token_t *token_p;


extern FILE       * yyin;
extern PROGRAMMER * current_prog;
extern AVRPART    * current_part;
extern AVRMEM     * current_mem;
extern int          lineno;
extern const char * infile;
extern LISTID       string_list;
extern LISTID       number_list;


#if !defined(HAS_YYSTYPE)
#define YYSTYPE token_p
#endif
extern YYSTYPE yylval;

extern char string_buf[MAX_STR_CONST];
extern char *string_buf_ptr;

#ifdef __cplusplus
extern "C" {
#endif

int yyparse(void);

int yyerror(char * errmsg, ...);

int yywarning(char * errmsg, ...);

TOKEN * new_token(int primary);

void free_token(TOKEN * tkn);

void free_tokens(int n, ...);

TOKEN * number(char * text);

TOKEN * number_real(char * text);

TOKEN * hexnumber(char * text);

TOKEN * string(char * text);

TOKEN * keyword(int primary);

void print_token(TOKEN * tkn);

void pyytext(void);

char * dup_string(const char * str);

#ifdef __cplusplus
}
#endif

#endif
