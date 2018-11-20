/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2005 Joerg Wunsch <j@uriah.heep.sax.de>
 *
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
 * JTAG ICE mkI definitions
 */

/* ICE command codes */
/* 0x20 Get Synch [Resp_OK] */
#define CMD_GET_SYNC ' '

/* 0x31 Single Step [Sync_CRC/EOP] [Resp_OK] */
/* 0x32 Read PC [Sync_CRC/EOP] [Resp_OK] [program counter]
 * [Resp_OK] */
/* 0x33 Write PC [program counter] [Sync_CRC/EOP] [Resp_OK]
 * [Resp_OK] */
/* 0xA2 Firmware Upgrade [upgrade string] [Sync_CRC/EOP] [Resp_OK]
 * [Resp_OK] */
/* 0xA0 Set Device Descriptor [device info] [Sync_CRC/EOP] [Resp_OK]
 * [Resp_OK] */
#define CMD_SET_DEVICE_DESCRIPTOR 0xA0

/* 0x42 Set Parameter [parameter] [setting] [Sync_CRC/EOP] [Resp_OK]
 * [Resp_OK] */
#define CMD_SET_PARAM 'B'

/* 0x46 Forced Stop [Sync_CRC/EOP] [Resp_OK] [checksum][program
 * counter] [Resp_OK] */
#define CMD_STOP 'F'

/* 0x47 Go [Sync_CRC/EOP] [Resp_OK] */
#define CMD_GO 'G'

/* 0x52 Read Memory [memory type] [word count] [start address]
 * [Sync_CRC/EOP] [Resp_OK] [word 0] ... [word n] [checksum]
 * [Resp_OK] */
#define CMD_READ_MEM 'R'

/* 0x53 Get Sign On [Sync_CRC/EOP] [Resp_OK] ["AVRNOCD"] [Resp_OK] */
#define CMD_GET_SIGNON 'S'

/* 0XA1 Erase Page spm [address] [Sync_CRC/EOP] [Resp_OK] [Resp_OK] */

/* 0x57 Write Memory [memory type] [word count] [start address]
 * [Sync_CRC/EOP] [Resp_OK] [Cmd_DATA] [word 0] ... [word n] */
#define CMD_WRITE_MEM 'W'

/* Second half of write memory: the data command.  Undocumented. */
#define CMD_DATA 'h'

/* 0x64 Get Debug Info [Sync_CRC/EOP] [Resp_OK] [0x00] [Resp_OK] */
/* 0x71 Get Parameter [parameter] [Sync_CRC/EOP] [Resp_OK] [setting]
 * [Resp_OK] */
#define CMD_GET_PARAM 'q'

/* 0x78 Reset [Sync_CRC/EOP] [Resp_OK] [Resp_OK] */
#define CMD_RESET 'x'

/* 0xA3 Enter Progmode [Sync_CRC/EOP] [Resp_OK] [Resp_OK] */
#define CMD_ENTER_PROGMODE 0xa3

/* 0xA4 Leave Progmode [Sync_CRC/EOP] [Resp_OK] [Resp_OK] */
#define CMD_LEAVE_PROGMODE 0xa4

/* 0xA5 Chip Erase [Sync_CRC/EOP] [Resp_OK] [Resp_OK] */
#define CMD_CHIP_ERASE 0xa5


/* ICE responses */
#define RESP_OK 'A'
#define RESP_BREAK 'B'
#define RESP_INFO 'G'
#define RESP_FAILED 'F'
#define RESP_SYNC_ERROR 'E'
#define RESP_SLEEP 'H'
#define RESP_POWER 'I'

