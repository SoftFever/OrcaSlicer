//**** ATMEL AVR - A P P L I C A T I O N   N O T E  ************************
//*
//* Title:		AVR068 - STK500 Communication Protocol
//* Filename:		command.h
//* Version:		1.0
//* Last updated:	10.01.2005
//*
//* Support E-mail:	avr@atmel.com
//*
//**************************************************************************

// *****************[ STK message constants ]***************************

#define MESSAGE_START                       0x1B        //= ESC = 27 decimal
#define TOKEN                               0x0E

// *****************[ STK general command constants ]**************************

#define CMD_SIGN_ON                         0x01
#define CMD_SET_PARAMETER                   0x02
#define CMD_GET_PARAMETER                   0x03
#define CMD_SET_DEVICE_PARAMETERS           0x04
#define CMD_OSCCAL                          0x05
#define CMD_LOAD_ADDRESS                    0x06
#define CMD_FIRMWARE_UPGRADE                0x07
#define CMD_CHECK_TARGET_CONNECTION         0x0D
#define CMD_LOAD_RC_ID_TABLE                0x0E
#define CMD_LOAD_EC_ID_TABLE                0x0F

// *****************[ STK ISP command constants ]******************************

#define CMD_ENTER_PROGMODE_ISP              0x10
#define CMD_LEAVE_PROGMODE_ISP              0x11
#define CMD_CHIP_ERASE_ISP                  0x12
#define CMD_PROGRAM_FLASH_ISP               0x13
#define CMD_READ_FLASH_ISP                  0x14
#define CMD_PROGRAM_EEPROM_ISP              0x15
#define CMD_READ_EEPROM_ISP                 0x16
#define CMD_PROGRAM_FUSE_ISP                0x17
#define CMD_READ_FUSE_ISP                   0x18
#define CMD_PROGRAM_LOCK_ISP                0x19
#define CMD_READ_LOCK_ISP                   0x1A
#define CMD_READ_SIGNATURE_ISP              0x1B
#define CMD_READ_OSCCAL_ISP                 0x1C
#define CMD_SPI_MULTI                       0x1D /* STK500v2, AVRISPmkII,
						  * JTAGICEmkII */
#define CMD_SET_SCK                         0x1D /* JTAGICE3 */
#define CMD_GET_SCK                         0x1E /* JTAGICE3 */

// *****************[ STK PP command constants ]*******************************

#define CMD_ENTER_PROGMODE_PP               0x20
#define CMD_LEAVE_PROGMODE_PP               0x21
#define CMD_CHIP_ERASE_PP                   0x22
#define CMD_PROGRAM_FLASH_PP                0x23
#define CMD_READ_FLASH_PP                   0x24
#define CMD_PROGRAM_EEPROM_PP               0x25
#define CMD_READ_EEPROM_PP                  0x26
#define CMD_PROGRAM_FUSE_PP                 0x27
#define CMD_READ_FUSE_PP                    0x28
#define CMD_PROGRAM_LOCK_PP                 0x29
#define CMD_READ_LOCK_PP                    0x2A
#define CMD_READ_SIGNATURE_PP               0x2B
#define CMD_READ_OSCCAL_PP                  0x2C

#define CMD_SET_CONTROL_STACK               0x2D

// *****************[ STK HVSP command constants ]*****************************

#define CMD_ENTER_PROGMODE_HVSP             0x30
#define CMD_LEAVE_PROGMODE_HVSP             0x31
#define CMD_CHIP_ERASE_HVSP                 0x32
#define CMD_PROGRAM_FLASH_HVSP              0x33
#define CMD_READ_FLASH_HVSP                 0x34
#define CMD_PROGRAM_EEPROM_HVSP             0x35
#define CMD_READ_EEPROM_HVSP                0x36
#define CMD_PROGRAM_FUSE_HVSP               0x37
#define CMD_READ_FUSE_HVSP                  0x38
#define CMD_PROGRAM_LOCK_HVSP               0x39
#define CMD_READ_LOCK_HVSP                  0x3A
#define CMD_READ_SIGNATURE_HVSP             0x3B
#define CMD_READ_OSCCAL_HVSP                0x3C
// These two are redefined since 0x30/0x31 collide
// with the STK600 bootloader.
#define CMD_ENTER_PROGMODE_HVSP_STK600      0x3D
#define CMD_LEAVE_PROGMODE_HVSP_STK600      0x3E

// *** XPROG command constants ***

#define CMD_XPROG                           0x50
#define CMD_XPROG_SETMODE                   0x51


// *****************[ STK Prusa3D specific command constants ]*****************

#define CMD_SET_UPLOAD_SIZE_PRUSA3D         0x71


// *** AVR32 JTAG Programming command ***

#define CMD_JTAG_AVR32                      0x80
#define CMD_ENTER_PROGMODE_JTAG_AVR32       0x81
#define CMD_LEAVE_PROGMODE_JTAG_AVR32       0x82


