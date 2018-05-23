/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2005, 2006 Joerg Wunsch <j@uriah.heep.sax.de>
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
 * JTAG ICE mkII definitions
 * Taken from Appnote AVR067
 */

#if !defined(JTAGMKII_PRIVATE_EXPORTED)
/*
 * Communication with the JTAG ICE works in frames.  The protocol
 * somewhat resembles the STK500v2 protocol, yet it is sufficiently
 * different to prevent a direct code reuse. :-(
 *
 * Frame format:
 *
 *  +---------------------------------------------------------------+
 *  |   0   |  1  .  2  |  3 . 4 . 5 . 6  |   7   | ... | N-1 .  N  |
 *  |       |           |                 |       |     |           |
 *  | start | LSB   MSB | LSB ....... MSB | token | msg | LSB   MSB |
 *  | 0x1B  | sequence# | message size    | 0x0E  |     |   CRC16   |
 *  +---------------------------------------------------------------+
 *
 * Each request message will be returned by a response with a matching
 * sequence #.  Sequence # 0xffff is reserved for asynchronous event
 * notifications that will be sent by the ICE without a request
 * message (e.g. when the target hit a breakpoint).
 *
 * The message size excludes the framing overhead (10 bytes).
 *
 * The first byte of the message is always the request or response
 * code, which is roughly classified as:
 *
 * . Messages (commands) use 0x00 through 0x3f.  (The documentation
 *   claims that messages start at 0x01, but actually CMND_SIGN_OFF is
 *   0x00.)
 * . Internal commands use 0x40 through 0x7f (not documented).
 * . Success responses use 0x80 through 0x9f.
 * . Failure responses use 0xa0 through 0xbf.
 * . Events use 0xe0 through 0xff.
 */
#define MESSAGE_START 0x1b
#define TOKEN 0x0e

/*
 * Max message size we are willing to accept.  Prevents us from trying
 * to allocate too much VM in case we received a nonsensical packet
 * length.  We have to allocate the buffer as soon as we've got the
 * length information (and thus have to trust that information by that
 * time at first), as the final CRC check can only be done once the
 * entire packet came it.
 */
#define MAX_MESSAGE 100000

#endif /* JTAGMKII_PRIVATE_EXPORTED */

/* ICE command codes */
#define CMND_SIGN_OFF              0x00
#define CMND_GET_SIGN_ON           0x01
#define CMND_SET_PARAMETER         0x02
#define CMND_GET_PARAMETER         0x03
#define CMND_WRITE_MEMORY          0x04
#define CMND_READ_MEMORY           0x05
#define CMND_WRITE_PC              0x06
#define CMND_READ_PC               0x07
#define CMND_GO                    0x08
#define CMND_SINGLE_STEP           0x09
#define CMND_FORCED_STOP           0x0A
#define CMND_RESET                 0x0B
#define CMND_SET_DEVICE_DESCRIPTOR 0x0C
#define CMND_ERASEPAGE_SPM         0x0D
#define CMND_GET_SYNC              0x0f
#define CMND_SELFTEST              0x10
#define CMND_SET_BREAK             0x11
#define CMND_GET_BREAK             0x12
#define CMND_CHIP_ERASE            0x13
#define CMND_ENTER_PROGMODE        0x14
#define CMND_LEAVE_PROGMODE        0x15
#define CMND_SET_N_PARAMETERS      0x16
#define CMND_CLR_BREAK             0x1A
#define CMND_RUN_TO_ADDR           0x1C
#define CMND_SPI_CMD               0x1D
#define CMND_CLEAR_EVENTS          0x22
#define CMND_RESTORE_TARGET        0x23
#define CMND_GET_IR                0x24
#define CMND_GET_xxx               0x25
#define CMND_WRITE_SAB             0x28
#define CMND_READ_SAB              0x29
#define CMND_RESET_AVR             0x2B
#define CMND_READ_MEMORY32         0x2C
#define CMND_WRITE_MEMORY32        0x2D
#define CMND_ISP_PACKET            0x2F
#define CMND_XMEGA_ERASE           0x34
#define CMND_SET_XMEGA_PARAMS      0x36  // undocumented in AVR067


