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
/* need this for the call to atof() below */
#include <math.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "avrdude.h"
#include "libavrdude.h"
#include "config.h"

#include "config_gram.h"

#ifndef YYERRCODE
#define YYERRCODE 256
#endif

%}

DIGIT    [0-9]
HEXDIGIT [0-9a-fA-F]
SIGN     [+-]

%x strng
%x incl
%x comment
%option nounput

/* Bump resources for classic lex. */
%e2000
%p10000
%n1000

%%

#{SIGN}*{DIGIT}+            { yylval = number(yytext); return TKN_NUMBER; }
#{SIGN}*{DIGIT}+"."{DIGIT}* { yylval = number_real(yytext); return TKN_NUMBER_REAL; }
#{SIGN}*"."{DIGIT}*         { yylval = number_real(yytext); return TKN_NUMBER_REAL; }
{DIGIT}+            { yylval = number(yytext); return TKN_NUMBER; }
{DIGIT}+"."{DIGIT}* { yylval = number_real(yytext); return TKN_NUMBER_REAL; }
"."{DIGIT}+         { yylval = number_real(yytext); return TKN_NUMBER_REAL; }

"\""      { string_buf_ptr = string_buf; BEGIN(strng); }

0x{HEXDIGIT}+ { yylval = hexnumber(yytext); return TKN_NUMBER; }



#   { /* The following eats '#' style comments to end of line */
       BEGIN(comment); }
<comment>[^\n] { /* eat comments */ }
<comment>\n { lineno++; BEGIN(INITIAL); }


"/*" {  /* The following eats multiline C style comments */
        int c;
        int comment_start;
        
        comment_start = lineno;
        while (1) {
          while (((c = input()) != '*') && (c != EOF)) {
            /* eat up text of comment, but keep counting lines */
            if (c == '\n')
              lineno++;
          }
          
          if (c == '*') {
            while ((c = input()) == '*')
              ;
            if (c == '/')
              break;    /* found the end */
          }
          
          if (c == EOF) {
            yyerror("EOF in comment (started on line %d)", comment_start);
            return YYERRCODE;
          }
        }
     }


<strng>\" { *string_buf_ptr = 0; string_buf_ptr = string_buf;
             yylval = string(string_buf_ptr); BEGIN(INITIAL); return TKN_STRING; }
<strng>\\n  *string_buf_ptr++ = '\n';
<strng>\\t  *string_buf_ptr++ = '\t';
<strng>\\r  *string_buf_ptr++ = '\r';
<strng>\\b  *string_buf_ptr++ = '\b';
<strng>\\f  *string_buf_ptr++ = '\f';
<strng>\\(.|\n)  *(string_buf_ptr++) = yytext[1];
<strng>[^\\\n\"]+ { char *yptr = yytext; while (*yptr) 
                                         *(string_buf_ptr++) = *(yptr++); }

<strng>\n { yyerror("unterminated character constant");
            return YYERRCODE; }

