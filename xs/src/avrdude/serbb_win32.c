/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2003, 2004  Martin J. Thomas  <mthomas@rhrk.uni-kl.de>
 * Copyright (C) 2005 Michael Holzt <kju-avr@fqdn.org>
 * Copyright (C) 2005, 2006 Joerg Wunsch <j@uriah.heep.sax.de>
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
 * Win32 serial bitbanging interface for avrdude.
 */

#include "avrdude.h"

#if defined(WIN32NATIVE)


#include "ac_cfg.h"

#include <windows.h>
#include <stdio.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "bitbang.h"
#include "serbb.h"

/* cached status lines */
static int dtr, rts, txd;

#define W32SERBUFSIZE 1024

/*
  serial port/pin mapping

  1	cd	<-
  2	(rxd)	<-
  3	txd	->
  4	dtr	->
  5	GND
  6	dsr	<-
  7	rts	->
  8	cts	<-
  9	ri	<-
*/

#define DB9PINS 9

static int serbb_setpin(PROGRAMMER * pgm, int pinfunc, int value)
{
	int pin = pgm->pinno[pinfunc];
	HANDLE hComPort = (HANDLE)pgm->fd.pfd;
        LPVOID lpMsgBuf;
        DWORD dwFunc;
        const char *name;

        if (pin & PIN_INVERSE)
        {
                value = !value;
                pin &= PIN_MASK;
        }

        if (pin < 1 || pin > DB9PINS)
                return -1;

        switch (pin)
        {
        case 3:  /* txd */
                dwFunc = value? SETBREAK: CLRBREAK;
                name = value? "SETBREAK": "CLRBREAK";
                txd = value;
                break;

        case 4:  /* dtr */
                dwFunc = value? SETDTR: CLRDTR;
                name = value? "SETDTR": "CLRDTR";
                dtr = value;
                break;

        case 7:  /* rts */
                dwFunc = value? SETRTS: CLRRTS;
                name = value? "SETRTS": "CLRRTS";
                break;

        default:
                avrdude_message(MSG_NOTICE, "%s: serbb_setpin(): unknown pin %d\n",
                                        progname, pin + 1);
                return -1;
        }
        avrdude_message(MSG_TRACE2, "%s: serbb_setpin(): EscapeCommFunction(%s)\n",
                                progname, name);
        if (!EscapeCommFunction(hComPort, dwFunc))
        {
                FormatMessage(
                        FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL,
                        GetLastError(),
                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
                        (LPTSTR) &lpMsgBuf,
                        0,
                        NULL);
                avrdude_message(MSG_INFO, "%s: serbb_setpin(): SetCommState() failed: %s\n",
                                progname, (char *)lpMsgBuf);
                CloseHandle(hComPort);
                LocalFree(lpMsgBuf);
                return -1;
        }

	if (pgm->ispdelay > 1)
	  bitbang_delay(pgm->ispdelay);

        return 0;
}

static int serbb_getpin(PROGRAMMER * pgm, int pinfunc)
{
	int pin = pgm->pinno[pinfunc];
	HANDLE hComPort = (HANDLE)pgm->fd.pfd;
        LPVOID lpMsgBuf;
        int invert, rv;
        const char *name;
        DWORD modemstate;

        if (pin & PIN_INVERSE)
        {
                invert = 1;
                pin &= PIN_MASK;
        } else
                invert = 0;

        if (pin < 1 || pin > DB9PINS)
                return -1;

        if (pin == 1 /* cd */ || pin == 6 /* dsr */ || pin == 8 /* cts */)
        {
                if (!GetCommModemStatus(hComPort, &modemstate))
                {
                        FormatMessage(
                                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL,
                                GetLastError(),
                                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
                                (LPTSTR) &lpMsgBuf,
                                0,
                                NULL);
                        avrdude_message(MSG_INFO, "%s: serbb_setpin(): GetCommModemStatus() failed: %s\n",
                                        progname, (char *)lpMsgBuf);
                        CloseHandle(hComPort);
                        LocalFree(lpMsgBuf);
                        return -1;
                }
                avrdude_message(MSG_TRACE2, "%s: serbb_getpin(): GetCommState() => 0x%lx\n",
                                        progname, modemstate);
                switch (pin)
                {
                case 1:
                        modemstate &= MS_RLSD_ON;
                        break;
                case 6:
                        modemstate &= MS_DSR_ON;
                        break;
                case 8:
                        modemstate &= MS_CTS_ON;
                        break;
                }
                rv = modemstate != 0;
                if (invert)
                        rv = !rv;

                return rv;
        }

        switch (pin)
        {
        case 3: /* txd */
                rv = txd;
                name = "TXD";
                break;
        case 4: /* dtr */
                rv = dtr;
                name = "DTR";
                break;
        case 7: /* rts */
                rv = rts;
                name = "RTS";
                break;
        default:
                avrdude_message(MSG_NOTICE, "%s: serbb_getpin(): unknown pin %d\n",
                                        progname, pin + 1);
                return -1;
        }
        avrdude_message(MSG_TRACE2, "%s: serbb_getpin(): return cached state for %s\n",
                                progname, name);
        if (invert)
                rv = !rv;

        return rv;
}