/* ICE responses */
#define RSP_OK                     0x80
#define RSP_PARAMETER              0x81
#define RSP_MEMORY                 0x82
#define RSP_GET_BREAK              0x83
#define RSP_PC                     0x84
#define RSP_SELFTEST               0x85
#define RSP_SIGN_ON                0x86
#define RSP_SPI_DATA               0x88
#define RSP_FAILED                 0xA0
#define RSP_ILLEGAL_PARAMETER      0xA1
#define RSP_ILLEGAL_MEMORY_TYPE    0xA2
#define RSP_ILLEGAL_MEMORY_RANGE   0xA3
#define RSP_ILLEGAL_EMULATOR_MODE  0xA4
#define RSP_ILLEGAL_MCU_STATE      0xA5
#define RSP_ILLEGAL_VALUE          0xA6
#define RSP_SET_N_PARAMETERS       0xA7
#define RSP_ILLEGAL_BREAKPOINT     0xA8
#define RSP_ILLEGAL_JTAG_ID        0xA9
#define RSP_ILLEGAL_COMMAND        0xAA
#define RSP_NO_TARGET_POWER        0xAB
#define RSP_DEBUGWIRE_SYNC_FAILED  0xAC
#define RSP_ILLEGAL_POWER_STATE    0xAD

/* ICE events */
#define EVT_BREAK                           0xE0
#define EVT_RUN                             0xE1
#define EVT_ERROR_PHY_FORCE_BREAK_TIMEOUT   0xE2
#define EVT_ERROR_PHY_RELEASE_BREAK_TIMEOUT 0xE3
#define EVT_TARGET_POWER_ON                 0xE4
#define EVT_TARGET_POWER_OFF                0xE5
#define EVT_DEBUG                           0xE6
#define EVT_EXT_RESET                       0xE7
#define EVT_TARGET_SLEEP                    0xE8
#define EVT_TARGET_WAKEUP                   0xE9
#define EVT_ICE_POWER_ERROR_STATE           0xEA
#define EVT_ICE_POWER_OK                    0xEB
#define EVT_IDR_DIRTY                       0xEC
#define EVT_ERROR_PHY_MAX_BIT_LENGTH_DIFF   0xED
#define EVT_NONE                            0xEF
#define EVT_ERROR_PHY_SYNC_TIMEOUT          0xF0
#define EVT_PROGRAM_BREAK                   0xF1
#define EVT_PDSB_BREAK                      0xF2
#define EVT_PDSMB_BREAK                     0xF3
#define EVT_ERROR_PHY_SYNC_TIMEOUT_BAUD     0xF4
#define EVT_ERROR_PHY_SYNC_OUT_OF_RANGE     0xF5
#define EVT_ERROR_PHY_SYNC_WAIT_TIMEOUT     0xF6
#define EVT_ERROR_PHY_RECEIVE_TIMEOUT       0xF7
#define EVT_ERROR_PHY_RECEIVED_BREAK        0xF8
#define EVT_ERROR_PHY_OPT_RECEIVE_TIMEOUT   0xF9
#define EVT_ERROR_PHY_OPT_RECEIVED_BREAK    0xFA
#define EVT_RESULT_PHY_NO_ACTIVITY          0xFB

/* memory types for CMND_{READ,WRITE}_MEMORY */
#define MTYPE_IO_SHADOW   0x30	/* cached IO registers? */
#define MTYPE_SRAM        0x20	/* target's SRAM or [ext.] IO registers */
#define MTYPE_EEPROM      0x22	/* EEPROM, what way? */
#define MTYPE_EVENT       0x60	/* ICE event memory */
#define MTYPE_SPM         0xA0	/* flash through LPM/SPM */
#define MTYPE_FLASH_PAGE  0xB0	/* flash in programming mode */
#define MTYPE_EEPROM_PAGE 0xB1	/* EEPROM in programming mode */
#define MTYPE_FUSE_BITS   0xB2	/* fuse bits in programming mode */
#define MTYPE_LOCK_BITS   0xB3	/* lock bits in programming mode */
#define MTYPE_SIGN_JTAG   0xB4	/* signature in programming mode */
#define MTYPE_OSCCAL_BYTE 0xB5	/* osccal cells in programming mode */
#define MTYPE_CAN         0xB6	/* CAN mailbox */
#define MTYPE_FLASH       0xc0	/* xmega (app.) flash - undocumented in AVR067 */
#define MTYPE_BOOT_FLASH  0xc1	/* xmega boot flash - undocumented in AVR067 */
#define MTYPE_EEPROM_XMEGA 0xc4	/* xmega EEPROM in debug mode - undocumented in AVR067 */
#define MTYPE_USERSIG     0xc5	/* xmega user signature - undocumented in AVR067 */
#define MTYPE_PRODSIG     0xc6	/* xmega production signature - undocumented in AVR067 */