#define PARM_BITRATE 'b'
#define PARM_SW_VERSION 0x7b
#define PARM_HW_VERSION 0x7a
#define PARM_IREG_HIGH 0x81
#define PARM_IREG_LOW 0x82
#define PARM_OCD_VTARGET 0x84
#define PARM_OCD_BREAK_CAUSE 0x85
#define PARM_CLOCK 0x86
#define PARM_EXTERNAL_RESET 0x8b
#define PARM_FLASH_PAGESIZE_LOW 0x88
#define PARM_FLASH_PAGESIZE_HIGH 0x89
#define PARM_EEPROM_PAGESIZE 0x8a
#define PARM_TIMERS_RUNNING 0xa0
#define PARM_BP_FLOW 0xa1
#define PARM_BP_X_HIGH 0xa2
#define PARM_BP_X_LOW 0xa3
#define PARM_BP_Y_HIGH 0xa4
#define PARM_BP_Y_LOW 0xa5
#define PARM_BP_MODE 0xa6
#define PARM_JTAGID_BYTE0 0xa7
#define PARM_JTAGID_BYTE1 0xa8
#define PARM_JTAGID_BYTE2 0xa9
#define PARM_JTAGID_BYTE3 0xaa
#define PARM_UNITS_BEFORE 0xab
#define PARM_UNITS_AFTER 0xac
#define PARM_BIT_BEFORE 0xad
#define PARM_BIT_AFTER 0xae
#define PARM_PSB0_LOW 0xaf
#define PARM_PSBO_HIGH 0xb0
#define PARM_PSB1_LOW 0xb1
#define PARM_PSB1_HIGH 0xb2
#define PARM_MCU_MODE 0xb3

#define JTAG_BITRATE_1_MHz   0xff
#define JTAG_BITRATE_500_kHz 0xfe
#define JTAG_BITRATE_250_kHz 0xfd
#define JTAG_BITRATE_125_kHz 0xfb

/* memory types for CMND_{READ,WRITE}_MEMORY */
#define MTYPE_IO_SHADOW 0x30	/* cached IO registers? */
#define MTYPE_SRAM 0x20		/* target's SRAM or [ext.] IO registers */
#define MTYPE_EEPROM 0x22	/* EEPROM, what way? */
#define MTYPE_EVENT 0x60	/* ICE event memory */
#define MTYPE_SPM 0xA0		/* flash through LPM/SPM */
#define MTYPE_FLASH_PAGE 0xB0	/* flash in programming mode */
#define MTYPE_EEPROM_PAGE 0xB1	/* EEPROM in programming mode */
#define MTYPE_FUSE_BITS 0xB2	/* fuse bits in programming mode */
#define MTYPE_LOCK_BITS 0xB3	/* lock bits in programming mode */
#define MTYPE_SIGN_JTAG 0xB4	/* signature in programming mode */
#define MTYPE_OSCCAL_BYTE 0xB5	/* osccal cells in programming mode */

struct device_descriptor
{
  unsigned char ucReadIO[8]; /*LSB = IOloc 0, MSB = IOloc63 */
  unsigned char ucWriteIO[8]; /*LSB = IOloc 0, MSB = IOloc63 */
  unsigned char ucReadIOShadow[8]; /*LSB = IOloc 0, MSB = IOloc63 */
  unsigned char ucWriteIOShadow[8]; /*LSB = IOloc 0, MSB = IOloc63 */
  unsigned char ucReadExtIO[20]; /*LSB = IOloc 96, MSB = IOloc255 */
  unsigned char ucWriteExtIO[20]; /*LSB = IOloc 96, MSB = IOloc255 */
  unsigned char ucReadIOExtShadow[20]; /*LSB = IOloc 96, MSB = IOloc255 */
  unsigned char ucWriteIOExtShadow[20];/*LSB = IOloc 96, MSB = IOloc255 */
  unsigned char ucIDRAddress; /*IDR address */
  unsigned char ucSPMCRAddress; /*SPMCR Register address and dW BasePC */
  unsigned char ucRAMPZAddress; /*RAMPZ Register address in SRAM I/O */
                                /*space */
  unsigned char uiFlashPageSize[2]; /*Device Flash Page Size, Size = */
                                /*2 exp ucFlashPageSize */
  unsigned char ucEepromPageSize; /*Device Eeprom Page Size in bytes */
  unsigned char ulBootAddress[4]; /*Device Boot Loader Start Address */
  unsigned char uiUpperExtIOLoc; /*Topmost (last) extended I/O */
                                /*location, 0 if no external I/O */
};
