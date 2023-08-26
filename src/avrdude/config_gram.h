/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_YY_CONFIG_GRAM_H_INCLUDED
# define YY_YY_CONFIG_GRAM_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    K_READ = 258,
    K_WRITE = 259,
    K_READ_LO = 260,
    K_READ_HI = 261,
    K_WRITE_LO = 262,
    K_WRITE_HI = 263,
    K_LOADPAGE_LO = 264,
    K_LOADPAGE_HI = 265,
    K_LOAD_EXT_ADDR = 266,
    K_WRITEPAGE = 267,
    K_CHIP_ERASE = 268,
    K_PGM_ENABLE = 269,
    K_MEMORY = 270,
    K_PAGE_SIZE = 271,
    K_PAGED = 272,
    K_BAUDRATE = 273,
    K_BS2 = 274,
    K_BUFF = 275,
    K_CHIP_ERASE_DELAY = 276,
    K_CONNTYPE = 277,
    K_DEDICATED = 278,
    K_DEFAULT_BITCLOCK = 279,
    K_DEFAULT_PARALLEL = 280,
    K_DEFAULT_PROGRAMMER = 281,
    K_DEFAULT_SAFEMODE = 282,
    K_DEFAULT_SERIAL = 283,
    K_DESC = 284,
    K_DEVICECODE = 285,
    K_STK500_DEVCODE = 286,
    K_AVR910_DEVCODE = 287,
    K_EEPROM = 288,
    K_ERRLED = 289,
    K_FLASH = 290,
    K_ID = 291,
    K_IO = 292,
    K_LOADPAGE = 293,
    K_MAX_WRITE_DELAY = 294,
    K_MCU_BASE = 295,
    K_MIN_WRITE_DELAY = 296,
    K_MISO = 297,
    K_MOSI = 298,
    K_NUM_PAGES = 299,
    K_NVM_BASE = 300,
    K_OCDREV = 301,
    K_OFFSET = 302,
    K_PAGEL = 303,
    K_PARALLEL = 304,
    K_PARENT = 305,
    K_PART = 306,
    K_PGMLED = 307,
    K_PROGRAMMER = 308,
    K_PSEUDO = 309,
    K_PWROFF_AFTER_WRITE = 310,
    K_RDYLED = 311,
    K_READBACK_P1 = 312,
    K_READBACK_P2 = 313,
    K_READMEM = 314,
    K_RESET = 315,
    K_RETRY_PULSE = 316,
    K_SERIAL = 317,
    K_SCK = 318,
    K_SIGNATURE = 319,
    K_SIZE = 320,
    K_USB = 321,
    K_USBDEV = 322,
    K_USBSN = 323,
    K_USBPID = 324,
    K_USBPRODUCT = 325,
    K_USBVENDOR = 326,
    K_USBVID = 327,
    K_TYPE = 328,
    K_VCC = 329,
    K_VFYLED = 330,
    K_NO = 331,
    K_YES = 332,
    K_TIMEOUT = 333,
    K_STABDELAY = 334,
    K_CMDEXEDELAY = 335,
    K_HVSPCMDEXEDELAY = 336,
    K_SYNCHLOOPS = 337,
    K_BYTEDELAY = 338,
    K_POLLVALUE = 339,
    K_POLLINDEX = 340,
    K_PREDELAY = 341,
    K_POSTDELAY = 342,
    K_POLLMETHOD = 343,
    K_MODE = 344,
    K_DELAY = 345,
    K_BLOCKSIZE = 346,
    K_READSIZE = 347,
    K_HVENTERSTABDELAY = 348,
    K_PROGMODEDELAY = 349,
    K_LATCHCYCLES = 350,
    K_TOGGLEVTG = 351,
    K_POWEROFFDELAY = 352,
    K_RESETDELAYMS = 353,
    K_RESETDELAYUS = 354,
    K_HVLEAVESTABDELAY = 355,
    K_RESETDELAY = 356,
    K_SYNCHCYCLES = 357,
    K_HVCMDEXEDELAY = 358,
    K_CHIPERASEPULSEWIDTH = 359,
    K_CHIPERASEPOLLTIMEOUT = 360,
    K_CHIPERASETIME = 361,
    K_PROGRAMFUSEPULSEWIDTH = 362,
    K_PROGRAMFUSEPOLLTIMEOUT = 363,
    K_PROGRAMLOCKPULSEWIDTH = 364,
    K_PROGRAMLOCKPOLLTIMEOUT = 365,
    K_PP_CONTROLSTACK = 366,
    K_HVSP_CONTROLSTACK = 367,
    K_ALLOWFULLPAGEBITSTREAM = 368,
    K_ENABLEPAGEPROGRAMMING = 369,
    K_HAS_JTAG = 370,
    K_HAS_DW = 371,
    K_HAS_PDI = 372,
    K_HAS_TPI = 373,
    K_IDR = 374,
    K_IS_AT90S1200 = 375,
    K_IS_AVR32 = 376,
    K_RAMPZ = 377,
    K_SPMCR = 378,
    K_EECR = 379,
    K_FLASH_INSTR = 380,
    K_EEPROM_INSTR = 381,
    TKN_COMMA = 382,
    TKN_EQUAL = 383,
    TKN_SEMI = 384,
    TKN_TILDE = 385,
    TKN_LEFT_PAREN = 386,
    TKN_RIGHT_PAREN = 387,
    TKN_NUMBER = 388,
    TKN_NUMBER_REAL = 389,
    TKN_STRING = 390
  };