/* (some) ICE parameters, for CMND_{GET,SET}_PARAMETER */
#define PAR_HW_VERSION                         0x01
#define PAR_FW_VERSION                         0x02
#define PAR_EMULATOR_MODE                      0x03
# define EMULATOR_MODE_DEBUGWIRE                 0x00
# define EMULATOR_MODE_JTAG                      0x01
# define EMULATOR_MODE_HV                        0x02	/* HVSP or PP mode of AVR Dragon */
# define EMULATOR_MODE_SPI                       0x03
# define EMULATOR_MODE_JTAG_AVR32                0x04
# define EMULATOR_MODE_JTAG_XMEGA                0x05
# define EMULATOR_MODE_PDI                       0x06
#define PAR_IREG                               0x04
#define PAR_BAUD_RATE                          0x05
# define PAR_BAUD_2400                           0x01
# define PAR_BAUD_4800                           0x02
# define PAR_BAUD_9600                           0x03
# define PAR_BAUD_19200                          0x04	/* default */
# define PAR_BAUD_38400                          0x05
# define PAR_BAUD_57600                          0x06
# define PAR_BAUD_115200                         0x07
# define PAR_BAUD_14400                          0x08
#define PAR_OCD_VTARGET                        0x06
#define PAR_OCD_JTAG_CLK                       0x07
#define PAR_OCD_BREAK_CAUSE                    0x08
#define PAR_TIMERS_RUNNING                     0x09
#define PAR_BREAK_ON_CHANGE_FLOW               0x0A
#define PAR_BREAK_ADDR1                        0x0B
#define PAR_BREAK_ADDR2                        0x0C
#define PAR_COMBBREAKCTRL                      0x0D
#define PAR_JTAGID                             0x0E
#define PAR_UNITS_BEFORE                       0x0F
#define PAR_UNITS_AFTER                        0x10
#define PAR_BIT_BEFORE                         0x11
#define PAR_BIT_ATER                           0x12
#define PAR_EXTERNAL_RESET                     0x13
#define PAR_FLASH_PAGE_SIZE                    0x14
#define PAR_EEPROM_PAGE_SIZE                   0x15
#define PAR_UNUSED1                            0x16
#define PAR_PSB0                               0x17
#define PAR_PSB1                               0x18
#define PAR_PROTOCOL_DEBUG_EVENT               0x19
#define PAR_MCU_STATE                          0x1A
# define STOPPED                                 0x00
# define RUNNING                                 0x01
# define PROGRAMMING                             0x02
#define PAR_DAISY_CHAIN_INFO                   0x1B
#define PAR_BOOT_ADDRESS                       0x1C
#define PAR_TARGET_SIGNATURE                   0x1D
#define PAR_DEBUGWIRE_BAUDRATE                 0x1E
#define PAR_PROGRAM_ENTRY_POINT                0x1F
#define PAR_PDI_OFFSET_START                   0x32
#define PAR_PDI_OFFSET_END                     0x33
#define PAR_PACKET_PARSING_ERRORS              0x40
#define PAR_VALID_PACKETS_RECEIVED             0x41
#define PAR_INTERCOMMUNICATION_TX_FAILURES     0x42
#define PAR_INTERCOMMUNICATION_RX_FAILURES     0x43
#define PAR_CRC_ERRORS                         0x44
#define PAR_POWER_SOURCE                       0x45
# define POWER_EXTERNAL                          0x00
# define POWER_USB                               0x01
#define PAR_CAN_FLAG                           0x22
# define DONT_READ_CAN_MAILBOX                   0x00
# define READ_CAN_MAILBOX                        0x01
#define PAR_ENABLE_IDR_IN_RUN_MODE             0x23
# define ACCESS_OSCCAL                           0x00
# define ACCESS_IDR                              0x01
#define PAR_ALLOW_PAGEPROGRAMMING_IN_SCANCHAIN 0x24
# define PAGEPROG_NOT_ALLOWED                    0x00
# define PAGEPROG_ALLOWED                        0x01