// *** AVR JTAG Programming command ***

#define CMD_JTAG_AVR                        0x90

// *****************[ STK test command constants ]***************************

#define CMD_ENTER_TESTMODE                  0x60
#define CMD_LEAVE_TESTMODE                  0x61
#define CMD_CHIP_WRITE                      0x62
#define CMD_PROGRAM_FLASH_PARTIAL           0x63
#define CMD_PROGRAM_EEPROM_PARTIAL          0x64
#define CMD_PROGRAM_SIGNATURE_ROW           0x65
#define CMD_READ_FLASH_MARGIN               0x66
#define CMD_READ_EEPROM_MARGIN              0x67
#define CMD_READ_SIGNATURE_ROW_MARGIN       0x68
#define CMD_PROGRAM_TEST_FUSE               0x69
#define CMD_READ_TEST_FUSE                  0x6A
#define CMD_PROGRAM_HIDDEN_FUSE_LOW         0x6B
#define CMD_READ_HIDDEN_FUSE_LOW            0x6C
#define CMD_PROGRAM_HIDDEN_FUSE_HIGH        0x6D
#define CMD_READ_HIDDEN_FUSE_HIGH           0x6E
#define CMD_PROGRAM_HIDDEN_FUSE_EXT         0x6F
#define CMD_READ_HIDDEN_FUSE_EXT            0x70

// *****************[ STK status constants ]***************************

// Success
#define STATUS_CMD_OK                       0x00

// Warnings
#define STATUS_CMD_TOUT                     0x80
#define STATUS_RDY_BSY_TOUT                 0x81
#define STATUS_SET_PARAM_MISSING            0x82

// Errors
#define STATUS_CMD_FAILED                   0xC0
#define STATUS_CKSUM_ERROR                  0xC1
#define STATUS_CMD_UNKNOWN                  0xC9
#define STATUS_CMD_ILLEGAL_PARAMETER        0xCA

// Status
#define STATUS_ISP_READY                    0x00
#define STATUS_CONN_FAIL_MOSI               0x01
#define STATUS_CONN_FAIL_RST                0x02
#define STATUS_CONN_FAIL_SCK                0x04
#define STATUS_TGT_NOT_DETECTED             0x10
#define STATUS_TGT_REVERSE_INSERTED         0x20

// hw_status
// Bits in status variable
// Bit 0-3: Slave MCU
// Bit 4-7: Master MCU

#define STATUS_AREF_ERROR    0
// Set to '1' if AREF is short circuited

#define STATUS_VTG_ERROR     4
// Set to '1' if VTG is short circuited

#define STATUS_RC_CARD_ERROR 5
// Set to '1' if board id changes when board is powered

#define STATUS_PROGMODE      6
// Set to '1' if board is in programming mode

#define STATUS_POWER_SURGE   7
// Set to '1' if board draws excessive current

// *****************[ STK parameter constants ]***************************
#define PARAM_BUILD_NUMBER_LOW              0x80 /* ??? */
#define PARAM_BUILD_NUMBER_HIGH             0x81 /* ??? */
#define PARAM_HW_VER                        0x90
#define PARAM_SW_MAJOR                      0x91
#define PARAM_SW_MINOR                      0x92
#define PARAM_VTARGET                       0x94
#define PARAM_VADJUST                       0x95 /* STK500 only */
#define PARAM_OSC_PSCALE                    0x96 /* STK500 only */
#define PARAM_OSC_CMATCH                    0x97 /* STK500 only */
#define PARAM_SCK_DURATION                  0x98 /* STK500 only */
#define PARAM_TOPCARD_DETECT                0x9A /* STK500 only */
#define PARAM_STATUS                        0x9C /* STK500 only */
#define PARAM_DATA                          0x9D /* STK500 only */
#define PARAM_RESET_POLARITY                0x9E /* STK500 only, and STK600 FW
                                                  * version <= 2.0.3 */
#define PARAM_CONTROLLER_INIT               0x9F

/* STK600 parameters */
#define PARAM_STATUS_TGT_CONN               0xA1
#define PARAM_DISCHARGEDELAY                0xA4
#define PARAM_SOCKETCARD_ID                 0xA5
#define PARAM_ROUTINGCARD_ID                0xA6
#define PARAM_EXPCARD_ID                    0xA7
#define PARAM_SW_MAJOR_SLAVE1               0xA8
#define PARAM_SW_MINOR_SLAVE1               0xA9
#define PARAM_SW_MAJOR_SLAVE2               0xAA
#define PARAM_SW_MINOR_SLAVE2               0xAB
#define PARAM_BOARD_ID_STATUS               0xAD
#define PARAM_RESET                         0xB4

