/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2003, 2004  Martin J. Thomas  <mthomas@rhrk.uni-kl.de>
 * Copyright (C) 2006  Joerg Wunsch <j@uriah.heep.sax.de>
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
 * Native Win32 serial interface for avrdude.
 */

#include "ac_cfg.h"

#if defined(WIN32NATIVE)

#ifdef HAVE_LIBWS2_32
/* winsock2.h must be included before windows.h from avrdude.h... */
#  include <winsock2.h>
#endif

#include <windows.h>
#include <stdio.h>
#include <ctype.h>   /* for isprint */
#include <errno.h>   /* ENOTTY */

#include "avrdude.h"
#include "libavrdude.h"

long serial_recv_timeout = 5000; /* ms */

#define W32SERBUFSIZE 1024

struct baud_mapping {
  long baud;
  DWORD speed;
};

static unsigned char serial_over_ethernet = 0;

/* HANDLE hComPort=INVALID_HANDLE_VALUE; */

static struct baud_mapping baud_lookup_table [] = {
  { 1200,   CBR_1200 },
  { 2400,   CBR_2400 },
  { 4800,   CBR_4800 },
  { 9600,   CBR_9600 },
  { 19200,  CBR_19200 },
  { 38400,  CBR_38400 },
  { 57600,  CBR_57600 },
  { 115200, CBR_115200 },
  { 0,      0 }                 /* Terminator. */
};

static DWORD serial_baud_lookup(long baud)
{
  struct baud_mapping *map = baud_lookup_table;

  while (map->baud) {
    if (map->baud == baud)
      return map->speed;
    map++;
  }

  /*
   * If a non-standard BAUD rate is used, issue
   * a warning (if we are verbose) and return the raw rate
   */
  avrdude_message(MSG_NOTICE, "%s: serial_baud_lookup(): Using non-standard baud rate: %ld",
              progname, baud);

  return baud;
}


static BOOL serial_w32SetTimeOut(HANDLE hComPort, DWORD timeout) // in ms
{
	COMMTIMEOUTS ctmo;
	ZeroMemory (&ctmo, sizeof(COMMTIMEOUTS));
	ctmo.ReadIntervalTimeout = timeout;
	ctmo.ReadTotalTimeoutMultiplier = timeout;
	ctmo.ReadTotalTimeoutConstant = timeout;

	return SetCommTimeouts(hComPort, &ctmo);
}

static int ser_setspeed(union filedescriptor *fd, long baud)
{
	if (serial_over_ethernet) {
		return -ENOTTY;
	} else {
		DCB dcb;
		HANDLE hComPort = (HANDLE)fd->pfd;

		ZeroMemory (&dcb, sizeof(DCB));
		dcb.DCBlength = sizeof(DCB);
		dcb.BaudRate = serial_baud_lookup (baud);
		dcb.fBinary = 1;
		dcb.fDtrControl = DTR_CONTROL_DISABLE;
		dcb.fRtsControl = RTS_CONTROL_DISABLE;
		dcb.ByteSize = 8;
		dcb.Parity = NOPARITY;
		dcb.StopBits = ONESTOPBIT;

		if (!SetCommState(hComPort, &dcb))
			return -1;

		return 0;
	}
}

#ifdef HAVE_LIBWS2_32
static int
net_open(const char *port, union filedescriptor *fdp)
{
	WSADATA wsaData;
	LPVOID lpMsgBuf;

	char *hstr, *pstr, *end;
	unsigned int pnum;
	int fd;
	struct sockaddr_in sockaddr;
	struct hostent *hp;

	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
		avrdude_message(MSG_INFO, "%s: net_open(): WSAStartup() failed\n", progname);
		return -1;
	}

	if ((hstr = strdup(port)) == NULL) {
		avrdude_message(MSG_INFO, "%s: net_open(): Out of memory!\n", progname);
		return -1;
	}

	if (((pstr = strchr(hstr, ':')) == NULL) || (pstr == hstr)) {
		avrdude_message(MSG_INFO, "%s: net_open(): Mangled host:port string \"%s\"\n", progname, hstr);
		free(hstr);
		return -1;
	}

	/*
	 * Terminate the host section of the description.
	 */
	*pstr++ = '\0';

	pnum = strtoul(pstr, &end, 10);

	if ((*pstr == '\0') || (*end != '\0') || (pnum == 0) || (pnum > 65535)) {
		avrdude_message(MSG_INFO, "%s: net_open(): Bad port number \"%s\"\n", progname, pstr);
		free(hstr);
		return -1;
	}

	if ((hp = gethostbyname(hstr)) == NULL) {
		avrdude_message(MSG_INFO, "%s: net_open(): unknown host \"%s\"\n", progname, hstr);
		free(hstr);
		return -1;
	}

	free(hstr);

	if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			WSAGetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&lpMsgBuf,
			0,
			NULL);
		avrdude_message(MSG_INFO, "%s: net_open(): Cannot open socket: %s\n", progname, (char *)lpMsgBuf);
		LocalFree(lpMsgBuf);
		return -1;
	}

	memset(&sockaddr, 0, sizeof(struct sockaddr_in));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(pnum);
	memcpy(&(sockaddr.sin_addr.s_addr), hp->h_addr, sizeof(struct in_addr));

	if (connect(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr))) {
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			WSAGetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&lpMsgBuf,
			0,
			NULL);
		avrdude_message(MSG_INFO, "%s: net_open(): Connect failed: %s\n", progname, (char *)lpMsgBuf);
		LocalFree(lpMsgBuf);
		return -1;
	}

	fdp->ifd = fd;

	serial_over_ethernet = 1;
	return 0;
}
#endif