allowfullpagebitstream { yylval=NULL; return K_ALLOWFULLPAGEBITSTREAM; }
avr910_devcode   { yylval=NULL; return K_AVR910_DEVCODE; }
bank_size        { yylval=NULL; return K_PAGE_SIZE; }
banked           { yylval=NULL; return K_PAGED; }
baudrate         { yylval=NULL; return K_BAUDRATE; }
blocksize        { yylval=NULL; return K_BLOCKSIZE; }
bs2              { yylval=NULL; return K_BS2; }
buff             { yylval=NULL; return K_BUFF; }
bytedelay        { yylval=NULL; return K_BYTEDELAY; }
chip_erase       { yylval=new_token(K_CHIP_ERASE); return K_CHIP_ERASE; }
chip_erase_delay { yylval=NULL; return K_CHIP_ERASE_DELAY; }
chiperasepolltimeout { yylval=NULL; return K_CHIPERASEPOLLTIMEOUT; }
chiperasepulsewidth { yylval=NULL; return K_CHIPERASEPULSEWIDTH; }
chiperasetime    { yylval=NULL; return K_CHIPERASETIME; }
cmdexedelay      { yylval=NULL; return K_CMDEXEDELAY; }
connection_type  { yylval=NULL; return K_CONNTYPE; }
dedicated        { yylval=new_token(K_DEDICATED); return K_DEDICATED; }
default_bitclock { yylval=NULL; return K_DEFAULT_BITCLOCK; }
default_parallel { yylval=NULL; return K_DEFAULT_PARALLEL; }
default_programmer { yylval=NULL; return K_DEFAULT_PROGRAMMER; }
default_safemode { yylval=NULL; return K_DEFAULT_SAFEMODE; }
default_serial   { yylval=NULL; return K_DEFAULT_SERIAL; }
delay            { yylval=NULL; return K_DELAY; }
desc             { yylval=NULL; return K_DESC; }
devicecode       { yylval=NULL; return K_DEVICECODE; }
eecr             { yylval=NULL; return K_EECR; }
eeprom           { yylval=NULL; return K_EEPROM; }
eeprom_instr     { yylval=NULL; return K_EEPROM_INSTR; }
enablepageprogramming { yylval=NULL; return K_ENABLEPAGEPROGRAMMING; }
errled           { yylval=NULL; return K_ERRLED; }
flash            { yylval=NULL; return K_FLASH; }
flash_instr      { yylval=NULL; return K_FLASH_INSTR; }
has_debugwire    { yylval=NULL; return K_HAS_DW; }
has_jtag         { yylval=NULL; return K_HAS_JTAG; }
has_pdi          { yylval=NULL; return K_HAS_PDI; }
has_tpi          { yylval=NULL; return K_HAS_TPI; }
hventerstabdelay { yylval=NULL; return K_HVENTERSTABDELAY; }
hvleavestabdelay { yylval=NULL; return K_HVLEAVESTABDELAY; }
hvsp_controlstack  { yylval=NULL; return K_HVSP_CONTROLSTACK; }
hvspcmdexedelay  { yylval=NULL; return K_HVSPCMDEXEDELAY; }
id               { yylval=NULL; return K_ID; }
idr              { yylval=NULL; return K_IDR; }
io               { yylval=new_token(K_IO); return K_IO; }
is_at90s1200     { yylval=NULL; return K_IS_AT90S1200; }
is_avr32         { yylval=NULL; return K_IS_AVR32; }
latchcycles      { yylval=NULL; return K_LATCHCYCLES; }
load_ext_addr    { yylval=new_token(K_LOAD_EXT_ADDR); return K_LOAD_EXT_ADDR; }
loadpage_hi      { yylval=new_token(K_LOADPAGE_HI); return K_LOADPAGE_HI; }
loadpage_lo      { yylval=new_token(K_LOADPAGE_LO); return K_LOADPAGE_LO; }
max_write_delay  { yylval=NULL; return K_MAX_WRITE_DELAY; }
mcu_base         { yylval=NULL; return K_MCU_BASE; }
memory           { yylval=NULL; return K_MEMORY; }
min_write_delay  { yylval=NULL; return K_MIN_WRITE_DELAY; }
miso             { yylval=NULL; return K_MISO; }
mode             { yylval=NULL; return K_MODE; }
mosi             { yylval=NULL; return K_MOSI; }
no               { yylval=new_token(K_NO); return K_NO; }
num_banks        { yylval=NULL; return K_NUM_PAGES; }
num_pages        { yylval=NULL; return K_NUM_PAGES; }
nvm_base         { yylval=NULL; return K_NVM_BASE; }
ocdrev           { yylval=NULL; return K_OCDREV; }
offset           { yylval=NULL; return K_OFFSET; }
page_size        { yylval=NULL; return K_PAGE_SIZE; }
paged            { yylval=NULL; return K_PAGED; }
pagel            { yylval=NULL; return K_PAGEL; }
parallel         { yylval=NULL; return K_PARALLEL; }
parent           { yylval=NULL; return K_PARENT; }
part             { yylval=NULL; return K_PART; }
pgm_enable       { yylval=new_token(K_PGM_ENABLE); return K_PGM_ENABLE; }
pgmled           { yylval=NULL; return K_PGMLED; }
pollindex        { yylval=NULL; return K_POLLINDEX; }
pollmethod       { yylval=NULL; return K_POLLMETHOD; }
pollvalue        { yylval=NULL; return K_POLLVALUE; }
postdelay        { yylval=NULL; return K_POSTDELAY; }
poweroffdelay    { yylval=NULL; return K_POWEROFFDELAY; }
pp_controlstack  { yylval=NULL; return K_PP_CONTROLSTACK; }
predelay         { yylval=NULL; return K_PREDELAY; }
progmodedelay    { yylval=NULL; return K_PROGMODEDELAY; }
programfusepolltimeout { yylval=NULL; return K_PROGRAMFUSEPOLLTIMEOUT; }
programfusepulsewidth { yylval=NULL; return K_PROGRAMFUSEPULSEWIDTH; }
programlockpolltimeout { yylval=NULL; return K_PROGRAMLOCKPOLLTIMEOUT; }
programlockpulsewidth { yylval=NULL; return K_PROGRAMLOCKPULSEWIDTH; }
programmer       { yylval=NULL; return K_PROGRAMMER; }
pseudo           { yylval=new_token(K_PSEUDO); return K_PSEUDO; }
pwroff_after_write { yylval=NULL; return K_PWROFF_AFTER_WRITE; }
rampz            { yylval=NULL; return K_RAMPZ; }
rdyled           { yylval=NULL; return K_RDYLED; }
read             { yylval=new_token(K_READ); return K_READ; }
read_hi          { yylval=new_token(K_READ_HI); return K_READ_HI; }
read_lo          { yylval=new_token(K_READ_LO); return K_READ_LO; }
readback_p1      { yylval=NULL; return K_READBACK_P1; }
readback_p2      { yylval=NULL; return K_READBACK_P2; }
readsize        { yylval=NULL; return K_READSIZE; }
reset            { yylval=new_token(K_RESET); return K_RESET; }
resetdelay       { yylval=NULL; return K_RESETDELAY; }
resetdelayms     { yylval=NULL; return K_RESETDELAYMS; }
resetdelayus     { yylval=NULL; return K_RESETDELAYUS; }
retry_pulse      { yylval=NULL; return K_RETRY_PULSE; }
sck              { yylval=new_token(K_SCK); return K_SCK; }
serial           { yylval=NULL; return K_SERIAL; }
signature        { yylval=NULL; return K_SIGNATURE; }
size             { yylval=NULL; return K_SIZE; }
spmcr            { yylval=NULL; return K_SPMCR; }
stabdelay        { yylval=NULL; return K_STABDELAY; }
stk500_devcode   { yylval=NULL; return K_STK500_DEVCODE; }
synchcycles      { yylval=NULL; return K_SYNCHCYCLES; }
synchloops       { yylval=NULL; return K_SYNCHLOOPS; }
timeout          { yylval=NULL; return K_TIMEOUT; }
togglevtg        { yylval=NULL; return K_TOGGLEVTG; }
type             { yylval=NULL; return K_TYPE; }
usb              { yylval=NULL; return K_USB; }
usbdev           { yylval=NULL; return K_USBDEV; }
usbpid           { yylval=NULL; return K_USBPID; }
usbproduct       { yylval=NULL; return K_USBPRODUCT; }
usbsn            { yylval=NULL; return K_USBSN; }
usbvendor        { yylval=NULL; return K_USBVENDOR; }
usbvid           { yylval=NULL; return K_USBVID; }
vcc              { yylval=NULL; return K_VCC; }
vfyled           { yylval=NULL; return K_VFYLED; }
write            { yylval=new_token(K_WRITE); return K_WRITE; }
write_hi         { yylval=new_token(K_WRITE_HI); return K_WRITE_HI; }
write_lo         { yylval=new_token(K_WRITE_LO); return K_WRITE_LO; }
writepage        { yylval=new_token(K_WRITEPAGE); return K_WRITEPAGE; }
yes              { yylval=new_token(K_YES); return K_YES; }

","       { yylval = NULL; pyytext(); return TKN_COMMA; }
"="       { yylval = NULL; pyytext(); return TKN_EQUAL; }
";"       { yylval = NULL; pyytext(); return TKN_SEMI; }
"~"       { yylval = NULL; pyytext(); return TKN_TILDE; }
"("       { yylval = NULL; pyytext(); return TKN_LEFT_PAREN; }
")"       { yylval = NULL; pyytext(); return TKN_RIGHT_PAREN; }

"\n"      { lineno++; }
[ \r\t]+  { /* ignore whitespace */ }

c: { yyerror("possible old-style config file entry\n"
             "  Update your config file (see " CONFIG_DIR 
               "/avrdude.conf.sample for a sample)");
     return YYERRCODE; }

. { return YYERRCODE; }

%%