/* Xmega erase memory types, for CMND_XMEGA_ERASE */
#define XMEGA_ERASE_CHIP        0x00
#define XMEGA_ERASE_APP         0x01
#define XMEGA_ERASE_BOOT        0x02
#define XMEGA_ERASE_EEPROM      0x03
#define XMEGA_ERASE_APP_PAGE    0x04
#define XMEGA_ERASE_BOOT_PAGE   0x05
#define XMEGA_ERASE_EEPROM_PAGE 0x06
#define XMEGA_ERASE_USERSIG     0x07

/* AVR32 related definitions */
#define AVR32_FLASHC_FCR                  0xFFFE1400
#define AVR32_FLASHC_FCMD                 0xFFFE1404
#define   AVR32_FLASHC_FCMD_KEY           0xA5000000
#define   AVR32_FLASHC_FCMD_WRITE_PAGE             1
#define   AVR32_FLASHC_FCMD_ERASE_PAGE             2
#define   AVR32_FLASHC_FCMD_CLEAR_PAGE_BUFFER      3
#define   AVR32_FLASHC_FCMD_LOCK                   4
#define   AVR32_FLASHC_FCMD_UNLOCK                 5
#define AVR32_FLASHC_FSR                  0xFFFE1408
#define   AVR32_FLASHC_FSR_RDY            0x00000001
#define   AVR32_FLASHC_FSR_ERR            0x00000008
#define AVR32_FLASHC_FGPFRHI              0xFFFE140C
#define AVR32_FLASHC_FGPFRLO              0xFFFE1410

#define AVR32_DC                          0x00000008
#define AVR32_DS                          0x00000010
#define AVR32_DINST                       0x00000104
#define AVR32_DCCPU                       0x00000110
#define AVR32_DCEMU                       0x00000114
#define AVR32_DCSR                        0x00000118

#define AVR32_DC_ABORT                    0x80000000
#define AVR32_DC_RESET                    0x40000000
#define AVR32_DC_DBE                      0x00002000
#define AVR32_DC_DBR                      0x00001000

#define AVR32_RESET_READ             0x0001
#define AVR32_RESET_WRITE            0x0002
#define AVR32_RESET_CHIP_ERASE       0x0004
#define AVR32_SET4RUNNING            0x0008
//#define AVR32_RESET_COMMON           (AVR32_RESET_READ | AVR32_RESET_WRITE | AVR32_RESET_CHIP_ERASE )


#if !defined(JTAGMKII_PRIVATE_EXPORTED)
/*
 * In appnote AVR067, struct device_descriptor is written with
 * int/long field types.  We cannot use them directly, as they were
 * neither properly aligned for portability, nor did they care for
 * endianess issues.  We thus use arrays of unsigned chars, plus
 * conversion macros.
 */