static int ser_open(char * port, union pinfo pinfo, union filedescriptor *fdp)
{
	LPVOID lpMsgBuf;
	HANDLE hComPort=INVALID_HANDLE_VALUE;
	char *newname = 0;

	/*
	 * If the port is of the form "net:<host>:<port>", then
	 * handle it as a TCP connection to a terminal server.
	 */
	if (strncmp(port, "net:", strlen("net:")) == 0) {
#ifdef HAVE_LIBWS2_32
		return net_open(port + strlen("net:"), fdp);
#else
		avrdude_message(MSG_INFO, "%s: ser_open(): "
				"not configured for TCP connections\n",
                                progname);
		return -1;
#endif
	}

	if (strncasecmp(port, "com", strlen("com")) == 0) {

	    // prepend "\\\\.\\" to name, required for port # >= 10
	    newname = malloc(strlen("\\\\.\\") + strlen(port) + 1);

	    if (newname == 0) {
		avrdude_message(MSG_INFO, "%s: ser_open(): out of memory\n",
                                progname);
		exit(1);
	    }
	    strcpy(newname, "\\\\.\\");
	    strcat(newname, port);

	    port = newname;
	}

	hComPort = CreateFile(port, GENERIC_READ | GENERIC_WRITE, 0, NULL,
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
		LocalFree( lpMsgBuf );
		return -1;
	}

	if (!SetupComm(hComPort, W32SERBUFSIZE, W32SERBUFSIZE))
	{
		CloseHandle(hComPort);
		avrdude_message(MSG_INFO, "%s: ser_open(): can't set buffers for \"%s\"\n",
				progname, port);
		return -1;
	}

        fdp->pfd = (void *)hComPort;
	if (ser_setspeed(fdp, pinfo.baud) != 0)
	{
		CloseHandle(hComPort);
		avrdude_message(MSG_INFO, "%s: ser_open(): can't set com-state for \"%s\"\n",
				progname, port);
		return -1;
	}

	if (!serial_w32SetTimeOut(hComPort,0))
	{
		CloseHandle(hComPort);
		avrdude_message(MSG_INFO, "%s: ser_open(): can't set initial timeout for \"%s\"\n",
				progname, port);
		return -1;
	}

	if (newname != 0) {
	    free(newname);
	}
	return 0;
}


static void ser_close(union filedescriptor *fd)
{
	if (serial_over_ethernet) {
		closesocket(fd->ifd);
		WSACleanup();
	} else {
		HANDLE hComPort=(HANDLE)fd->pfd;
		if (hComPort != INVALID_HANDLE_VALUE)
			CloseHandle (hComPort);

		hComPort = INVALID_HANDLE_VALUE;
	}
}

static int ser_set_dtr_rts(union filedescriptor *fd, int is_on)
{
	if (serial_over_ethernet) {
		return 0;
	} else {
		HANDLE hComPort=(HANDLE)fd->pfd;

		if (is_on) {
			EscapeCommFunction(hComPort, SETDTR);
			EscapeCommFunction(hComPort, SETRTS);
		} else {
			EscapeCommFunction(hComPort, CLRDTR);
			EscapeCommFunction(hComPort, CLRRTS);
		}
		return 0;
	}
}

