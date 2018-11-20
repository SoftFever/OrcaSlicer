/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.0.4"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* Copy the first part of user declarations.  */
#line 21 "config_gram.y" /* yacc.c:339  */


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

#line 98 "config_gram.c" /* yacc.c:339  */

# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* In a future release of Bison, this section will be replaced
   by #include "y.tab.h".  */
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

/* Copy the second part of user declarations.  */

#line 419 "config_gram.c" /* yacc.c:358  */

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef YY_ATTRIBUTE
# if (defined __GNUC__                                               \
      && (2 < __GNUC__ || (__GNUC__ == 2 && 96 <= __GNUC_MINOR__)))  \
     || defined __SUNPRO_C && 0x5110 <= __SUNPRO_C
#  define YY_ATTRIBUTE(Spec) __attribute__(Spec)
# else
#  define YY_ATTRIBUTE(Spec) /* empty */
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# define YY_ATTRIBUTE_PURE   YY_ATTRIBUTE ((__pure__))
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# define YY_ATTRIBUTE_UNUSED YY_ATTRIBUTE ((__unused__))
#endif

#if !defined _Noreturn \
     && (!defined __STDC_VERSION__ || __STDC_VERSION__ < 201112)
# if defined _MSC_VER && 1200 <= _MSC_VER
#  define _Noreturn __declspec (noreturn)
# else
#  define _Noreturn YY_ATTRIBUTE ((__noreturn__))
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif


#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYSIZE_T yynewbytes;                                            \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / sizeof (*yyptr);                          \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  22
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   401

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  136
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  45
/* YYNRULES -- Number of rules.  */
#define YYNRULES  182
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  418

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   390

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   211,   211,   217,   221,   222,   226,   227,   232,   234,
     236,   242,   248,   254,   259,   270,   303,   313,   335,   393,
     403,   426,   427,   432,   433,   437,   438,   442,   465,   467,
     469,   471,   473,   478,   487,   491,   501,   509,   513,   514,
     515,   519,   526,   532,   533,   540,   547,   557,   572,   585,
     587,   591,   593,   597,   599,   603,   605,   610,   612,   616,
     616,   617,   617,   618,   618,   619,   619,   620,   620,   621,
     621,   622,   622,   623,   623,   624,   624,   625,   625,   629,
     630,   631,   632,   633,   634,   635,   636,   637,   638,   639,
     640,   645,   646,   651,   651,   655,   655,   659,   659,   663,
     670,   677,   685,   692,   699,   710,   717,   748,   779,   809,
     839,   845,   851,   857,   867,   873,   879,   885,   891,   897,
     903,   909,   915,   921,   927,   933,   939,   945,   951,   957,
     963,   969,   975,   981,   987,   993,   999,  1005,  1011,  1017,
    1023,  1029,  1035,  1045,  1055,  1065,  1075,  1085,  1095,  1105,
    1115,  1121,  1127,  1133,  1139,  1145,  1151,  1157,  1167,  1186,
    1210,  1209,  1234,  1261,  1261,  1266,  1267,  1272,  1278,  1285,
    1291,  1297,  1303,  1309,  1315,  1321,  1327,  1334,  1340,  1346,
    1352,  1358,  1365
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 0
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "K_READ", "K_WRITE", "K_READ_LO",
  "K_READ_HI", "K_WRITE_LO", "K_WRITE_HI", "K_LOADPAGE_LO",
  "K_LOADPAGE_HI", "K_LOAD_EXT_ADDR", "K_WRITEPAGE", "K_CHIP_ERASE",
  "K_PGM_ENABLE", "K_MEMORY", "K_PAGE_SIZE", "K_PAGED", "K_BAUDRATE",
  "K_BS2", "K_BUFF", "K_CHIP_ERASE_DELAY", "K_CONNTYPE", "K_DEDICATED",
  "K_DEFAULT_BITCLOCK", "K_DEFAULT_PARALLEL", "K_DEFAULT_PROGRAMMER",
  "K_DEFAULT_SAFEMODE", "K_DEFAULT_SERIAL", "K_DESC", "K_DEVICECODE",
  "K_STK500_DEVCODE", "K_AVR910_DEVCODE", "K_EEPROM", "K_ERRLED",
  "K_FLASH", "K_ID", "K_IO", "K_LOADPAGE", "K_MAX_WRITE_DELAY",
  "K_MCU_BASE", "K_MIN_WRITE_DELAY", "K_MISO", "K_MOSI", "K_NUM_PAGES",
  "K_NVM_BASE", "K_OCDREV", "K_OFFSET", "K_PAGEL", "K_PARALLEL",
  "K_PARENT", "K_PART", "K_PGMLED", "K_PROGRAMMER", "K_PSEUDO",
  "K_PWROFF_AFTER_WRITE", "K_RDYLED", "K_READBACK_P1", "K_READBACK_P2",
  "K_READMEM", "K_RESET", "K_RETRY_PULSE", "K_SERIAL", "K_SCK",
  "K_SIGNATURE", "K_SIZE", "K_USB", "K_USBDEV", "K_USBSN", "K_USBPID",
  "K_USBPRODUCT", "K_USBVENDOR", "K_USBVID", "K_TYPE", "K_VCC", "K_VFYLED",
  "K_NO", "K_YES", "K_TIMEOUT", "K_STABDELAY", "K_CMDEXEDELAY",
  "K_HVSPCMDEXEDELAY", "K_SYNCHLOOPS", "K_BYTEDELAY", "K_POLLVALUE",
  "K_POLLINDEX", "K_PREDELAY", "K_POSTDELAY", "K_POLLMETHOD", "K_MODE",
  "K_DELAY", "K_BLOCKSIZE", "K_READSIZE", "K_HVENTERSTABDELAY",
  "K_PROGMODEDELAY", "K_LATCHCYCLES", "K_TOGGLEVTG", "K_POWEROFFDELAY",
  "K_RESETDELAYMS", "K_RESETDELAYUS", "K_HVLEAVESTABDELAY", "K_RESETDELAY",
  "K_SYNCHCYCLES", "K_HVCMDEXEDELAY", "K_CHIPERASEPULSEWIDTH",
  "K_CHIPERASEPOLLTIMEOUT", "K_CHIPERASETIME", "K_PROGRAMFUSEPULSEWIDTH",
  "K_PROGRAMFUSEPOLLTIMEOUT", "K_PROGRAMLOCKPULSEWIDTH",
  "K_PROGRAMLOCKPOLLTIMEOUT", "K_PP_CONTROLSTACK", "K_HVSP_CONTROLSTACK",
  "K_ALLOWFULLPAGEBITSTREAM", "K_ENABLEPAGEPROGRAMMING", "K_HAS_JTAG",
  "K_HAS_DW", "K_HAS_PDI", "K_HAS_TPI", "K_IDR", "K_IS_AT90S1200",
  "K_IS_AVR32", "K_RAMPZ", "K_SPMCR", "K_EECR", "K_FLASH_INSTR",
  "K_EEPROM_INSTR", "TKN_COMMA", "TKN_EQUAL", "TKN_SEMI", "TKN_TILDE",
  "TKN_LEFT_PAREN", "TKN_RIGHT_PAREN", "TKN_NUMBER", "TKN_NUMBER_REAL",
  "TKN_STRING", "$accept", "number_real", "configuration", "config", "def",
  "prog_def", "prog_decl", "part_def", "part_decl", "string_list",
  "num_list", "prog_parms", "prog_parm", "prog_parm_type",
  "prog_parm_type_id", "prog_parm_conntype", "prog_parm_conntype_id",
  "prog_parm_usb", "usb_pid_list", "pin_number_non_empty", "pin_number",
  "pin_list_element", "pin_list_non_empty", "pin_list", "prog_parm_pins",
  "$@1", "$@2", "$@3", "$@4", "$@5", "$@6", "$@7", "$@8", "$@9", "$@10",
  "opcode", "part_parms", "reset_disposition", "parallel_modes",
  "retry_lines", "part_parm", "$@11", "yesno", "mem_specs", "mem_spec", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375,   376,   377,   378,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390
};
# endif