#endif
/* Tokens.  */
#define K_READ 258
#define K_WRITE 259
#define K_READ_LO 260
#define K_READ_HI 261
#define K_WRITE_LO 262
#define K_WRITE_HI 263
#define K_LOADPAGE_LO 264
#define K_LOADPAGE_HI 265
#define K_LOAD_EXT_ADDR 266
#define K_WRITEPAGE 267
#define K_CHIP_ERASE 268
#define K_PGM_ENABLE 269
#define K_MEMORY 270
#define K_PAGE_SIZE 271
#define K_PAGED 272
#define K_BAUDRATE 273
#define K_BS2 274
#define K_BUFF 275
#define K_CHIP_ERASE_DELAY 276
#define K_CONNTYPE 277
#define K_DEDICATED 278
#define K_DEFAULT_BITCLOCK 279
#define K_DEFAULT_PARALLEL 280
#define K_DEFAULT_PROGRAMMER 281
#define K_DEFAULT_SAFEMODE 282
#define K_DEFAULT_SERIAL 283
#define K_DESC 284
#define K_DEVICECODE 285
#define K_STK500_DEVCODE 286
#define K_AVR910_DEVCODE 287
#define K_EEPROM 288
#define K_ERRLED 289
#define K_FLASH 290
#define K_ID 291
#define K_IO 292
#define K_LOADPAGE 293
#define K_MAX_WRITE_DELAY 294
#define K_MCU_BASE 295
#define K_MIN_WRITE_DELAY 296
#define K_MISO 297
#define K_MOSI 298
#define K_NUM_PAGES 299
#define K_NVM_BASE 300
#define K_OCDREV 301
#define K_OFFSET 302
#define K_PAGEL 303
#define K_PARALLEL 304
#define K_PARENT 305
#define K_PART 306
#define K_PGMLED 307
#define K_PROGRAMMER 308
#define K_PSEUDO 309
#define K_PWROFF_AFTER_WRITE 310
#define K_RDYLED 311
#define K_READBACK_P1 312
#define K_READBACK_P2 313
#define K_READMEM 314
#define K_RESET 315
#define K_RETRY_PULSE 316
#define K_SERIAL 317
#define K_SCK 318
#define K_SIGNATURE 319
#define K_SIZE 320
#define K_USB 321
#define K_USBDEV 322
#define K_USBSN 323
#define K_USBPID 324
#define K_USBPRODUCT 325
#define K_USBVENDOR 326
#define K_USBVID 327
#define K_TYPE 328
#define K_VCC 329
#define K_VFYLED 330
#define K_NO 331
#define K_YES 332
#define K_TIMEOUT 333
#define K_STABDELAY 334
#define K_CMDEXEDELAY 335
#define K_HVSPCMDEXEDELAY 336
#define K_SYNCHLOOPS 337
#define K_BYTEDELAY 338
#define K_POLLVALUE 339
#define K_POLLINDEX 340
#define K_PREDELAY 341
#define K_POSTDELAY 342
#define K_POLLMETHOD 343
#define K_MODE 344
#define K_DELAY 345
#define K_BLOCKSIZE 346
#define K_READSIZE 347
#define K_HVENTERSTABDELAY 348
#define K_PROGMODEDELAY 349
#define K_LATCHCYCLES 350
#define K_TOGGLEVTG 351
#define K_POWEROFFDELAY 352
#define K_RESETDELAYMS 353
#define K_RESETDELAYUS 354
#define K_HVLEAVESTABDELAY 355
#define K_RESETDELAY 356
#define K_SYNCHCYCLES 357
#define K_HVCMDEXEDELAY 358
#define K_CHIPERASEPULSEWIDTH 359
#define K_CHIPERASEPOLLTIMEOUT 360
#define K_CHIPERASETIME 361
#define K_PROGRAMFUSEPULSEWIDTH 362
#define K_PROGRAMFUSEPOLLTIMEOUT 363
#define K_PROGRAMLOCKPULSEWIDTH 364
#define K_PROGRAMLOCKPOLLTIMEOUT 365
#define K_PP_CONTROLSTACK 366
#define K_HVSP_CONTROLSTACK 367
#define K_ALLOWFULLPAGEBITSTREAM 368
#define K_ENABLEPAGEPROGRAMMING 369
#define K_HAS_JTAG 370
#define K_HAS_DW 371
#define K_HAS_PDI 372
#define K_HAS_TPI 373
#define K_IDR 374
#define K_IS_AT90S1200 375
#define K_IS_AVR32 376
#define K_RAMPZ 377
#define K_SPMCR 378
#define K_EECR 379
#define K_FLASH_INSTR 380
#define K_EEPROM_INSTR 381
#define TKN_COMMA 382
#define TKN_EQUAL 383
#define TKN_SEMI 384
#define TKN_TILDE 385
#define TKN_LEFT_PAREN 386
#define TKN_RIGHT_PAREN 387
#define TKN_NUMBER 388
#define TKN_NUMBER_REAL 389
#define TKN_STRING 390

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef int YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_CONFIG_GRAM_H_INCLUDED  */