#ifdef HAVE_LIBWS2_32
static int net_send(union filedescriptor *fd, const unsigned char * buf, size_t buflen)
{
	LPVOID lpMsgBuf;
	int rc;
	const unsigned char *p = buf;
	size_t len = buflen;

	if (fd->ifd < 0) {
		avrdude_message(MSG_NOTICE, "%s: net_send(): connection not open\n", progname);
		exit(1);
	}

	if (!len) {
		return 0;
	}

	if (verbose > 3) {
		avrdude_message(MSG_TRACE, "%s: Send: ", progname);

		while (buflen) {
			unsigned char c = *buf;
			if (isprint(c)) {
				avrdude_message(MSG_TRACE, "%c ", c);
			} else {
				avrdude_message(MSG_TRACE, ". ");
			}
			avrdude_message(MSG_TRACE, "[%02x] ", c);

			buf++;
			buflen--;
		}

		avrdude_message(MSG_TRACE, "\n");
	}

	while (len) {
		rc = send(fd->ifd, p, (len > 1024) ? 1024 : len, 0);
		if (rc < 0) {
			FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				WSAGetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR)&lpMsgBuf,
				0,
				NULL);
			avrdude_message(MSG_INFO, "%s: net_send(): send error: %s\n", progname, (char *)lpMsgBuf);
			LocalFree(lpMsgBuf);
			exit(1);
		}
		p += rc;
		len -= rc;
	}

	return 0;
}
#endif


static int ser_send(union filedescriptor *fd, const unsigned char * buf, size_t buflen)
{
#ifdef HAVE_LIBWS2_32
	if (serial_over_ethernet) {
		return net_send(fd, buf, buflen);
	}
#endif

	size_t len = buflen;
	unsigned char c='\0';
	DWORD written;
        const unsigned char * b = buf;

	HANDLE hComPort=(HANDLE)fd->pfd;

	if (hComPort == INVALID_HANDLE_VALUE) {
		avrdude_message(MSG_INFO, "%s: ser_send(): port not open\n",
              progname); 
		return -1;
	}

	if (!len)
  return 0;

	if (verbose > 3)
	{
		avrdude_message(MSG_TRACE, "%s: Send: ", progname);

		while (len) {
			c = *b;
			if (isprint(c)) {
				avrdude_message(MSG_TRACE, "%c ", c);
			}
			else {
				avrdude_message(MSG_TRACE, ". ");
			}
			avrdude_message(MSG_TRACE, "[%02x] ", c);
			b++;
			len--;
		}
      avrdude_message(MSG_INFO, "\n");
	}
	
	serial_w32SetTimeOut(hComPort,500);

	if (!WriteFile (hComPort, buf, buflen, &written, NULL)) {
		avrdude_message(MSG_INFO, "%s: ser_send(): write error: %s\n",
              progname, "sorry no info avail"); // TODO
		return -1;
	}

	if (written != buflen) {
		avrdude_message(MSG_INFO, "%s: ser_send(): size/send mismatch\n",
              progname); 
		return -1;
	}

	return 0;
}


#ifdef HAVE_LIBWS2_32
static int net_recv(union filedescriptor *fd, unsigned char * buf, size_t buflen)
{
	LPVOID lpMsgBuf;
	struct timeval timeout, to2;
	fd_set rfds;
	int nfds;
	int rc;
	unsigned char *p = buf;
	size_t len = 0;

	if (fd->ifd < 0) {
		avrdude_message(MSG_INFO, "%s: net_recv(): connection not open\n", progname);
		exit(1);
	}

	timeout.tv_sec  = serial_recv_timeout / 1000L;
	timeout.tv_usec = (serial_recv_timeout % 1000L) * 1000;
	to2 = timeout;

	while (len < buflen) {
reselect:
		FD_ZERO(&rfds);
		FD_SET(fd->ifd, &rfds);

		nfds = select(fd->ifd + 1, &rfds, NULL, NULL, &to2);
		if (nfds == 0) {
			if (verbose > 1) {
				avrdude_message(MSG_NOTICE, "%s: ser_recv(): programmer is not responding\n", progname);
			}
			return -1;
		} else if (nfds == -1) {
			if (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEINPROGRESS) {
				avrdude_message(MSG_NOTICE, "%s: ser_recv(): programmer is not responding, reselecting\n", progname);
				goto reselect;
			} else {
				FormatMessage(
					FORMAT_MESSAGE_ALLOCATE_BUFFER |
					FORMAT_MESSAGE_FROM_SYSTEM |
					FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL,
					WSAGetLastError(),
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					(LPTSTR)&lpMsgBuf,
					0,
					NULL);
				avrdude_message(MSG_INFO, "%s: ser_recv(): select(): %s\n", progname, (char *)lpMsgBuf);
				LocalFree(lpMsgBuf);
				exit(1);
			}
		}

		rc = recv(fd->ifd, p, (buflen - len > 1024) ? 1024 : buflen - len, 0);
		if (rc < 0) {
			FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				WSAGetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR)&lpMsgBuf,
				0,
				NULL);
			avrdude_message(MSG_INFO, "%s: ser_recv(): read error: %s\n", progname, (char *)lpMsgBuf);
			LocalFree(lpMsgBuf);
			exit(1);
		}
		p += rc;
		len += rc;
	}

	p = buf;

	if (verbose > 3) {
		avrdude_message(MSG_TRACE, "%s: Recv: ", progname);

		while (len) {
			unsigned char c = *p;
			if (isprint(c)) {
				avrdude_message(MSG_TRACE, "%c ", c);
			} else {
				avrdude_message(MSG_TRACE, ". ");
			}
			avrdude_message(MSG_TRACE, "[%02x] ", c);

			p++;
			len--;
		}
		avrdude_message(MSG_TRACE, "\n");
	}

	return 0;
}
#endif