#define YYPACT_NINF -258

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-258)))

#define YYTABLE_NINF -1

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      23,  -119,   -83,   -82,   -68,   -50,     2,    31,    82,    23,
    -258,   -39,    55,   -37,   166,   -99,   -42,   -41,   -63,   -40,
     -35,   -34,  -258,  -258,  -258,   -43,   -32,   -26,   -20,   -19,
     -18,   -15,   -14,   -12,   -11,    -9,    -8,    -7,     3,     5,
       6,     7,     8,     9,    10,    12,    55,    13,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,    -3,    15,    17,    18,    20,
      22,    24,    25,    26,    27,    28,    29,    30,    32,    33,
      34,    35,    37,    38,    39,    54,    56,    58,    60,    61,
      62,    63,    64,    65,    66,    71,    72,    73,    76,    77,
      79,    81,    85,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,   101,   103,   104,   105,   106,   108,   109,
     110,   111,   112,   113,   114,   115,   127,   128,   166,   129,
    -258,  -258,   140,   164,   165,  -258,  -258,   167,   168,  -258,
    -258,    16,  -258,    21,    48,  -258,    75,  -258,  -258,  -258,
    -258,  -258,  -258,   122,   160,   169,   163,   170,   171,     4,
    -258,  -258,   172,  -258,  -258,   173,   174,   175,   176,   178,
     179,   180,   181,   183,   184,   185,   -44,    19,   -52,   -63,
     186,   187,   188,   189,   190,   191,   192,   193,   194,   195,
     196,   197,   198,   199,   200,   201,   202,   203,   204,   205,
     206,   207,   208,   209,   210,   211,   212,   213,   214,   215,
     216,   216,   -63,   -63,   -63,   -63,   -63,   -63,   217,   -63,
     -63,   218,   219,   220,   216,   216,    75,   225,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,   -76,  -258,  -258,  -258,  -258,
    -258,   -71,  -258,   228,   -71,   -71,   -71,   -71,   -71,   -71,
    -258,  -258,  -258,   229,  -258,  -258,  -258,  -258,  -258,  -258,
     -76,   -71,  -258,    14,  -258,  -258,  -258,  -258,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,   224,  -258,  -258,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,  -258,   231,   231,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,
     231,   231,   228,  -258,   -45,  -258,  -258,  -258,   232,  -258,
     227,  -258,  -258,   226,  -258,  -258,  -258,  -258,  -258,  -258,
     230,  -258,  -258,   234,   236,   237,   238,   239,   240,   241,
     242,   243,   244,   245,   246,   247,   248,   249,   250,    14,
     251,   252,   253,   216,  -258,   -76,  -258,  -258,   254,   -63,
     255,   256,   257,   258,   -63,   259,   260,   261,   262,   263,
     264,   265,   266,    75,   271,  -258,  -258,  -258,  -120,  -258,
    -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,   228,  -258,  -258
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       4,     0,     0,     0,     0,     0,    19,    16,     0,     5,
       6,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     1,     7,     8,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    15,     0,    28,    31,
      30,    29,     9,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    18,     0,
       2,     3,     0,     0,     0,   164,   163,     0,     0,    20,
      17,     0,    61,     0,     0,    71,     0,    69,    67,    75,
      73,    63,    65,     0,     0,     0,     0,     0,     0,     0,
      59,    77,     0,    25,   160,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    91,    13,
      11,    10,    14,    12,    33,    58,    38,    39,    40,    37,
      32,    52,    21,    27,    52,    52,    52,    52,    52,    52,
      41,    44,    47,    43,    46,    45,    42,    36,    35,    34,
      58,    52,    26,     0,   112,   110,   100,   101,   102,   103,
      99,   154,   155,   156,   111,    96,   158,    95,    93,    94,
     113,    97,    98,   159,   157,     0,   105,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   141,   134,   135,
     136,   137,   138,   139,   140,    23,   106,   107,   148,   149,
     142,   143,   144,   145,   150,   146,   147,   151,   152,   153,
     108,   109,   162,    92,     0,    49,    53,    55,    57,    62,
       0,    51,    72,     0,    70,    68,    76,    74,    64,    66,
       0,    60,    78,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   161,
       0,     0,     0,     0,    50,     0,    22,    48,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   165,   104,    24,     0,    56,
     169,   167,   173,   172,   170,   171,   174,   175,   176,   168,
     181,   177,   178,   179,   180,   182,   166,    54
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -258,  -258,  -258,  -258,   132,  -258,  -258,  -258,  -258,  -225,
    -209,  -258,   267,  -258,  -258,  -258,  -258,  -258,  -258,  -231,
    -181,  -228,  -258,  -109,  -258,  -258,  -258,  -258,  -258,  -258,
    -258,  -258,  -258,  -258,  -258,  -257,  -258,  -258,  -258,  -258,
     273,  -258,  -176,  -258,  -210
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,   132,     8,     9,    10,    11,    12,    13,    14,   243,
     316,    46,    47,    48,   259,    49,   239,    50,   253,   341,
     342,   337,   338,   339,    51,   260,   235,   248,   249,   245,
     244,   241,   247,   246,   261,   127,   128,   280,   276,   283,
     129,   263,   137,   369,   370
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint16 yytable[] =
{
     277,   332,   317,   284,   336,   257,   368,   372,   281,    15,
     275,   282,   417,   135,   136,   330,   331,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,   336,
     353,   354,   135,   136,   130,   131,   318,   319,   320,   321,
     322,   323,   278,   325,   326,    16,    17,     1,     2,     3,
       4,     5,    20,   355,   334,   356,   279,   335,   357,   340,
      18,   358,   335,   344,   345,   346,   347,   348,   349,   359,
     236,   360,   361,    25,     6,    26,     7,    27,    19,   362,
     352,    21,    22,   237,    28,   141,   373,   238,   374,    29,
      24,    30,    52,   133,   134,   138,   142,    31,    32,   363,
     139,   140,   143,   364,   365,   366,   367,    33,   144,   145,
     146,    34,   368,   147,   148,    35,   149,   150,    36,   151,
     152,   153,    37,    38,    39,    40,    41,    42,    43,    44,
      45,   154,   164,   155,   156,   157,   158,   159,   160,   258,
     161,    23,   163,   165,   336,   166,   167,   399,   168,   234,
     169,   351,   170,   171,   172,   173,   174,   175,   176,   394,
     177,   178,   179,   180,   398,   181,   182,   183,   415,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,   184,   240,   185,    66,   186,    67,   187,   188,
     189,   190,   191,   192,   193,    68,    69,    70,    71,   194,
     195,   196,    72,   401,   197,   198,    73,   199,   406,   200,
     242,    74,    75,   201,    76,    77,   202,   203,   204,   205,
     206,   207,   208,   209,   210,   211,    78,    79,    80,   212,
      81,   213,   214,   215,   216,    82,   217,   218,   219,   220,
     221,   222,   223,   224,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,   225,   226,   250,   228,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   229,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   230,   231,   251,   232,   233,   254,     0,
       0,   262,   252,     0,   256,   255,   264,   265,     0,   267,
     266,   268,   269,   162,   271,   270,   272,   273,   274,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,   302,   303,   304,   305,
     306,   307,   308,   309,   310,   311,   312,   313,   314,   315,
     324,   327,   328,   329,   333,   343,   350,   371,   372,   375,
     374,   376,   378,   377,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390,   391,   392,   393,     0,
     395,     0,     0,     0,     0,   396,   397,   400,   402,   403,
     404,   405,   407,   408,   409,   410,   411,   412,   413,   414,
     416,   227
};

