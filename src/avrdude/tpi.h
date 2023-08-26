/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2011  Darell Tan <darell.tan@gmail.com>
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

#ifndef tpi_h
#define tpi_h

#ifdef __cplusplus
extern "C" {
#endif

static const unsigned char tpi_skey[] = { 0x12, 0x89, 0xAB, 0x45, 0xCD, 0xD8, 0x88, 0xFF };

/* registers */
#define TPI_REG_TPIIR  0x0F

#define TPI_IDENT_CODE 0x80

#define TPI_REG_TPIPCR 0x02
#define TPI_REG_TPISR  0x00

#define TPI_REG_TPISR_NVMEN		(1 << 1)

/* TPI commands */
#define TPI_CMD_SLD		0x20
#define TPI_CMD_SLD_PI	0x24
#define TPI_CMD_SIN		0x10
#define TPI_CMD_SOUT	0x90
#define TPI_CMD_SSTCS	0xC0
#define TPI_CMD_SST		0x60
#define TPI_CMD_SST_PI	0x64

#define TPI_CMD_SLDCS	0x80
#define TPI_CMD_SSTPR	0x68
#define TPI_CMD_SKEY	0xE0

/* for TPI_CMD_SIN & TPI_CMD_SOUT */
#define TPI_SIO_ADDR(x) ((x & 0x30) << 1 | (x & 0x0F)) 

/* ATtiny4/5/9/10 I/O registers */
#define TPI_IOREG_NVMCSR		0x32
#define TPI_IOREG_NVMCMD		0x33

/* bit for NVMCSR */
#define TPI_IOREG_NVMCSR_NVMBSY	(1 << 7)

/* NVM commands */
#define TPI_NVMCMD_NO_OPERATION		0x00
#define TPI_NVMCMD_CHIP_ERASE		0x10
#define TPI_NVMCMD_SECTION_ERASE	0x14
#define TPI_NVMCMD_WORD_WRITE		0x1D

static const unsigned char tpi_skey_cmd[] = { TPI_CMD_SKEY, 0xff, 0x88, 0xd8, 0xcd, 0x45, 0xab, 0x89, 0x12 };

#ifdef __cplusplus
}
#endif

#endif

