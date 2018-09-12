/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2006  Thomas Fischl
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

#ifndef usbasp_h
#define usbasp_h

/* USB function call identifiers */
#define USBASP_FUNC_CONNECT    1
#define USBASP_FUNC_DISCONNECT 2
#define USBASP_FUNC_TRANSMIT   3
#define USBASP_FUNC_READFLASH  4
#define USBASP_FUNC_ENABLEPROG 5
#define USBASP_FUNC_WRITEFLASH 6
#define USBASP_FUNC_READEEPROM 7
#define USBASP_FUNC_WRITEEEPROM 8
#define USBASP_FUNC_SETLONGADDRESS 9
#define USBASP_FUNC_SETISPSCK 10
#define USBASP_FUNC_TPI_CONNECT      11
#define USBASP_FUNC_TPI_DISCONNECT   12
#define USBASP_FUNC_TPI_RAWREAD      13
#define USBASP_FUNC_TPI_RAWWRITE     14
#define USBASP_FUNC_TPI_READBLOCK    15
#define USBASP_FUNC_TPI_WRITEBLOCK   16
#define USBASP_FUNC_GETCAPABILITIES 127

/* USBASP capabilities */
#define USBASP_CAP_TPI    0x01

/* Block mode flags */
#define USBASP_BLOCKFLAG_FIRST    1
#define USBASP_BLOCKFLAG_LAST     2

/* Block mode data size */
#define USBASP_READBLOCKSIZE   200
#define USBASP_WRITEBLOCKSIZE  200

/* ISP SCK speed identifiers */
#define USBASP_ISP_SCK_AUTO   0
#define USBASP_ISP_SCK_0_5    1   /* 500 Hz */
#define USBASP_ISP_SCK_1      2   /*   1 kHz */
#define USBASP_ISP_SCK_2      3   /*   2 kHz */
#define USBASP_ISP_SCK_4      4   /*   4 kHz */
#define USBASP_ISP_SCK_8      5   /*   8 kHz */
#define USBASP_ISP_SCK_16     6   /*  16 kHz */
#define USBASP_ISP_SCK_32     7   /*  32 kHz */
#define USBASP_ISP_SCK_93_75  8   /*  93.75 kHz */
#define USBASP_ISP_SCK_187_5  9   /* 187.5  kHz */
#define USBASP_ISP_SCK_375    10  /* 375 kHz   */
#define USBASP_ISP_SCK_750    11  /* 750 kHz   */
#define USBASP_ISP_SCK_1500   12  /* 1.5 MHz   */

/* TPI instructions */
#define TPI_OP_SLD      0x20
#define TPI_OP_SLD_INC  0x24
#define TPI_OP_SST      0x60
#define TPI_OP_SST_INC  0x64
#define TPI_OP_SSTPR(a) (0x68 | (a))
#define TPI_OP_SIN(a)   (0x10 | (((a)<<1)&0x60) | ((a)&0x0F) )
#define TPI_OP_SOUT(a)  (0x90 | (((a)<<1)&0x60) | ((a)&0x0F) )
#define TPI_OP_SLDCS(a) (0x80 | ((a)&0x0F) )
#define TPI_OP_SSTCS(a) (0xC0 | ((a)&0x0F) )
#define TPI_OP_SKEY     0xE0

/* TPI control/status registers */
#define TPIIR  0xF
#define TPIPCR 0x2
#define TPISR  0x0

// TPIPCR bits
#define TPIPCR_GT_2    0x04
#define TPIPCR_GT_1    0x02
#define TPIPCR_GT_0    0x01
#define TPIPCR_GT_128b 0x00
#define TPIPCR_GT_64b  0x01
#define TPIPCR_GT_32b  0x02
#define TPIPCR_GT_16b  0x03
#define TPIPCR_GT_8b   0x04
#define TPIPCR_GT_4b   0x05
#define TPIPCR_GT_2b   0x06
#define TPIPCR_GT_0b   0x07

// TPISR bits
#define TPISR_NVMEN    0x02

/* NVM registers */
#define NVMCSR         0x32
#define NVMCMD         0x33

// NVMCSR bits
#define NVMCSR_BSY     0x80

// NVMCMD values
#define NVMCMD_NOP           0x00
#define NVMCMD_CHIP_ERASE    0x10
#define NVMCMD_SECTION_ERASE 0x14
#define NVMCMD_WORD_WRITE    0x1D


typedef struct sckoptions_t {
  int id;
  double frequency;
} CLOCKOPTIONS;

/* USB error identifiers */
#define USB_ERROR_NOTFOUND  1
#define USB_ERROR_ACCESS    2
#define USB_ERROR_IO        3

#ifdef __cplusplus
extern "C" {
#endif

extern const char usbasp_desc[];
void usbasp_initpgm (PROGRAMMER * pgm);

#ifdef __cplusplus
}
#endif

#endif /* usbasp_h */