static const yytype_int16 yycheck[] =
{
     176,   226,   211,   179,   235,     1,   263,   127,    60,   128,
      54,    63,   132,    76,    77,   224,   225,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    14,   260,
      16,    17,    76,    77,   133,   134,   212,   213,   214,   215,
     216,   217,    23,   219,   220,   128,   128,    24,    25,    26,
      27,    28,    50,    39,   130,    41,    37,   133,    44,   130,
     128,    47,   133,   244,   245,   246,   247,   248,   249,    55,
      49,    57,    58,    18,    51,    20,    53,    22,   128,    65,
     261,    50,     0,    62,    29,   128,   131,    66,   133,    34,
     129,    36,   129,   135,   135,   135,   128,    42,    43,    85,
     135,   135,   128,    89,    90,    91,    92,    52,   128,   128,
     128,    56,   369,   128,   128,    60,   128,   128,    63,   128,
     128,   128,    67,    68,    69,    70,    71,    72,    73,    74,
      75,   128,   135,   128,   128,   128,   128,   128,   128,   135,
     128,     9,   129,   128,   375,   128,   128,   375,   128,   133,
     128,   260,   128,   128,   128,   128,   128,   128,   128,   369,
     128,   128,   128,   128,   373,   128,   128,   128,   393,     3,
       4,     5,     6,     7,     8,     9,    10,    11,    12,    13,
      14,    15,   128,   135,   128,    19,   128,    21,   128,   128,
     128,   128,   128,   128,   128,    29,    30,    31,    32,   128,
     128,   128,    36,   379,   128,   128,    40,   128,   384,   128,
     135,    45,    46,   128,    48,    49,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,    60,    61,    62,   128,
      64,   128,   128,   128,   128,    69,   128,   128,   128,   128,
     128,   128,   128,   128,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,   128,   128,   135,   129,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   129,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   129,   129,   135,   129,   129,   135,    -1,
      -1,   129,   133,    -1,   133,   135,   133,   133,    -1,   133,
     135,   133,   133,    46,   133,   135,   133,   133,   133,   133,
     133,   133,   133,   133,   133,   133,   133,   133,   133,   133,
     133,   133,   133,   133,   133,   133,   133,   133,   133,   133,
     133,   133,   133,   133,   133,   133,   133,   133,   133,   133,
     133,   133,   133,   133,   129,   127,   127,   133,   127,   127,
     133,   135,   128,   133,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,    -1,
     129,    -1,    -1,    -1,    -1,   133,   133,   133,   133,   133,
     133,   133,   133,   133,   133,   133,   133,   133,   133,   133,
     129,   128
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    24,    25,    26,    27,    28,    51,    53,   138,   139,
     140,   141,   142,   143,   144,   128,   128,   128,   128,   128,
      50,    50,     0,   140,   129,    18,    20,    22,    29,    34,
      36,    42,    43,    52,    56,    60,    63,    67,    68,    69,
      70,    71,    72,    73,    74,    75,   147,   148,   149,   151,
     153,   160,   129,     3,     4,     5,     6,     7,     8,     9,
      10,    11,    12,    13,    14,    15,    19,    21,    29,    30,
      31,    32,    36,    40,    45,    46,    48,    49,    60,    61,
      62,    64,    69,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   171,   172,   176,
     133,   134,   137,   135,   135,    76,    77,   178,   135,   135,
     135,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   148,   129,   135,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   176,   129,   129,
     129,   129,   129,   129,   133,   162,    49,    62,    66,   152,
     135,   167,   135,   145,   166,   165,   169,   168,   163,   164,
     135,   135,   133,   154,   135,   135,   133,     1,   135,   150,
     161,   170,   129,   177,   133,   133,   135,   133,   133,   133,
     135,   133,   133,   133,   133,    54,   174,   178,    23,    37,
     173,    60,    63,   175,   178,   133,   133,   133,   133,   133,
     133,   133,   133,   133,   133,   133,   133,   133,   133,   133,
     133,   133,   133,   133,   133,   133,   133,   133,   133,   133,
     133,   133,   133,   133,   133,   133,   146,   146,   178,   178,
     178,   178,   178,   178,   133,   178,   178,   133,   133,   133,
     146,   146,   145,   129,   130,   133,   155,   157,   158,   159,
     130,   155,   156,   127,   156,   156,   156,   156,   156,   156,
     127,   159,   156,    16,    17,    39,    41,    44,    47,    55,
      57,    58,    65,    85,    89,    90,    91,    92,   171,   179,
     180,   133,   127,   131,   133,   127,   135,   133,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   180,   129,   133,   133,   146,   157,
     133,   178,   133,   133,   133,   133,   178,   133,   133,   133,
     133,   133,   133,   133,   133,   145,   129,   132
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   136,   137,   137,   138,   138,   139,   139,   140,   140,
     140,   140,   140,   140,   140,   141,   142,   142,   143,   144,
     144,   145,   145,   146,   146,   147,   147,   148,   148,   148,
     148,   148,   148,   148,   149,   150,   150,   151,   152,   152,
     152,   153,   153,   153,   153,   153,   153,   154,   154,   155,
     155,   156,   156,   157,   157,   158,   158,   159,   159,   161,
     160,   162,   160,   163,   160,   164,   160,   165,   160,   166,
     160,   167,   160,   168,   160,   169,   160,   170,   160,   171,
     171,   171,   171,   171,   171,   171,   171,   171,   171,   171,
     171,   172,   172,   173,   173,   174,   174,   175,   175,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,   176,   176,
     177,   176,   176,   178,   178,   179,   179,   180,   180,   180,
     180,   180,   180,   180,   180,   180,   180,   180,   180,   180,
     180,   180,   180
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     1,     0,     1,     1,     2,     2,     2,
       4,     4,     4,     4,     4,     2,     1,     3,     2,     1,
       3,     1,     3,     1,     3,     2,     3,     3,     1,     1,
       1,     1,     3,     3,     3,     1,     1,     3,     1,     1,
       1,     3,     3,     3,     3,     3,     3,     1,     3,     1,
       2,     1,     0,     1,     4,     1,     3,     1,     0,     0,
       4,     0,     4,     0,     4,     0,     4,     0,     4,     0,
       4,     0,     4,     0,     4,     0,     4,     0,     4,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     2,     3,     1,     1,     1,     1,     1,     1,     3,
       3,     3,     3,     3,     5,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       0,     4,     3,     1,     1,     2,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                  \
do                                                              \
  if (yychar == YYEMPTY)                                        \
    {                                                           \
      yychar = (Token);                                         \
      yylval = (Value);                                         \
      YYPOPSTACK (yylen);                                       \
      yystate = *yyssp;                                         \
      goto yybackup;                                            \
    }                                                           \
  else                                                          \
    {                                                           \
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;                                                  \
    }                                                           \
while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*----------------------------------------.
| Print this symbol's value on YYOUTPUT.  |
`----------------------------------------*/

static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  YYUSE (yytype);
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyoutput, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, int yyrule)
{
  unsigned long int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &(yyvsp[(yyi + 1) - (yynrhs)])
                                              );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
yystrlen (const char *yystr)
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            /* Fall through.  */
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (! (yysize <= yysize1
                         && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                    return 2;
                  yysize = yysize1;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
      return 2;
    yysize = yysize1;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
{
  YYUSE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        YYSTYPE *yyvs1 = yyvs;
        yytype_int16 *yyss1 = yyss;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * sizeof (*yyssp),
                    &yyvs1, yysize * sizeof (*yyvsp),
                    &yystacksize);

        yyss = yyss1;
        yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yytype_int16 *yyss1 = yyss;
        union yyalloc *yyptr =
          (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
                  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 211 "config_gram.y" /* yacc.c:1646  */
    {
    (yyval) = (yyvsp[0]);
    /* convert value to real */
    (yyval)->value.number_real = (yyval)->value.number;
    (yyval)->value.type = V_NUM_REAL;
  }
#line 1819 "config_gram.c" /* yacc.c:1646  */
    break;

  case 3:
#line 217 "config_gram.y" /* yacc.c:1646  */
    {
    (yyval) = (yyvsp[0]);
  }
#line 1827 "config_gram.c" /* yacc.c:1646  */
    break;

  case 10:
#line 236 "config_gram.y" /* yacc.c:1646  */
    {
    strncpy(default_programmer, (yyvsp[-1])->value.string, MAX_STR_CONST);
    default_programmer[MAX_STR_CONST-1] = 0;
    free_token((yyvsp[-1]));
  }
#line 1837 "config_gram.c" /* yacc.c:1646  */
    break;

  case 11:
#line 242 "config_gram.y" /* yacc.c:1646  */
    {
    strncpy(default_parallel, (yyvsp[-1])->value.string, PATH_MAX);
    default_parallel[PATH_MAX-1] = 0;
    free_token((yyvsp[-1]));
  }
#line 1847 "config_gram.c" /* yacc.c:1646  */
    break;

  case 12:
#line 248 "config_gram.y" /* yacc.c:1646  */
    {
    strncpy(default_serial, (yyvsp[-1])->value.string, PATH_MAX);
    default_serial[PATH_MAX-1] = 0;
    free_token((yyvsp[-1]));
  }
#line 1857 "config_gram.c" /* yacc.c:1646  */
    break;

  case 13:
#line 254 "config_gram.y" /* yacc.c:1646  */
    {
    default_bitclock = (yyvsp[-1])->value.number_real;
    free_token((yyvsp[-1]));
  }
#line 1866 "config_gram.c" /* yacc.c:1646  */
    break;

  case 14:
#line 259 "config_gram.y" /* yacc.c:1646  */
    {
    if ((yyvsp[-1])->primary == K_YES)
      default_safemode = 1;
    else if ((yyvsp[-1])->primary == K_NO)
      default_safemode = 0;
    free_token((yyvsp[-1]));
  }
#line 1878 "config_gram.c" /* yacc.c:1646  */
    break;

  case 15:
#line 271 "config_gram.y" /* yacc.c:1646  */
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
#line 1911 "config_gram.c" /* yacc.c:1646  */
    break;

  case 16:
#line 304 "config_gram.y" /* yacc.c:1646  */
    { current_prog = pgm_new();
      if (current_prog == NULL) {
        yyerror("could not create pgm instance");
        YYABORT;
      }
      strcpy(current_prog->config_file, infile);
      current_prog->lineno = lineno;
    }
#line 1924 "config_gram.c" /* yacc.c:1646  */
    break;

  case 17:
#line 314 "config_gram.y" /* yacc.c:1646  */
    {
      struct programmer_t * pgm = locate_programmer(programmers, (yyvsp[0])->value.string);
      if (pgm == NULL) {
        yyerror("parent programmer %s not found", (yyvsp[0])->value.string);
        free_token((yyvsp[0]));
        YYABORT;
      }
      current_prog = pgm_dup(pgm);
      if (current_prog == NULL) {
        yyerror("could not duplicate pgm instance");
        free_token((yyvsp[0]));
        YYABORT;
      }
      strcpy(current_prog->config_file, infile);
      current_prog->lineno = lineno;
      free_token((yyvsp[0]));
    }
#line 1946 "config_gram.c" /* yacc.c:1646  */
    break;

  case 18:
#line 336 "config_gram.y" /* yacc.c:1646  */
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
#line 2005 "config_gram.c" /* yacc.c:1646  */
    break;

  case 19:
#line 394 "config_gram.y" /* yacc.c:1646  */
    {
      current_part = avr_new_part();
      if (current_part == NULL) {
        yyerror("could not create part instance");
        YYABORT;
      }
      strcpy(current_part->config_file, infile);
      current_part->lineno = lineno;
    }
#line 2019 "config_gram.c" /* yacc.c:1646  */
    break;

  case 20:
#line 404 "config_gram.y" /* yacc.c:1646  */
    {
      AVRPART * parent_part = locate_part(part_list, (yyvsp[0])->value.string);
      if (parent_part == NULL) {
        yyerror("can't find parent part");
        free_token((yyvsp[0]));
        YYABORT;
      }

      current_part = avr_dup_part(parent_part);
      if (current_part == NULL) {
        yyerror("could not duplicate part instance");
        free_token((yyvsp[0]));
        YYABORT;
      }
      strcpy(current_part->config_file, infile);
      current_part->lineno = lineno;

      free_token((yyvsp[0]));
    }
#line 2043 "config_gram.c" /* yacc.c:1646  */
    break;

  case 21:
#line 426 "config_gram.y" /* yacc.c:1646  */
    { ladd(string_list, (yyvsp[0])); }
#line 2049 "config_gram.c" /* yacc.c:1646  */
    break;

  case 22:
#line 427 "config_gram.y" /* yacc.c:1646  */
    { ladd(string_list, (yyvsp[0])); }
#line 2055 "config_gram.c" /* yacc.c:1646  */
    break;

  case 23:
#line 432 "config_gram.y" /* yacc.c:1646  */
    { ladd(number_list, (yyvsp[0])); }
#line 2061 "config_gram.c" /* yacc.c:1646  */
    break;

  case 24:
#line 433 "config_gram.y" /* yacc.c:1646  */
    { ladd(number_list, (yyvsp[0])); }
#line 2067 "config_gram.c" /* yacc.c:1646  */
    break;

  case 27:
#line 442 "config_gram.y" /* yacc.c:1646  */
    {
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
  }
#line 2095 "config_gram.c" /* yacc.c:1646  */
    break;

  case 32:
#line 473 "config_gram.y" /* yacc.c:1646  */
    {
    strncpy(current_prog->desc, (yyvsp[0])->value.string, PGM_DESCLEN);
    current_prog->desc[PGM_DESCLEN-1] = 0;
    free_token((yyvsp[0]));
  }
#line 2105 "config_gram.c" /* yacc.c:1646  */
    break;

  case 33:
#line 478 "config_gram.y" /* yacc.c:1646  */
    {
    {
      current_prog->baudrate = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
  }
#line 2116 "config_gram.c" /* yacc.c:1646  */
    break;

  case 35:
#line 491 "config_gram.y" /* yacc.c:1646  */
    {
  const struct programmer_type_t * pgm_type = locate_programmer_type((yyvsp[0])->value.string);
    if (pgm_type == NULL) {
        yyerror("programmer type %s not found", (yyvsp[0])->value.string);
        free_token((yyvsp[0])); 
        YYABORT;
    }
    current_prog->initpgm = pgm_type->initpgm;
    free_token((yyvsp[0])); 
}
#line 2131 "config_gram.c" /* yacc.c:1646  */
    break;

  case 36:
#line 502 "config_gram.y" /* yacc.c:1646  */
    {
        yyerror("programmer type must be written as \"id_type\"");
        YYABORT;
}
#line 2140 "config_gram.c" /* yacc.c:1646  */
    break;

  case 38:
#line 513 "config_gram.y" /* yacc.c:1646  */
    { current_prog->conntype = CONNTYPE_PARALLEL; }
#line 2146 "config_gram.c" /* yacc.c:1646  */
    break;

  case 39:
#line 514 "config_gram.y" /* yacc.c:1646  */
    { current_prog->conntype = CONNTYPE_SERIAL; }
#line 2152 "config_gram.c" /* yacc.c:1646  */
    break;

  case 40:
#line 515 "config_gram.y" /* yacc.c:1646  */
    { current_prog->conntype = CONNTYPE_USB; }
#line 2158 "config_gram.c" /* yacc.c:1646  */
    break;

  case 41:
#line 519 "config_gram.y" /* yacc.c:1646  */
    {
    {
      strncpy(current_prog->usbdev, (yyvsp[0])->value.string, PGM_USBSTRINGLEN);
      current_prog->usbdev[PGM_USBSTRINGLEN-1] = 0;
      free_token((yyvsp[0]));
    }
  }
#line 2170 "config_gram.c" /* yacc.c:1646  */
    break;

  case 42:
#line 526 "config_gram.y" /* yacc.c:1646  */
    {
    {
      current_prog->usbvid = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
  }
#line 2181 "config_gram.c" /* yacc.c:1646  */
    break;

  case 44:
#line 533 "config_gram.y" /* yacc.c:1646  */
    {
    {
      strncpy(current_prog->usbsn, (yyvsp[0])->value.string, PGM_USBSTRINGLEN);
      current_prog->usbsn[PGM_USBSTRINGLEN-1] = 0;
      free_token((yyvsp[0]));
    }
  }
#line 2193 "config_gram.c" /* yacc.c:1646  */
    break;

  case 45:
#line 540 "config_gram.y" /* yacc.c:1646  */
    {
    {
      strncpy(current_prog->usbvendor, (yyvsp[0])->value.string, PGM_USBSTRINGLEN);
      current_prog->usbvendor[PGM_USBSTRINGLEN-1] = 0;
      free_token((yyvsp[0]));
    }
  }
#line 2205 "config_gram.c" /* yacc.c:1646  */
    break;

  case 46:
#line 547 "config_gram.y" /* yacc.c:1646  */
    {
    {
      strncpy(current_prog->usbproduct, (yyvsp[0])->value.string, PGM_USBSTRINGLEN);
      current_prog->usbproduct[PGM_USBSTRINGLEN-1] = 0;
      free_token((yyvsp[0]));
    }
  }
#line 2217 "config_gram.c" /* yacc.c:1646  */
    break;

  case 47:
#line 557 "config_gram.y" /* yacc.c:1646  */
    {
    {
      /* overwrite pids, so clear the existing entries */
      ldestroy_cb(current_prog->usbpid, free);
      current_prog->usbpid = lcreat(NULL, 0);
    }
    {
      int *ip = malloc(sizeof(int));
      if (ip) {
        *ip = (yyvsp[0])->value.number;
        ladd(current_prog->usbpid, ip);
      }
      free_token((yyvsp[0]));
    }
  }
#line 2237 "config_gram.c" /* yacc.c:1646  */
    break;

  case 48:
#line 572 "config_gram.y" /* yacc.c:1646  */
    {
    {
      int *ip = malloc(sizeof(int));
      if (ip) {
        *ip = (yyvsp[0])->value.number;
        ladd(current_prog->usbpid, ip);
      }
      free_token((yyvsp[0]));
    }
  }
#line 2252 "config_gram.c" /* yacc.c:1646  */
    break;

  case 49:
#line 585 "config_gram.y" /* yacc.c:1646  */
    { if(0 != assign_pin(pin_name, (yyvsp[0]), 0)) YYABORT;  }
#line 2258 "config_gram.c" /* yacc.c:1646  */
    break;

  case 50:
#line 587 "config_gram.y" /* yacc.c:1646  */
    { if(0 != assign_pin(pin_name, (yyvsp[0]), 1)) YYABORT; }
#line 2264 "config_gram.c" /* yacc.c:1646  */
    break;

  case 52:
#line 593 "config_gram.y" /* yacc.c:1646  */
    { pin_clear_all(&(current_prog->pin[pin_name])); }
#line 2270 "config_gram.c" /* yacc.c:1646  */
    break;

  case 54:
#line 599 "config_gram.y" /* yacc.c:1646  */
    { if(0 != assign_pin_list(1)) YYABORT; }
#line 2276 "config_gram.c" /* yacc.c:1646  */
    break;

  case 58:
#line 612 "config_gram.y" /* yacc.c:1646  */
    { pin_clear_all(&(current_prog->pin[pin_name])); }
#line 2282 "config_gram.c" /* yacc.c:1646  */
    break;

  case 59:
#line 616 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PPI_AVR_VCC;  }
#line 2288 "config_gram.c" /* yacc.c:1646  */
    break;

  case 61:
#line 617 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PPI_AVR_BUFF; }
#line 2294 "config_gram.c" /* yacc.c:1646  */
    break;

  case 63:
#line 618 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PIN_AVR_RESET;}
#line 2300 "config_gram.c" /* yacc.c:1646  */
    break;

  case 64:
#line 618 "config_gram.y" /* yacc.c:1646  */
    { free_token((yyvsp[-3])); }
#line 2306 "config_gram.c" /* yacc.c:1646  */
    break;

  case 65:
#line 619 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PIN_AVR_SCK;  }
#line 2312 "config_gram.c" /* yacc.c:1646  */
    break;

  case 66:
#line 619 "config_gram.y" /* yacc.c:1646  */
    { free_token((yyvsp[-3])); }
#line 2318 "config_gram.c" /* yacc.c:1646  */
    break;

  case 67:
#line 620 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PIN_AVR_MOSI; }
#line 2324 "config_gram.c" /* yacc.c:1646  */
    break;

  case 69:
#line 621 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PIN_AVR_MISO; }
#line 2330 "config_gram.c" /* yacc.c:1646  */
    break;

  case 71:
#line 622 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PIN_LED_ERR;  }
#line 2336 "config_gram.c" /* yacc.c:1646  */
    break;

  case 73:
#line 623 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PIN_LED_RDY;  }
#line 2342 "config_gram.c" /* yacc.c:1646  */
    break;

  case 75:
#line 624 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PIN_LED_PGM;  }
#line 2348 "config_gram.c" /* yacc.c:1646  */
    break;

  case 77:
#line 625 "config_gram.y" /* yacc.c:1646  */
    {pin_name = PIN_LED_VFY;  }
#line 2354 "config_gram.c" /* yacc.c:1646  */
    break;

  case 99:
#line 664 "config_gram.y" /* yacc.c:1646  */
    {
      strncpy(current_part->id, (yyvsp[0])->value.string, AVR_IDLEN);
      current_part->id[AVR_IDLEN-1] = 0;
      free_token((yyvsp[0]));
    }
#line 2364 "config_gram.c" /* yacc.c:1646  */
    break;

  case 100:
#line 671 "config_gram.y" /* yacc.c:1646  */
    {
      strncpy(current_part->desc, (yyvsp[0])->value.string, AVR_DESCLEN);
      current_part->desc[AVR_DESCLEN-1] = 0;
      free_token((yyvsp[0]));
    }
#line 2374 "config_gram.c" /* yacc.c:1646  */
    break;

  case 101:
#line 677 "config_gram.y" /* yacc.c:1646  */
    {
    {
      yyerror("devicecode is deprecated, use "
              "stk500_devcode instead");
      YYABORT;
    }
  }
#line 2386 "config_gram.c" /* yacc.c:1646  */
    break;

  case 102:
#line 685 "config_gram.y" /* yacc.c:1646  */
    {
    {
      current_part->stk500_devcode = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
  }
#line 2397 "config_gram.c" /* yacc.c:1646  */
    break;

  case 103:
#line 692 "config_gram.y" /* yacc.c:1646  */
    {
    {
      current_part->avr910_devcode = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
  }
#line 2408 "config_gram.c" /* yacc.c:1646  */
    break;

  case 104:
#line 699 "config_gram.y" /* yacc.c:1646  */
    {
    {
      current_part->signature[0] = (yyvsp[-2])->value.number;
      current_part->signature[1] = (yyvsp[-1])->value.number;
      current_part->signature[2] = (yyvsp[0])->value.number;
      free_token((yyvsp[-2]));
      free_token((yyvsp[-1]));
      free_token((yyvsp[0]));
    }
  }
#line 2423 "config_gram.c" /* yacc.c:1646  */
    break;

  case 105:
#line 710 "config_gram.y" /* yacc.c:1646  */
    {
    {
      current_part->usbpid = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
  }
#line 2434 "config_gram.c" /* yacc.c:1646  */
    break;

  case 106:
#line 717 "config_gram.y" /* yacc.c:1646  */
    {
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
  }
#line 2469 "config_gram.c" /* yacc.c:1646  */
    break;

  case 107:
#line 748 "config_gram.y" /* yacc.c:1646  */
    {
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
  }
#line 2504 "config_gram.c" /* yacc.c:1646  */
    break;

  case 108:
#line 779 "config_gram.y" /* yacc.c:1646  */
    {
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
  }
#line 2538 "config_gram.c" /* yacc.c:1646  */
    break;

  case 109:
#line 809 "config_gram.y" /* yacc.c:1646  */
    {
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
  }
#line 2572 "config_gram.c" /* yacc.c:1646  */
    break;

  case 110:
#line 840 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->chip_erase_delay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2581 "config_gram.c" /* yacc.c:1646  */
    break;

  case 111:
#line 846 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->pagel = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2590 "config_gram.c" /* yacc.c:1646  */
    break;

  case 112:
#line 852 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->bs2 = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2599 "config_gram.c" /* yacc.c:1646  */
    break;

  case 113:
#line 858 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_DEDICATED)
        current_part->reset_disposition = RESET_DEDICATED;
      else if ((yyvsp[0])->primary == K_IO)
        current_part->reset_disposition = RESET_IO;

      free_tokens(2, (yyvsp[-2]), (yyvsp[0]));
    }
#line 2612 "config_gram.c" /* yacc.c:1646  */
    break;

  case 114:
#line 868 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->timeout = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2621 "config_gram.c" /* yacc.c:1646  */
    break;

  case 115:
#line 874 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->stabdelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2630 "config_gram.c" /* yacc.c:1646  */
    break;

  case 116:
#line 880 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->cmdexedelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2639 "config_gram.c" /* yacc.c:1646  */
    break;

  case 117:
#line 886 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->hvspcmdexedelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2648 "config_gram.c" /* yacc.c:1646  */
    break;

  case 118:
#line 892 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->synchloops = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2657 "config_gram.c" /* yacc.c:1646  */
    break;

  case 119:
#line 898 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->bytedelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2666 "config_gram.c" /* yacc.c:1646  */
    break;

  case 120:
#line 904 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->pollvalue = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2675 "config_gram.c" /* yacc.c:1646  */
    break;

  case 121:
#line 910 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->pollindex = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2684 "config_gram.c" /* yacc.c:1646  */
    break;

  case 122:
#line 916 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->predelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2693 "config_gram.c" /* yacc.c:1646  */
    break;

  case 123:
#line 922 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->postdelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2702 "config_gram.c" /* yacc.c:1646  */
    break;

  case 124:
#line 928 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->pollmethod = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2711 "config_gram.c" /* yacc.c:1646  */
    break;

  case 125:
#line 934 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->hventerstabdelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2720 "config_gram.c" /* yacc.c:1646  */
    break;

  case 126:
#line 940 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->progmodedelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2729 "config_gram.c" /* yacc.c:1646  */
    break;

  case 127:
#line 946 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->latchcycles = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2738 "config_gram.c" /* yacc.c:1646  */
    break;

  case 128:
#line 952 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->togglevtg = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2747 "config_gram.c" /* yacc.c:1646  */
    break;

  case 129:
#line 958 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->poweroffdelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2756 "config_gram.c" /* yacc.c:1646  */
    break;

  case 130:
#line 964 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->resetdelayms = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2765 "config_gram.c" /* yacc.c:1646  */
    break;

  case 131:
#line 970 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->resetdelayus = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2774 "config_gram.c" /* yacc.c:1646  */
    break;

  case 132:
#line 976 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->hvleavestabdelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2783 "config_gram.c" /* yacc.c:1646  */
    break;

  case 133:
#line 982 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->resetdelay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2792 "config_gram.c" /* yacc.c:1646  */
    break;

  case 134:
#line 988 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->chiperasepulsewidth = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2801 "config_gram.c" /* yacc.c:1646  */
    break;

  case 135:
#line 994 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->chiperasepolltimeout = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2810 "config_gram.c" /* yacc.c:1646  */
    break;

  case 136:
#line 1000 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->chiperasetime = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2819 "config_gram.c" /* yacc.c:1646  */
    break;

  case 137:
#line 1006 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->programfusepulsewidth = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2828 "config_gram.c" /* yacc.c:1646  */
    break;

  case 138:
#line 1012 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->programfusepolltimeout = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2837 "config_gram.c" /* yacc.c:1646  */
    break;

  case 139:
#line 1018 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->programlockpulsewidth = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2846 "config_gram.c" /* yacc.c:1646  */
    break;

  case 140:
#line 1024 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->programlockpolltimeout = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2855 "config_gram.c" /* yacc.c:1646  */
    break;

  case 141:
#line 1030 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->synchcycles = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2864 "config_gram.c" /* yacc.c:1646  */
    break;

  case 142:
#line 1036 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES)
        current_part->flags |= AVRPART_HAS_JTAG;
      else if ((yyvsp[0])->primary == K_NO)
        current_part->flags &= ~AVRPART_HAS_JTAG;

      free_token((yyvsp[0]));
    }
#line 2877 "config_gram.c" /* yacc.c:1646  */
    break;

  case 143:
#line 1046 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES)
        current_part->flags |= AVRPART_HAS_DW;
      else if ((yyvsp[0])->primary == K_NO)
        current_part->flags &= ~AVRPART_HAS_DW;

      free_token((yyvsp[0]));
    }
#line 2890 "config_gram.c" /* yacc.c:1646  */
    break;

  case 144:
#line 1056 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES)
        current_part->flags |= AVRPART_HAS_PDI;
      else if ((yyvsp[0])->primary == K_NO)
        current_part->flags &= ~AVRPART_HAS_PDI;

      free_token((yyvsp[0]));
    }
#line 2903 "config_gram.c" /* yacc.c:1646  */
    break;

  case 145:
#line 1066 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES)
        current_part->flags |= AVRPART_HAS_TPI;
      else if ((yyvsp[0])->primary == K_NO)
        current_part->flags &= ~AVRPART_HAS_TPI;

      free_token((yyvsp[0]));
    }
#line 2916 "config_gram.c" /* yacc.c:1646  */
    break;

  case 146:
#line 1076 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES)
        current_part->flags |= AVRPART_IS_AT90S1200;
      else if ((yyvsp[0])->primary == K_NO)
        current_part->flags &= ~AVRPART_IS_AT90S1200;

      free_token((yyvsp[0]));
    }
#line 2929 "config_gram.c" /* yacc.c:1646  */
    break;

  case 147:
#line 1086 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES)
        current_part->flags |= AVRPART_AVR32;
      else if ((yyvsp[0])->primary == K_NO)
        current_part->flags &= ~AVRPART_AVR32;

      free_token((yyvsp[0]));
    }
#line 2942 "config_gram.c" /* yacc.c:1646  */
    break;

  case 148:
#line 1096 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES)
        current_part->flags |= AVRPART_ALLOWFULLPAGEBITSTREAM;
      else if ((yyvsp[0])->primary == K_NO)
        current_part->flags &= ~AVRPART_ALLOWFULLPAGEBITSTREAM;

      free_token((yyvsp[0]));
    }
#line 2955 "config_gram.c" /* yacc.c:1646  */
    break;

  case 149:
#line 1106 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES)
        current_part->flags |= AVRPART_ENABLEPAGEPROGRAMMING;
      else if ((yyvsp[0])->primary == K_NO)
        current_part->flags &= ~AVRPART_ENABLEPAGEPROGRAMMING;

      free_token((yyvsp[0]));
    }
#line 2968 "config_gram.c" /* yacc.c:1646  */
    break;

  case 150:
#line 1116 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->idr = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2977 "config_gram.c" /* yacc.c:1646  */
    break;

  case 151:
#line 1122 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->rampz = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2986 "config_gram.c" /* yacc.c:1646  */
    break;

  case 152:
#line 1128 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->spmcr = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 2995 "config_gram.c" /* yacc.c:1646  */
    break;

  case 153:
#line 1134 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->eecr = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3004 "config_gram.c" /* yacc.c:1646  */
    break;

  case 154:
#line 1140 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->mcu_base = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3013 "config_gram.c" /* yacc.c:1646  */
    break;

  case 155:
#line 1146 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->nvm_base = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3022 "config_gram.c" /* yacc.c:1646  */
    break;

  case 156:
#line 1152 "config_gram.y" /* yacc.c:1646  */
    {
      current_part->ocdrev = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3031 "config_gram.c" /* yacc.c:1646  */
    break;

  case 157:
#line 1158 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES)
        current_part->flags |= AVRPART_SERIALOK;
      else if ((yyvsp[0])->primary == K_NO)
        current_part->flags &= ~AVRPART_SERIALOK;

      free_token((yyvsp[0]));
    }
#line 3044 "config_gram.c" /* yacc.c:1646  */
    break;

  case 158:
#line 1168 "config_gram.y" /* yacc.c:1646  */
    {
      if ((yyvsp[0])->primary == K_YES) {
        current_part->flags |= AVRPART_PARALLELOK;
        current_part->flags &= ~AVRPART_PSEUDOPARALLEL;
      }
      else if ((yyvsp[0])->primary == K_NO) {
        current_part->flags &= ~AVRPART_PARALLELOK;
        current_part->flags &= ~AVRPART_PSEUDOPARALLEL;
      }
      else if ((yyvsp[0])->primary == K_PSEUDO) {
        current_part->flags |= AVRPART_PARALLELOK;
        current_part->flags |= AVRPART_PSEUDOPARALLEL;
      }


      free_token((yyvsp[0]));
    }
#line 3066 "config_gram.c" /* yacc.c:1646  */
    break;

  case 159:
#line 1187 "config_gram.y" /* yacc.c:1646  */
    {
      switch ((yyvsp[0])->primary) {
        case K_RESET :
          current_part->retry_pulse = PIN_AVR_RESET;
          break;
        case K_SCK :
          current_part->retry_pulse = PIN_AVR_SCK;
          break;
      }

      free_token((yyvsp[-2]));
    }
#line 3083 "config_gram.c" /* yacc.c:1646  */
    break;

  case 160:
#line 1210 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem = avr_new_memtype();
      if (current_mem == NULL) {
        yyerror("could not create mem instance");
        free_token((yyvsp[0]));
        YYABORT;
      }
      strncpy(current_mem->desc, (yyvsp[0])->value.string, AVR_MEMDESCLEN);
      current_mem->desc[AVR_MEMDESCLEN-1] = 0;
      free_token((yyvsp[0]));
    }
#line 3099 "config_gram.c" /* yacc.c:1646  */
    break;

  case 161:
#line 1222 "config_gram.y" /* yacc.c:1646  */
    { 
      AVRMEM * existing_mem;

      existing_mem = avr_locate_mem(current_part, current_mem->desc);
      if (existing_mem != NULL) {
        lrmv_d(current_part->mem, existing_mem);
        avr_free_mem(existing_mem);
      }
      ladd(current_part->mem, current_mem); 
      current_mem = NULL; 
    }
#line 3115 "config_gram.c" /* yacc.c:1646  */
    break;

  case 162:
#line 1234 "config_gram.y" /* yacc.c:1646  */
    {
    { 
      int opnum;
      OPCODE * op;

      opnum = which_opcode((yyvsp[-2]));
      if (opnum < 0) YYABORT;
      op = avr_new_opcode();
      if (op == NULL) {
        yyerror("could not create opcode instance");
        free_token((yyvsp[-2]));
        YYABORT;
      }
      if(0 != parse_cmdbits(op)) YYABORT;
      if (current_part->op[opnum] != NULL) {
        /*yywarning("operation redefined");*/
        avr_free_opcode(current_part->op[opnum]);
      }
      current_part->op[opnum] = op;

      free_token((yyvsp[-2]));
    }
  }
#line 3143 "config_gram.c" /* yacc.c:1646  */
    break;

  case 167:
#line 1273 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->paged = (yyvsp[0])->primary == K_YES ? 1 : 0;
      free_token((yyvsp[0]));
    }
#line 3152 "config_gram.c" /* yacc.c:1646  */
    break;

  case 168:
#line 1279 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->size = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3161 "config_gram.c" /* yacc.c:1646  */
    break;

  case 169:
#line 1286 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->page_size = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3170 "config_gram.c" /* yacc.c:1646  */
    break;

  case 170:
#line 1292 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->num_pages = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3179 "config_gram.c" /* yacc.c:1646  */
    break;

  case 171:
#line 1298 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->offset = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3188 "config_gram.c" /* yacc.c:1646  */
    break;

  case 172:
#line 1304 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->min_write_delay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3197 "config_gram.c" /* yacc.c:1646  */
    break;

  case 173:
#line 1310 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->max_write_delay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3206 "config_gram.c" /* yacc.c:1646  */
    break;

  case 174:
#line 1316 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->pwroff_after_write = (yyvsp[0])->primary == K_YES ? 1 : 0;
      free_token((yyvsp[0]));
    }
#line 3215 "config_gram.c" /* yacc.c:1646  */
    break;

  case 175:
#line 1322 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->readback[0] = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3224 "config_gram.c" /* yacc.c:1646  */
    break;

  case 176:
#line 1328 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->readback[1] = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3233 "config_gram.c" /* yacc.c:1646  */
    break;

  case 177:
#line 1335 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->mode = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3242 "config_gram.c" /* yacc.c:1646  */
    break;

  case 178:
#line 1341 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->delay = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3251 "config_gram.c" /* yacc.c:1646  */
    break;

  case 179:
#line 1347 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->blocksize = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3260 "config_gram.c" /* yacc.c:1646  */
    break;

  case 180:
#line 1353 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->readsize = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3269 "config_gram.c" /* yacc.c:1646  */
    break;

  case 181:
#line 1359 "config_gram.y" /* yacc.c:1646  */
    {
      current_mem->pollindex = (yyvsp[0])->value.number;
      free_token((yyvsp[0]));
    }
#line 3278 "config_gram.c" /* yacc.c:1646  */
    break;

  case 182:
#line 1365 "config_gram.y" /* yacc.c:1646  */
    {
    { 
      int opnum;
      OPCODE * op;

      opnum = which_opcode((yyvsp[-2]));
      if (opnum < 0) YYABORT;
      op = avr_new_opcode();
      if (op == NULL) {
        yyerror("could not create opcode instance");
        free_token((yyvsp[-2]));
        YYABORT;
      }
      if(0 != parse_cmdbits(op)) YYABORT;
      if (current_mem->op[opnum] != NULL) {
        /*yywarning("operation redefined");*/
        avr_free_opcode(current_mem->op[opnum]);
      }
      current_mem->op[opnum] = op;

      free_token((yyvsp[-2]));
    }
  }
#line 3306 "config_gram.c" /* yacc.c:1646  */
    break;


#line 3310 "config_gram.c" /* yacc.c:1646  */
      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYTERROR;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  return yyresult;
}
#line 1391 "config_gram.y" /* yacc.c:1906  */


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