static int ser_recv(union filedescriptor *fd, unsigned char * buf, size_t buflen)
{
#ifdef HAVE_LIBWS2_32
	if (serial_over_ethernet) {
		return net_recv(fd, buf, buflen);
	}
#endif

	unsigned char c;
	unsigned char * p = buf;
	DWORD read;

	HANDLE hComPort=(HANDLE)fd->pfd;
	
	if (hComPort == INVALID_HANDLE_VALUE) {
		avrdude_message(MSG_INFO, "%s: ser_read(): port not open\n",
              progname); 
		return -1;
	}
	
	serial_w32SetTimeOut(hComPort, serial_recv_timeout);
	
	if (!ReadFile(hComPort, buf, buflen, &read, NULL)) {
		LPVOID lpMsgBuf;
		FormatMessage( 
			FORMAT_MESSAGE_ALLOCATE_BUFFER | 
			FORMAT_MESSAGE_FROM_SYSTEM | 
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPTSTR) &lpMsgBuf,
			0,
			NULL 	);
		avrdude_message(MSG_INFO, "%s: ser_recv(): read error: %s\n",
			      progname, (char*)lpMsgBuf);
		LocalFree( lpMsgBuf );
		return -1;
	}

	/* time out detected */
	if (read == 0) {
		avrdude_message(MSG_NOTICE2, "%s: ser_recv(): programmer is not responding\n",
                                progname);
		return -1;
	}

	p = buf;

	if (verbose > 3)
	{
		avrdude_message(MSG_TRACE, "%s: Recv: ", progname);

		while (read) {
			c = *p;
			if (isprint(c)) {
				avrdude_message(MSG_TRACE, "%c ", c);
			}
			else {
				avrdude_message(MSG_TRACE, ". ");
			}
			avrdude_message(MSG_TRACE, "[%02x] ", c);

			p++;
			read--;
		}
		avrdude_message(MSG_INFO, "\n");
	}
  return 0;
}


static int ser_drain(union filedescriptor *fd, int display)
{
	// int rc;
	unsigned char buf[10];
	BOOL readres;
	DWORD read;

	HANDLE hComPort=(HANDLE)fd->pfd;

  	if (hComPort == INVALID_HANDLE_VALUE) {
		avrdude_message(MSG_INFO, "%s: ser_drain(): port not open\n",
              progname); 
		return -1;
	}

	serial_w32SetTimeOut(hComPort,250);
  
	if (display) {
		avrdude_message(MSG_INFO, "drain>");
	}

	while (1) {
		readres=ReadFile(hComPort, buf, 1, &read, NULL);
		if (!readres) {
			LPVOID lpMsgBuf;
			FormatMessage( 
				FORMAT_MESSAGE_ALLOCATE_BUFFER | 
				FORMAT_MESSAGE_FROM_SYSTEM | 
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
				(LPTSTR) &lpMsgBuf,
				0,
				NULL 	);
			avrdude_message(MSG_INFO, "%s: ser_drain(): read error: %s\n",
					  progname, (char*)lpMsgBuf);
			LocalFree( lpMsgBuf );
			return -1;
		}

		if (read) { // data avail
			if (display) avrdude_message(MSG_INFO, "%02x ", buf[0]);
		}
		else { // no more data
			if (display) avrdude_message(MSG_INFO, "<drain\n");
			break;
		}
	} // while
  return 0;
}

struct serial_device serial_serdev =
{
  .open = ser_open,
  .setspeed = ser_setspeed,
  .close = ser_close,
  .send = ser_send,
  .recv = ser_recv,
  .drain = ser_drain,
  .set_dtr_rts = ser_set_dtr_rts,
  .flags = SERDEV_FL_CANSETSPEED,
};

struct serial_device *serdev = &serial_serdev;

#endif /* WIN32NATIVE */