static int serbb_highpulsepin(PROGRAMMER * pgm, int pinfunc)
{
	    int pin = pgm->pinno[pinfunc];
        if ( (pin & PIN_MASK) < 1 || (pin & PIN_MASK) > DB9PINS )
          return -1;

        serbb_setpin(pgm, pinfunc, 1);
        serbb_setpin(pgm, pinfunc, 0);

        return 0;
}


static void serbb_display(PROGRAMMER *pgm, const char *p)
{
  /* MAYBE */
}

static void serbb_enable(PROGRAMMER *pgm)
{
  /* nothing */
}

static void serbb_disable(PROGRAMMER *pgm)
{
  /* nothing */
}

static void serbb_powerup(PROGRAMMER *pgm)
{
  /* nothing */
}

static void serbb_powerdown(PROGRAMMER *pgm)
{
  /* nothing */
}

static int serbb_open(PROGRAMMER *pgm, char *port)
{
        DCB dcb;
	LPVOID lpMsgBuf;
	HANDLE hComPort = INVALID_HANDLE_VALUE;

	if (bitbang_check_prerequisites(pgm) < 0)
	    return -1;

	hComPort = CreateFileA(port, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hComPort == INVALID_HANDLE_VALUE) {
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPTSTR) &lpMsgBuf,
			0,
			NULL);
		avrdude_message(MSG_INFO, "%s: ser_open(): can't open device \"%s\": %s\n",
                        progname, port, (char*)lpMsgBuf);
		LocalFree(lpMsgBuf);
                return -1;
	}

	if (!SetupComm(hComPort, W32SERBUFSIZE, W32SERBUFSIZE))
	{
		CloseHandle(hComPort);
		avrdude_message(MSG_INFO, "%s: ser_open(): can't set buffers for \"%s\"\n",
                        progname, port);
                return -1;
	}


	ZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);
	dcb.BaudRate = CBR_9600;
	dcb.fBinary = 1;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;

	if (!SetCommState(hComPort, &dcb))
	{
		CloseHandle(hComPort);
		avrdude_message(MSG_INFO, "%s: ser_open(): can't set com-state for \"%s\"\n",
                        progname, port);
                return -1;
	}
        avrdude_message(MSG_DEBUG, "%s: ser_open(): opened comm port \"%s\", handle 0x%x\n",
                        progname, port, (int)hComPort);

        pgm->fd.pfd = (void *)hComPort;

        dtr = rts = txd = 0;

        return 0;
}

static void serbb_close(PROGRAMMER *pgm)
{
	HANDLE hComPort=(HANDLE)pgm->fd.pfd;
	if (hComPort != INVALID_HANDLE_VALUE)
	{
		pgm->setpin(pgm, PIN_AVR_RESET, 1);
		CloseHandle (hComPort);
	}
        avrdude_message(MSG_DEBUG, "%s: ser_close(): closed comm port handle 0x%x\n",
                                progname, (int)hComPort);

	hComPort = INVALID_HANDLE_VALUE;
}

const char serbb_desc[] = "Serial port bitbanging";

void serbb_initpgm(PROGRAMMER *pgm)
{
  strcpy(pgm->type, "SERBB");

  pgm_fill_old_pins(pgm); // TODO to be removed if old pin data no longer needed

  pgm->rdy_led        = bitbang_rdy_led;
  pgm->err_led        = bitbang_err_led;
  pgm->pgm_led        = bitbang_pgm_led;
  pgm->vfy_led        = bitbang_vfy_led;
  pgm->initialize     = bitbang_initialize;
  pgm->display        = serbb_display;
  pgm->enable         = serbb_enable;
  pgm->disable        = serbb_disable;
  pgm->powerup        = serbb_powerup;
  pgm->powerdown      = serbb_powerdown;
  pgm->program_enable = bitbang_program_enable;
  pgm->chip_erase     = bitbang_chip_erase;
  pgm->cmd            = bitbang_cmd;
  pgm->cmd_tpi        = bitbang_cmd_tpi;
  pgm->open           = serbb_open;
  pgm->close          = serbb_close;
  pgm->setpin         = serbb_setpin;
  pgm->getpin         = serbb_getpin;
  pgm->highpulsepin   = serbb_highpulsepin;
  pgm->read_byte      = avr_read_byte_default;
  pgm->write_byte     = avr_write_byte_default;
}

#endif  /* WIN32NATIVE */