#define PARAM_JTAG_ALLOW_FULL_PAGE_STREAM   0x50
#define PARAM_JTAG_EEPROM_PAGE_SIZE         0x52
#define PARAM_JTAG_DAISY_BITS_BEFORE        0x53
#define PARAM_JTAG_DAISY_BITS_AFTER         0x54
#define PARAM_JTAG_DAISY_UNITS_BEFORE       0x55
#define PARAM_JTAG_DAISY_UNITS_AFTER        0x56

// *** Parameter constants for 2 byte values ***
#define PARAM2_SCK_DURATION                 0xC0
#define PARAM2_CLOCK_CONF                   0xC1
#define PARAM2_AREF0                        0xC2
#define PARAM2_AREF1                        0xC3

#define PARAM2_JTAG_FLASH_SIZE_H            0xC5
#define PARAM2_JTAG_FLASH_SIZE_L            0xC6
#define PARAM2_JTAG_FLASH_PAGE_SIZE         0xC7
#define PARAM2_RC_ID_TABLE_REV              0xC8
#define PARAM2_EC_ID_TABLE_REV              0xC9

/* STK600 XPROG section */
// XPROG modes
#define XPRG_MODE_PDI                       0
#define XPRG_MODE_JTAG                      1
#define XPRG_MODE_TPI                       2

// XPROG commands
#define XPRG_CMD_ENTER_PROGMODE             0x01
#define XPRG_CMD_LEAVE_PROGMODE             0x02
#define XPRG_CMD_ERASE                      0x03
#define XPRG_CMD_WRITE_MEM                  0x04
#define XPRG_CMD_READ_MEM                   0x05
#define XPRG_CMD_CRC                        0x06
#define XPRG_CMD_SET_PARAM                  0x07

// Memory types
#define XPRG_MEM_TYPE_APPL                   1
#define XPRG_MEM_TYPE_BOOT                   2
#define XPRG_MEM_TYPE_EEPROM                 3
#define XPRG_MEM_TYPE_FUSE                   4
#define XPRG_MEM_TYPE_LOCKBITS               5
#define XPRG_MEM_TYPE_USERSIG                6
#define XPRG_MEM_TYPE_FACTORY_CALIBRATION    7

// Erase types
#define XPRG_ERASE_CHIP                      1
#define XPRG_ERASE_APP                       2
#define XPRG_ERASE_BOOT                      3
#define XPRG_ERASE_EEPROM                    4
#define XPRG_ERASE_APP_PAGE                  5
#define XPRG_ERASE_BOOT_PAGE                 6
#define XPRG_ERASE_EEPROM_PAGE               7
#define XPRG_ERASE_USERSIG                   8
#define XPRG_ERASE_CONFIG                    9  // TPI only, prepare fuse write

// Write mode flags
#define XPRG_MEM_WRITE_ERASE                 0
#define XPRG_MEM_WRITE_WRITE                 1

// CRC types
#define XPRG_CRC_APP                         1
#define XPRG_CRC_BOOT                        2
#define XPRG_CRC_FLASH                       3

// Error codes
#define XPRG_ERR_OK                          0
#define XPRG_ERR_FAILED                      1
#define XPRG_ERR_COLLISION                   2
#define XPRG_ERR_TIMEOUT                     3

// XPROG parameters of different sizes
// 4-byte address
#define XPRG_PARAM_NVMBASE                  0x01
// 2-byte page size
#define XPRG_PARAM_EEPPAGESIZE              0x02
// 1-byte, undocumented TPI param
#define XPRG_PARAM_TPI_3                    0x03
// 1-byte, undocumented TPI param
#define XPRG_PARAM_TPI_4                    0x04

// *****************[ STK answer constants ]***************************

#define ANSWER_CKSUM_ERROR                  0xB0

/*
 * Private data for this programmer.
 */
struct pdata
{
  /*
   * See stk500pp_read_byte() for an explanation of the flash and
   * EEPROM page caches.
   */
  unsigned char *flash_pagecache;
  unsigned long flash_pageaddr;
  unsigned int flash_pagesize;

  unsigned char *eeprom_pagecache;
  unsigned long eeprom_pageaddr;
  unsigned int eeprom_pagesize;

  unsigned char command_sequence;

    enum
    {
        PGMTYPE_UNKNOWN,
        PGMTYPE_STK500,
        PGMTYPE_AVRISP,
        PGMTYPE_AVRISP_MKII,
        PGMTYPE_JTAGICE_MKII,
        PGMTYPE_STK600,
        PGMTYPE_JTAGICE3
    }
        pgmtype;

  AVRPART *lastpart;

  /* Start address of Xmega boot area */
  unsigned long boot_start;

  /*
   * Chained pdata for the JTAG ICE mkII backend.  This is used when
   * calling the backend functions for ISP/HVSP/PP programming
   * functionality of the JTAG ICE mkII and AVR Dragon.
   */
  void *chained_pdata;
};