struct device_descriptor
{
  unsigned char ucReadIO[8]; /*LSB = IOloc 0, MSB = IOloc63 */
  unsigned char ucReadIOShadow[8]; /*LSB = IOloc 0, MSB = IOloc63 */
  unsigned char ucWriteIO[8]; /*LSB = IOloc 0, MSB = IOloc63 */
  unsigned char ucWriteIOShadow[8]; /*LSB = IOloc 0, MSB = IOloc63 */
  unsigned char ucReadExtIO[52]; /*LSB = IOloc 96, MSB = IOloc511 */
  unsigned char ucReadIOExtShadow[52]; /*LSB = IOloc 96, MSB = IOloc511 */
  unsigned char ucWriteExtIO[52]; /*LSB = IOloc 96, MSB = IOloc511 */
  unsigned char ucWriteIOExtShadow[52];/*LSB = IOloc 96, MSB = IOloc511 */
  unsigned char ucIDRAddress; /*IDR address */
  unsigned char ucSPMCRAddress; /*SPMCR Register address and dW BasePC */
  unsigned char ucRAMPZAddress; /*RAMPZ Register address in SRAM I/O */
				/*space */
  unsigned char uiFlashPageSize[2]; /*Device Flash Page Size, Size = */
				/*2 exp ucFlashPageSize */
  unsigned char ucEepromPageSize; /*Device Eeprom Page Size in bytes */
  unsigned char ulBootAddress[4]; /*Device Boot Loader Start Address */
  unsigned char uiUpperExtIOLoc[2]; /*Topmost (last) extended I/O */
				/*location, 0 if no external I/O */
  unsigned char ulFlashSize[4]; /*Device Flash Size */
  unsigned char ucEepromInst[20]; /*Instructions for W/R EEPROM */
  unsigned char ucFlashInst[3]; /*Instructions for W/R FLASH */
  unsigned char ucSPHaddr; /* stack pointer high */
  unsigned char ucSPLaddr; /* stack pointer low */
  /* new as of 16-02-2004 */
  unsigned char uiFlashpages[2]; /* number of pages in flash */
  unsigned char ucDWDRAddress; /* DWDR register address */
  unsigned char ucDWBasePC; /* base/mask value of the PC */
  /* new as of 30-04-2004 */
  unsigned char ucAllowFullPageBitstream; /* FALSE on ALL new */
				/*parts */
  unsigned char uiStartSmallestBootLoaderSection[2]; /* */
  /* new as of 18-10-2004 */
  unsigned char EnablePageProgramming; /* For JTAG parts only, */
				/* default TRUE */
  unsigned char ucCacheType;	/* CacheType_Normal 0x00, */
				/* CacheType_CAN 0x01, */
				/* CacheType_HEIMDALL 0x02 */
				/* new as of 27-10-2004 */
  unsigned char uiSramStartAddr[2]; /* Start of SRAM */
  unsigned char ucResetType; /* Selects reset type. ResetNormal = 0x00 */
                             /* ResetAT76CXXX = 0x01 */
  unsigned char ucPCMaskExtended; /* For parts with extended PC */
  unsigned char ucPCMaskHigh; /* PC high mask */
  unsigned char ucEindAddress; /* Selects reset type. [EIND address...] */
  /* new as of early 2005, firmware 4.x */
  unsigned char EECRAddress[2]; /* EECR memory-mapped IO address */
};

/* New Xmega device descriptor, for firmware version 7 and above */
struct xmega_device_desc {
    unsigned char whatever[2];		// cannot guess; must be 0x0002
    unsigned char datalen;		// length of the following data, = 47
    unsigned char nvm_app_offset[4];	// NVM offset for application flash
    unsigned char nvm_boot_offset[4];	// NVM offset for boot flash
    unsigned char nvm_eeprom_offset[4]; // NVM offset for EEPROM
    unsigned char nvm_fuse_offset[4];	// NVM offset for fuses
    unsigned char nvm_lock_offset[4];	// NVM offset for lock bits
    unsigned char nvm_user_sig_offset[4]; // NVM offset for user signature row
    unsigned char nvm_prod_sig_offset[4]; // NVM offset for production sign. row
    unsigned char nvm_data_offset[4];	// NVM offset for data memory (SRAM + IO)
    unsigned char app_size[4];		// size of application flash
    unsigned char boot_size[2];		// size of boot flash
    unsigned char flash_page_size[2];	// flash page size
    unsigned char eeprom_size[2];	// size of EEPROM
    unsigned char eeprom_page_size;	// EEPROM page size
    unsigned char nvm_base_addr[2];	// IO space base address of NVM controller
    unsigned char mcu_base_addr[2];	// IO space base address of MCU control
};
#endif /* JTAGMKII_PRIVATE_EXPORTED */

/* return code from jtagmkII_getsync() to indicate a "graceful"
 * failure, i.e. an attempt to enable ISP failed and should be
 * eventually retried */
#define JTAGII_GETSYNC_FAIL_GRACEFUL (-2)
