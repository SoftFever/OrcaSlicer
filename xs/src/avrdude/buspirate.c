/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 *
 * avrdude support for The Bus Pirate - universal serial interface
 *
 * Copyright (C) 2009 Michal Ludvig <mludvig@logix.net.nz>
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

/*
 * BusPirate       AVR Chip
 * ---------       --------
 *       GND  <->  GND
 *       +5V  <->  Vcc
 *        CS  <->  RESET
 *      MOSI  <->  MOSI
 *      MISO  <->  MISO
 *   SCL/CLK  <->  SCK
 *     ( AUX  <->  XTAL1 )
 *
 * Tested with BusPirate PTH, firmware version 2.1 programming ATmega328P
 */

/* $Id$ */

#include "ac_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "avrdude.h"
#include "libavrdude.h"

#include "bitbang.h"
#include "buspirate.h"

/* ====== Private data structure ====== */
/* CS and AUX pin bitmasks in
 * 0100wxyz - Configure peripherals command */
#define BP_RESET_CS     0x01
#define BP_RESET_AUX    0x02
#define BP_RESET_AUX2   0x04

#define BP_FLAG_IN_BINMODE          (1<<0)
#define BP_FLAG_XPARM_FORCE_ASCII   (1<<1)
#define BP_FLAG_XPARM_RESET         (1<<2)
#define BP_FLAG_XPARM_SPIFREQ       (1<<3)
#define BP_FLAG_NOPAGEDWRITE        (1<<4)
#define BP_FLAG_XPARM_CPUFREQ       (1<<5)
#define BP_FLAG_XPARM_RAWFREQ       (1<<6)
#define BP_FLAG_NOPAGEDREAD         (1<<7)

struct pdata
{
	int	binmode_version;
	int	submode_version;
	int	current_peripherals_config;
	int	spifreq;		/* For "set speed" commands */
	int	cpufreq;		/* (125)..4000 kHz - see buspirate manual */
	int	serial_recv_timeout;    /* timeout in ms, default 100 */
	int	reset;			/* See BP_RESET_* above */
	unsigned char pin_dir;		/* Last written pin direction for bitbang mode */
	unsigned char pin_val;		/* Last written pin values for bitbang mode */
	int     unread_bytes;		/* How many bytes we expected, but ignored */
};
#define PDATA(pgm) ((struct pdata *)(pgm->cookie))

/* ====== Feature checks ====== */
static inline int
buspirate_uses_ascii(struct programmer_t *pgm)
{
	return (pgm->flag & BP_FLAG_XPARM_FORCE_ASCII);
}

/* ====== Serial talker functions - binmode ====== */

static void dump_mem(const int msglvl, const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i<len; i++) {
		if (i % 8 == 0)
			avrdude_message(msglvl, "\t");
		avrdude_message(msglvl, "0x%02x ", buf[i]);
		if (i % 8 == 3)
			avrdude_message(msglvl, "  ");
		else if (i % 8 == 7)
			avrdude_message(msglvl, "\n");
	}
	if (i % 8 != 7)
		avrdude_message(msglvl, "\n");
}

static int buspirate_send_bin(struct programmer_t *pgm, const unsigned char *data, size_t len)
{
	int rc;

	avrdude_message(MSG_DEBUG, "%s: buspirate_send_bin():\n", progname);
	dump_mem(MSG_DEBUG, data, len);

	rc = serial_send(&pgm->fd, data, len);

	return rc;
}

static int buspirate_recv_bin(struct programmer_t *pgm, unsigned char *buf, size_t len)
{
	int rc;

	rc = serial_recv(&pgm->fd, buf, len);
	if (rc < 0)
		return EOF;

	avrdude_message(MSG_DEBUG, "%s: buspirate_recv_bin():\n", progname);
	dump_mem(MSG_DEBUG, buf, len);

	return len;
}

static int buspirate_expect_bin(struct programmer_t *pgm,
				unsigned char *send_data, size_t send_len,
				unsigned char *expect_data, size_t expect_len)
{
	unsigned char *recv_buf = alloca(expect_len);
	if ((pgm->flag & BP_FLAG_IN_BINMODE) == 0) {
		avrdude_message(MSG_INFO, "BusPirate: Internal error: buspirate_send_bin() called from ascii mode\n");
		return -1;
	}

	buspirate_send_bin(pgm, send_data, send_len);
	buspirate_recv_bin(pgm, recv_buf, expect_len);
	if (memcmp(expect_data, recv_buf, expect_len) != 0)
		return 0;
	return 1;
}

static int buspirate_expect_bin_byte(struct programmer_t *pgm,
					unsigned char send_byte, unsigned char expect_byte)
{
	return buspirate_expect_bin(pgm, &send_byte, 1, &expect_byte, 1);
}

/* ====== Serial talker functions - ascii mode ====== */

static int buspirate_getc(struct programmer_t *pgm)
{
	int rc;
	unsigned char ch = 0;

	if (pgm->flag & BP_FLAG_IN_BINMODE) {
		avrdude_message(MSG_INFO, "BusPirate: Internal error: buspirate_getc() called from binmode\n");
		return EOF;
	}

	rc = serial_recv(&pgm->fd, &ch, 1);
	if (rc < 0)
		return EOF;
	return ch;
}

static char *buspirate_readline_noexit(struct programmer_t *pgm, char *buf, size_t len)
{
	char *buf_p;
	long orig_serial_recv_timeout = serial_recv_timeout;

	/* Static local buffer - this may come handy at times */
	static char buf_local[100];

	if (buf == NULL) {
		buf = buf_local;
		len = sizeof(buf_local);
	}
	buf_p = buf;
	memset(buf, 0, len);
	while (buf_p < (buf + len - 1)) { /* keep the very last byte == 0 */
		*buf_p = buspirate_getc(pgm);
		if (*buf_p == '\r')
			continue;
		if (*buf_p == '\n')
			break;
		if (*buf_p == EOF) {
			*buf_p = '\0';
			break;
		}
		buf_p++;
		serial_recv_timeout = PDATA(pgm)->serial_recv_timeout;
	}
	serial_recv_timeout = orig_serial_recv_timeout;
	avrdude_message(MSG_DEBUG, "%s: buspirate_readline(): %s%s",
	                progname, buf,
	                buf[strlen(buf) - 1] == '\n' ? "" : "\n");
	if (! buf[0])
		return NULL;

	return buf;
}

static char *buspirate_readline(struct programmer_t *pgm, char *buf, size_t len)
{
	char *ret;

	ret = buspirate_readline_noexit(pgm, buf, len);
	if (! ret) {
		avrdude_message(MSG_INFO, "%s: buspirate_readline(): programmer is not responding\n",
                                progname);
		return NULL;
	}
	return ret;
}
static int buspirate_send(struct programmer_t *pgm, const char *str)
{
	int rc;
	const char * readline;

	avrdude_message(MSG_DEBUG, "%s: buspirate_send(): %s", progname, str);

	if (pgm->flag & BP_FLAG_IN_BINMODE) {
		avrdude_message(MSG_INFO, "BusPirate: Internal error: buspirate_send() called from binmode\n");
		return -1;
	}

	rc = serial_send(&pgm->fd, (const unsigned char*)str, strlen(str));
	if (rc)
		return rc;
	do {
		readline = buspirate_readline(pgm, NULL, 0);
		if (readline == NULL)
			return -1;
	/* keep reading until we get what we sent there */
	} while (strcmp(readline, str) != 0);

	/* by now we should be in sync */
	return 0;
}

static int buspirate_is_prompt(const char *str)
{
	int strlen_str = strlen(str);
	/* Prompt ends with '>' or '> '
	 * all other input probably ends with '\n' */
	return (str[strlen_str - 1] == '>' || str[strlen_str - 2] == '>');
}

static int buspirate_expect(struct programmer_t *pgm, char *send,
				char *expect, int wait_for_prompt)
{
	int got_it = 0;
	size_t expect_len = strlen(expect);
	char *rcvd;

	buspirate_send(pgm, send);
	while (1) {
		rcvd = buspirate_readline(pgm, NULL, 0);
		if (rcvd == NULL) {
			return -1;
		}
		if (strncmp(rcvd, expect, expect_len) == 0) {
			if (! wait_for_prompt) {
				serial_drain(&pgm->fd, 0);
				return 1;
			} else {
				got_it = 1;
			}
		}

		if (buspirate_is_prompt(rcvd))
			break;
	}
	return got_it;
}

/* ====== Do-nothing functions ====== */
static void buspirate_dummy_6(struct programmer_t *pgm, const char *p)
{
}

/* ====== Config / parameters handling functions ====== */
static int
buspirate_parseextparms(struct programmer_t *pgm, LISTID extparms)
{
	LNODEID ln;
	const char *extended_param;
	char reset[10];
	char *preset = reset; /* for strtok() */
	unsigned int spifreq;
	unsigned int rawfreq;
	unsigned int cpufreq;
	int serial_recv_timeout;

	for (ln = lfirst(extparms); ln; ln = lnext(ln)) {
		extended_param = ldata(ln);
		if (strcmp(extended_param, "ascii") == 0) {
			pgm->flag |= BP_FLAG_XPARM_FORCE_ASCII;
			continue;
		}

		if (sscanf(extended_param, "spifreq=%u", &spifreq) == 1) {
			if (spifreq & (~0x07)) {
				avrdude_message(MSG_INFO, "BusPirate: spifreq must be between 0 and 7.\n");
				avrdude_message(MSG_INFO, "BusPirate: see BusPirate manual for details.\n");
				return -1;
			}
			if (pgm->flag & BP_FLAG_XPARM_RAWFREQ) {
				avrdude_message(MSG_INFO, "BusPirate: set either spifreq or rawfreq\n");
				return -1;
			}
			pgm->flag |=  BP_FLAG_XPARM_SPIFREQ;
			PDATA(pgm)->spifreq = spifreq;
			continue;
		}

		if (sscanf(extended_param, "rawfreq=%u", &rawfreq) == 1) {
			if (rawfreq >= 4) {
				avrdude_message(MSG_INFO, "BusPirate: rawfreq must be "
				                "between 0 and 3.\n");
				return -1;
			}
			if (pgm->flag & BP_FLAG_XPARM_SPIFREQ) {
				avrdude_message(MSG_INFO, "BusPirate: set either spifreq or rawfreq\n");
				return -1;
			}
			pgm->flag |=  BP_FLAG_XPARM_RAWFREQ;
			PDATA(pgm)->spifreq = rawfreq;
			continue;
		}

		if (sscanf(extended_param, "cpufreq=%u", &cpufreq) == 1) {
			/* lower limit comes from 'cpufreq > 4 * spifreq', spifreq in ascii mode is 30kHz. */
			if (cpufreq < 125 || cpufreq > 4000) {
				avrdude_message(MSG_INFO, "BusPirate: cpufreq must be between 125 and 4000 kHz.\n");
				avrdude_message(MSG_INFO, "BusPirate: see BusPirate manual for details.\n");
				return -1;
			}
			PDATA(pgm)->cpufreq = cpufreq;
			pgm->flag |= BP_FLAG_XPARM_CPUFREQ;
			continue;
		}

		if (sscanf(extended_param, "reset=%9s", reset) == 1) {
			char *resetpin;
			while ((resetpin = strtok(preset, ","))) {
				preset = NULL; /* for subsequent strtok() calls */
				if (strcasecmp(resetpin, "cs") == 0)
					PDATA(pgm)->reset |= BP_RESET_CS;
				else if (strcasecmp(resetpin, "aux") == 0 || strcasecmp(reset, "aux1") == 0)
					PDATA(pgm)->reset |= BP_RESET_AUX;
				else if (strcasecmp(resetpin, "aux2") == 0)
					PDATA(pgm)->reset |= BP_RESET_AUX2;
				else {
					avrdude_message(MSG_INFO, "BusPirate: reset must be either CS or AUX.\n");
					return -1;
				}
			}
			pgm->flag |= BP_FLAG_XPARM_RESET;
			continue;
		}

		if (strcmp(extended_param, "nopagedwrite") == 0) {
			pgm->flag |= BP_FLAG_NOPAGEDWRITE;
			continue;
		}

		if (strcmp(extended_param, "nopagedread") == 0) {
			pgm->flag |= BP_FLAG_NOPAGEDREAD;
			continue;
		}

		if (sscanf(extended_param, "serial_recv_timeout=%d", &serial_recv_timeout) == 1) {
			if (serial_recv_timeout < 1) {
				avrdude_message(MSG_INFO, "BusPirate: serial_recv_timeout must be greater 0.\n");
				return -1;
			}
			PDATA(pgm)->serial_recv_timeout = serial_recv_timeout;
			continue;
		}

		avrdude_message(MSG_INFO, "BusPirate: do not understand extended param '%s'.\n", extended_param);
		return -1;
	}

	return 0;
}

static int
buspirate_verifyconfig(struct programmer_t *pgm)
{
	/* Default reset pin is CS */
	if (PDATA(pgm)->reset == 0x00)
		PDATA(pgm)->reset |= BP_RESET_CS;

	if ((PDATA(pgm)->reset != BP_RESET_CS) && buspirate_uses_ascii(pgm)) {
		avrdude_message(MSG_INFO, "BusPirate: RESET pin other than CS is not supported in ASCII mode\n");
		return -1;
	}

	if (  ((pgm->flag & BP_FLAG_XPARM_SPIFREQ) || (pgm->flag & BP_FLAG_XPARM_RAWFREQ))
	   && buspirate_uses_ascii(pgm)) {
		avrdude_message(MSG_INFO, "BusPirate: SPI speed selection is not supported in ASCII mode\n");
		return -1;
	}

	return 0;
}

/* ====== Programmer methods ======= */
static int buspirate_open(struct programmer_t *pgm, char * port)
{
	union pinfo pinfo;
	/* BusPirate runs at 115200 by default */
	if(pgm->baudrate == 0)
		pgm->baudrate = 115200;

	pinfo.baud = pgm->baudrate;
	strcpy(pgm->port, port);
	if (serial_open(port, pinfo, &pgm->fd)==-1) {
		return -1;
	}

	/* drain any extraneous input */
	serial_drain(&pgm->fd, 0);

	return 0;
}

static void buspirate_close(struct programmer_t *pgm)
{
	serial_close(&pgm->fd);
	pgm->fd.ifd = -1;
}

static void buspirate_reset_from_binmode(struct programmer_t *pgm)
{
	unsigned char buf[10];

	buf[0] = 0x00;	/* BinMode: revert to raw bitbang mode */
	buspirate_send_bin(pgm, buf, 1);
	buspirate_recv_bin(pgm, buf, 5);

	if (pgm->flag & BP_FLAG_XPARM_CPUFREQ) {
		/* disable pwm */
		if (buspirate_expect_bin_byte(pgm, 0x13, 0x01) != 1) {
			avrdude_message(MSG_INFO, "%s: warning: did not get a response to stop PWM command.\n", progname);
		}
	}
	/* 0b0100wxyz - Configure peripherals w=power, x=pull-ups, y=AUX, z=CS
	 * we want everything off -- 0b01000000 = 0x40 */
	if (buspirate_expect_bin_byte(pgm, 0x40, 0x00) == 1) {
		avrdude_message(MSG_INFO, "%s: warning: did not get a response to power off command.\n", progname);
	}

	buf[0] = 0x0F;	/* BinMode: reset */
	buspirate_send_bin(pgm, buf, 1);

	/* read back all output */
	memset(buf, '\0', sizeof(buf));
	for (;;) {
		int rc;
		rc = buspirate_recv_bin(pgm, buf, sizeof(buf) - 1);

		if (buspirate_is_prompt((const char*)buf)) {
			pgm->flag &= ~BP_FLAG_IN_BINMODE;
			break;
		}
		if (rc == EOF)
			break;
		memset(buf, '\0', sizeof(buf));
	}

	if (pgm->flag & BP_FLAG_IN_BINMODE) {
		avrdude_message(MSG_INFO, "BusPirate reset failed. You may need to powercycle it.\n");
		return;
	}

	avrdude_message(MSG_NOTICE, "BusPirate is back in the text mode\n");
}

static int buspirate_start_mode_bin(struct programmer_t *pgm)
{
	const struct submode {
		const char *name;  /* Name of mode for user messages */
		char enter;  /* Command to enter from base binary mode */
		const char *entered_format;  /* Response, for "scanf" */
		char config;  /* Command to setup submode parameters */
	} *submode;

	if (pgm->flag & BP_FLAG_XPARM_RAWFREQ) {
		submode = &(const struct submode){
			.name = "Raw-wire",
			.enter = 0x05,
			.entered_format = "RAW%1d",
			.config = 0x8C,
		};
		pgm->flag |= BP_FLAG_NOPAGEDWRITE;
		pgm->flag |= BP_FLAG_NOPAGEDREAD;
	} else {
		submode = &(const struct submode){
			.name = "SPI",
			.enter = 0x01,
			.entered_format = "SPI%1d",
			
			/* 1000wxyz - SPI config, w=HiZ(0)/3.3v(1), x=CLK idle, y=CLK edge, z=SMP sample
			 * we want: 3.3V(1), idle low(0), data change on
			 *          trailing edge (1), sample in the middle
			 *          of the pulse (0)
			 *       => 0b10001010 = 0x8a */
			.config = 0x8A,
		};
	}

	unsigned char buf[20] = { '\0' };

	/* == Switch to binmode - send 20x '\0' == */
	buspirate_send_bin(pgm, buf, sizeof(buf));

	/* Expecting 'BBIOx' reply */
	memset(buf, 0, sizeof(buf));
	buspirate_recv_bin(pgm, buf, 5);
	if (sscanf((const char*)buf, "BBIO%1d", &PDATA(pgm)->binmode_version) != 1) {
		avrdude_message(MSG_INFO, "Binary mode not confirmed: '%s'\n", buf);
		buspirate_reset_from_binmode(pgm);
		return -1;
	}
	avrdude_message(MSG_NOTICE, "BusPirate binmode version: %d\n",
                PDATA(pgm)->binmode_version);

	pgm->flag |= BP_FLAG_IN_BINMODE;

	if (pgm->flag & BP_FLAG_XPARM_CPUFREQ) {
		unsigned short pwm_duty;
		unsigned short pwm_period;

		pwm_period = 16000/(PDATA(pgm)->cpufreq) - 1; // oscillator runs at 32MHz, we don't use a prescaler
		pwm_duty = pwm_period/2; // 50% duty cycle

		avrdude_message(MSG_NOTICE, "Setting up PWM for cpufreq\n");
		avrdude_message(MSG_DEBUG, "PWM settings: Prescaler=1, Duty Cycle=%hd, Period=%hd\n", pwm_duty, pwm_period);

		buf[0] = 0x12; // pwm setup
		buf[1] = 0; // prescaler 1
		buf[2] = (char) ((pwm_duty >> 8) & 0xff); // duty cycle register, high byte
		buf[3] = (char) pwm_duty & 0xff; // duty cycle register, low byte
		buf[4] = (char) ((pwm_period >> 8) & 0xff); // period register, high byte
		buf[5] = (char) pwm_period & 0xff; // period register, low byte
		buspirate_send_bin(pgm, buf, 6);

		buspirate_recv_bin(pgm, buf, 1);
		if (buf[0] != 0x01)
			avrdude_message(MSG_INFO, "cpufreq (PWM) setup failed\n");
	}

	/* == Set protocol sub-mode of binary mode == */
	buf[0] = submode->enter;
	buspirate_send_bin(pgm, buf, 1);
	memset(buf, 0, sizeof(buf));
	buspirate_recv_bin(pgm, buf, 4);
	if (sscanf((const char*)buf, submode->entered_format, &PDATA(pgm)->submode_version) != 1) {
		avrdude_message(MSG_INFO, "%s mode not confirmed: '%s'\n",
		                submode->name, buf);
		buspirate_reset_from_binmode(pgm);
		return -1;
	}
	avrdude_message(MSG_NOTICE, "BusPirate %s version: %d\n", 
	                submode->name, PDATA(pgm)->submode_version);

	if (pgm->flag & BP_FLAG_NOPAGEDWRITE) {
                avrdude_message(MSG_NOTICE, "%s: Paged flash write disabled.\n", progname);
		pgm->paged_write = NULL;
	} else {
		/* Check for write-then-read without !CS/CS and disable paged_write if absent: */
		static const unsigned char buf2[] = {5,0,0,0,0};
		buspirate_send_bin(pgm, buf2, sizeof(buf2));
		buspirate_recv_bin(pgm, buf, 1);
		if (buf[0] != 0x01) {

			/* Disable paged write: */
			pgm->flag |= BP_FLAG_NOPAGEDWRITE;
			pgm->paged_write = NULL;

			/* Return to SPI mode (0x00s have landed us back in binary bitbang mode): */
			buf[0] = 0x1;
			buspirate_send_bin(pgm, buf, 1);

			avrdude_message(MSG_NOTICE, "%s: Disabling paged flash write. (Need BusPirate firmware >=v5.10.)\n", progname);

			/* Flush serial buffer: */
			serial_drain(&pgm->fd, 0);
		} else {
			avrdude_message(MSG_INFO, "%s: Paged flash write enabled.\n", progname);
		}
	}

	/* 0b0100wxyz - Configure peripherals w=power, x=pull-ups/aux2, y=AUX, z=CS
	 * we want power (0x48) and all reset pins high. */
	PDATA(pgm)->current_peripherals_config  = 0x48 | PDATA(pgm)->reset;
	if (buspirate_expect_bin_byte(pgm, PDATA(pgm)->current_peripherals_config, 0x01) < 0)
		return -1;
	usleep(50000); // sleep for 50ms after power up

	/* 01100xxx -  Set speed */
	if (buspirate_expect_bin_byte(pgm, 0x60 | PDATA(pgm)->spifreq, 0x01) < 0)
		return -1;

	/* Submode config */
	if (buspirate_expect_bin_byte(pgm, submode->config, 0x01) < 0)
		return -1;

	/* AVR Extended Commands - test for existence */
	if (pgm->flag & BP_FLAG_NOPAGEDREAD) {
                avrdude_message(MSG_NOTICE, "%s: Paged flash read disabled.\n", progname);
		pgm->paged_load = NULL;
	} else {
		int rv = buspirate_expect_bin_byte(pgm, 0x06, 0x01);
		if (rv < 0)
			return -1;
		if (rv) {
			unsigned int ver = 0;
			static const unsigned char buf2[] = {1};
			buspirate_send_bin(pgm, buf2, sizeof(buf2));
			buspirate_recv_bin(pgm, buf, 3);
			ver = buf[1] << 8 | buf[2];
			avrdude_message(MSG_NOTICE, "AVR Extended Commands version %d\n", ver);
		} else {
			avrdude_message(MSG_NOTICE, "AVR Extended Commands not found.\n");
			pgm->flag |= BP_FLAG_NOPAGEDREAD;
			pgm->paged_load = NULL;
		}
	}

	return 0;
}

static int buspirate_start_spi_mode_ascii(struct programmer_t *pgm)
{
	int spi_cmd = -1;
	int cmd;
	char *rcvd;
	char buf[5];
	char mode[11];

	buspirate_send(pgm, "m\n");
	while(1) {
		rcvd = buspirate_readline(pgm, NULL, 0);
		if (rcvd == NULL) {
			return -1;
		}
		if (spi_cmd == -1 && sscanf(rcvd, "%2d. %10s", &cmd, mode)) {
			if (strcmp(mode, "SPI") == 0)
				spi_cmd = cmd;
		}
		if (buspirate_is_prompt(rcvd))
			break;
	}
	if (spi_cmd == -1) {
		avrdude_message(MSG_INFO, "%s: SPI mode number not found. Does your BusPirate support SPI?\n",
				progname);
		avrdude_message(MSG_INFO, "%s: Try powercycling your BusPirate and try again.\n",
				progname);
		return -1;
	}
	snprintf(buf, sizeof(buf), "%d\n", spi_cmd);
	buspirate_send(pgm, buf);
	buf[0] = '\0';
	while (1) {
		rcvd = buspirate_readline(pgm, NULL, 0);
		if (rcvd == NULL) {
			return -1;
		}
		if (strstr(rcvd, "Normal (H=3.3V, L=GND)")) {
			/* BP firmware 2.1 defaults to Open-drain output.
			 * That doesn't work on my board, even with pull-up
			 * resistors. Select 3.3V output mode instead. */
			sscanf(rcvd, " %2d.", &cmd);
			snprintf(buf, sizeof(buf), "%d\n", cmd);
		}
		if (buspirate_is_prompt(rcvd)) {
			if (strncmp(rcvd, "SPI>", 4) == 0) {
				avrdude_message(MSG_INFO, "BusPirate is now configured for SPI\n");
				break;
			}
			/* Not yet 'SPI>' prompt */
			if (buf[0]) {
				buspirate_send(pgm, buf);
				buf[0] = '\0';
			} else
				buspirate_send(pgm, "\n");
		}
	}
	return 0;
}

static void buspirate_enable(struct programmer_t *pgm)
{
	static const char *reset_str = "#\n";
	static const char *accept_str = "y\n";
	char *rcvd;
	int rc, print_banner = 0;

	/* Ensure configuration is self-consistant: */
	if (buspirate_verifyconfig(pgm)<0)
		return;         /* XXX should handle error */

	/* Attempt to start binary SPI mode unless explicitly told otherwise: */
	if (!buspirate_uses_ascii(pgm)) {
		avrdude_message(MSG_INFO, "Attempting to initiate BusPirate binary mode...\n");

		/* Send two CRs to ensure we're not in a sub-menu of the UI if we're in ASCII mode: */
		buspirate_send_bin(pgm, (const unsigned char*)"\n\n", 2);

		/* Clear input buffer: */
		serial_drain(&pgm->fd, 0);

		/* Attempt to enter binary mode: */
		if (buspirate_start_mode_bin(pgm) >= 0)
			return;
		else
			avrdude_message(MSG_INFO, "%s: Failed to start binary mode, falling back to ASCII...\n", progname);
	}

	avrdude_message(MSG_INFO, "Attempting to initiate BusPirate ASCII mode...\n");

	/* Call buspirate_send_bin() instead of buspirate_send() 
	 * because we don't know if BP is in text or bin mode */
	rc = buspirate_send_bin(pgm, (const unsigned char*)reset_str, strlen(reset_str));
	if (rc) {
		avrdude_message(MSG_INFO, "BusPirate is not responding. Serial port error: %d\n", rc);
		return;
	}

	while(1) {
		rcvd = buspirate_readline_noexit(pgm, NULL, 0);
		if (! rcvd) {
			avrdude_message(MSG_INFO, "%s: Fatal: Programmer is not responding.\n", progname);
			return;
		}
		if (strncmp(rcvd, "Are you sure?", 13) == 0) {
			buspirate_send_bin(pgm, (const unsigned char*)accept_str, strlen(accept_str));
		}
		if (strncmp(rcvd, "RESET", 5) == 0) {
			print_banner = 1;
			continue;
		}
		if (buspirate_is_prompt(rcvd)) {
			avrdude_message(MSG_DEBUG, "**\n");
			break;
		}
		if (print_banner)
			avrdude_message(MSG_DEBUG, "**  %s", rcvd);
	}

	if (!(pgm->flag & BP_FLAG_IN_BINMODE)) {
		avrdude_message(MSG_INFO, "BusPirate: using ASCII mode\n");
		if (buspirate_start_spi_mode_ascii(pgm) < 0) {
			avrdude_message(MSG_INFO, "%s: Failed to start ascii SPI mode\n", progname);
			return;
		}
	}
}

static void buspirate_disable(struct programmer_t *pgm)
{
	if (pgm->flag & BP_FLAG_IN_BINMODE) {
		serial_recv_timeout = 100;
		buspirate_reset_from_binmode(pgm);
	} else {
		buspirate_expect(pgm, "#\n", "RESET", 1);
	}
}

static int buspirate_initialize(struct programmer_t *pgm, AVRPART * p)
{
	pgm->powerup(pgm);

	return pgm->program_enable(pgm, p);
}

static void buspirate_powerup(struct programmer_t *pgm)
{
	if (pgm->flag & BP_FLAG_IN_BINMODE) {
		/* Powerup in BinMode is handled in binary mode init */
		return;
	} else {
		if (buspirate_expect(pgm, "W\n", "POWER SUPPLIES ON", 1)) {
			if (pgm->flag & BP_FLAG_XPARM_CPUFREQ) {
				char buf[25];
				int ok = 0;
				snprintf(buf, sizeof(buf), "%d\n", PDATA(pgm)->cpufreq);
				if (buspirate_expect(pgm, "g\n", "Frequency in KHz", 1)) {
					if (buspirate_expect(pgm, buf, "Duty cycle in %", 1)) {
						if (buspirate_expect(pgm, "50\n", "PWM active", 1)) {
							ok = 1;
						}
					}
				}
				if(!ok) {
					avrdude_message(MSG_INFO, "%s: warning: did not get a response to start PWM command.\n", progname);
				}
			}
			return;
		}
	}

	avrdude_message(MSG_INFO, "%s: warning: did not get a response to PowerUp command.\n", progname);
	avrdude_message(MSG_INFO, "%s: warning: Trying to continue anyway...\n", progname);
}

static void buspirate_powerdown(struct programmer_t *pgm)
{
	if (pgm->flag & BP_FLAG_IN_BINMODE) {
		/* Powerdown in BinMode is handled in binary mode init */
		return;
	} else {
		if (pgm->flag & BP_FLAG_XPARM_CPUFREQ) {
			if (!buspirate_expect(pgm, "g\n", "PWM disabled", 1)) {
				avrdude_message(MSG_INFO, "%s: warning: did not get a response to stop PWM command.\n", progname);
			}
		}
		if (buspirate_expect(pgm, "w\n", "POWER SUPPLIES OFF", 1))
			return;
	}

	avrdude_message(MSG_INFO, "%s: warning: did not get a response to PowerDown command.\n", progname);
}

static int buspirate_cmd_bin(struct programmer_t *pgm,
				const unsigned char *cmd,
				unsigned char *res)
{
	/* 0001xxxx - Bulk transfer, send/read 1-16 bytes (0=1byte!)
	 * we are sending 4 bytes -> 0x13 */
	int rv = buspirate_expect_bin_byte(pgm, 0x13, 0x01);
	if (rv < 0)
		return -1;
	if (rv == 0)
		return -1;

	buspirate_send_bin(pgm, cmd, 4);
	buspirate_recv_bin(pgm, res, 4);

	return 0;
}

static int buspirate_cmd_ascii(struct programmer_t *pgm,
				const unsigned char *cmd,
				unsigned char *res)
{
	char buf[25];
	char *rcvd;
	int spi_write, spi_read, i = 0;

	snprintf(buf, sizeof(buf), "0x%02x 0x%02x 0x%02x 0x%02x\n",
		cmd[0], cmd[1], cmd[2], cmd[3]);
	buspirate_send(pgm, buf);
	while (i < 4) {
		rcvd = buspirate_readline(pgm, NULL, 0);
		if (rcvd == NULL) {
			return -1;
		}
		/* WRITE: 0xAC READ: 0x04 */
		if (sscanf(rcvd, "WRITE: 0x%2x READ: 0x%2x", &spi_write, &spi_read) == 2) {
			res[i++] = spi_read;
		}
		if (buspirate_is_prompt(rcvd))
			break;
	}

	if (i != 4) {
		avrdude_message(MSG_INFO, "%s: error: SPI has not read 4 bytes back\n", progname);
		return -1;
	}

	/* wait for prompt */
	while (buspirate_getc(pgm) != '>')
		/* do nothing */;

	return 0;
}

static int buspirate_cmd(struct programmer_t *pgm,
				const unsigned char *cmd,
				unsigned char *res)
{
	if (pgm->flag & BP_FLAG_IN_BINMODE)
		return buspirate_cmd_bin(pgm, cmd, res);
	else
		return buspirate_cmd_ascii(pgm, cmd, res);
}

/* Paged load function which utilizes the AVR Extended Commands set */
static int buspirate_paged_load(
		PROGRAMMER *pgm,
		AVRPART *p,
		AVRMEM *m,
		unsigned int page_size,
		unsigned int address,
		unsigned int n_bytes)
{
	unsigned char commandbuf[10];
	unsigned char buf[275];
	unsigned int addr = 0;

	avrdude_message(MSG_NOTICE, "BusPirate: buspirate_paged_load(..,%s,%d,%d,%d)\n",m->desc,m->page_size,address,n_bytes);

	// This should never happen, but still...
	if (pgm->flag & BP_FLAG_NOPAGEDREAD) {
		avrdude_message(MSG_INFO, "BusPirate: buspirate_paged_load() called while in nopagedread mode!\n");
		return -1;
	}

	// determine what type of memory to read, only flash is supported
	if (strcmp(m->desc, "flash") != 0) {
		return -1;
	}

	// send command to read data
	commandbuf[0] = 6;
	commandbuf[1] = 2;

	// send start address (in WORDS, not bytes!)
	commandbuf[2] = (address >> 1 >> 24) & 0xff;
	commandbuf[3] = (address >> 1>> 16) & 0xff;
	commandbuf[4] = (address >> 1 >> 8) & 0xff;
	commandbuf[5] = (address >> 1) & 0xff;

	// send number of bytes to fetch (in BYTES)
	commandbuf[6] = (n_bytes >> 24) & 0xff;
	commandbuf[7] = (n_bytes >> 16) & 0xff;
	commandbuf[8] = (n_bytes >> 8) & 0xff;
	commandbuf[9] = (n_bytes) & 0xff;

	buspirate_send_bin(pgm, commandbuf, 10);
	buspirate_recv_bin(pgm, buf, 1);
	buspirate_recv_bin(pgm, buf, 1);

	if (buf[0] != 0x01) {
		avrdude_message(MSG_INFO, "BusPirate: Paged Read command returned zero.\n");
		return -1;
	}

	for (addr = 0; addr < n_bytes; addr++) {
		buspirate_recv_bin(pgm, &m->buf[addr+address], 1);
	}

	return n_bytes;
}
/* Paged write function which utilizes the Bus Pirate's "Write then Read" binary SPI instruction */
static int buspirate_paged_write(struct programmer_t *pgm,
		AVRPART *p,
		AVRMEM *m,
		unsigned int page_size,
		unsigned int base_addr,
		unsigned int n_data_bytes)
{
	int page, i;
	int addr = base_addr;
	int n_page_writes;
	int this_page_size;
	unsigned char cmd_buf[4096] = {'\0'};
	unsigned char send_byte, recv_byte;

	if (!(pgm->flag & BP_FLAG_IN_BINMODE)) {
		/* Return if we are not in binary mode. */
		return -1;
	}

	if (pgm->flag & BP_FLAG_NOPAGEDWRITE) {
		/* Return if we've nominated not to use paged writes. */
		return -1;
	}

	if (page_size>1024) {
		/* Page sizes greater than 1kB not yet supported. */
		return -1;
	}

	if (strcmp(m->desc,"flash") != 0) {
		/* Only flash memory currently supported. */
		return -1;
	}

	/* pre-check opcodes */
	if (m->op[AVR_OP_LOADPAGE_LO] == NULL) {
		avrdude_message(MSG_INFO, "%s failure: %s command not defined for %s\n",
		                progname, "AVR_OP_LOADPAGE_LO", p->desc);
		return -1;
	}
	if (m->op[AVR_OP_LOADPAGE_HI] == NULL) {
		avrdude_message(MSG_INFO, "%s failure: %s command not defined for %s\n",
		                progname, "AVR_OP_LOADPAGE_HI", p->desc);
		return -1;
	}

	/* Calculate total number of page writes needed: */
	n_page_writes = n_data_bytes/page_size;
	if (n_data_bytes%page_size >0)
		n_page_writes++;

	/* Ensure error LED is off: */
	pgm->err_led(pgm, OFF);

	/* Loop over pages: */
	for (page=0; page<n_page_writes; page++) {

		/* Determine bytes to write in this page: */
		this_page_size = page_size;
		if (page == n_page_writes-1)
			this_page_size = n_data_bytes - page_size*page;

		/* Set up command buffer: */
		memset(cmd_buf, 0, 4*this_page_size);
		for (i=0; i<this_page_size; i++) {

			addr = base_addr + page*page_size + i;

			if (i%2 == 0) {
				avr_set_bits(m->op[AVR_OP_LOADPAGE_LO], &(cmd_buf[4*i]));
				avr_set_addr(m->op[AVR_OP_LOADPAGE_LO], &(cmd_buf[4*i]), addr/2);
				avr_set_input(m->op[AVR_OP_LOADPAGE_LO], &(cmd_buf[4*i]), m->buf[addr]);
			} else {
				avr_set_bits(m->op[AVR_OP_LOADPAGE_HI], &(cmd_buf[4*i]));
				avr_set_addr(m->op[AVR_OP_LOADPAGE_HI], &(cmd_buf[4*i]), addr/2);
				avr_set_input(m->op[AVR_OP_LOADPAGE_HI], &(cmd_buf[4*i]), m->buf[addr]);
			}
		}

		/* 00000100 - Write then read */
		send_byte = 0x05;
		buspirate_send_bin(pgm, &send_byte, 1);

		/* Number of bytes to write: */
		send_byte = (4*this_page_size)/0x100;
		buspirate_send_bin(pgm, &send_byte, 1); /* High byte */
		send_byte = (4*this_page_size)%0x100;
		buspirate_send_bin(pgm, &send_byte, 1); /* Low byte */

		/* Number of bytes to read: */
		send_byte = 0x0;
		buspirate_send_bin(pgm, &send_byte, 1); /* High byte */
		buspirate_send_bin(pgm, &send_byte, 1); /* Low byte */

		/* Set programming LED: */
		pgm->pgm_led(pgm, ON);

		/* Send command buffer: */
		buspirate_send_bin(pgm, cmd_buf, 4*this_page_size);

		/* Check for write failure: */
		if ((buspirate_recv_bin(pgm, &recv_byte, 1) == EOF) || (recv_byte != 0x01)) {
			avrdude_message(MSG_INFO, "BusPirate: Fatal error: Write Then Read did not succeed.\n");
			pgm->pgm_led(pgm, OFF);
			pgm->err_led(pgm, ON);
			return -1;
		}

		/* Unset programming LED: */
		pgm->pgm_led(pgm, OFF);

		/* Write loaded page to flash: */
		avr_write_page(pgm, p, m, addr);
	}

	return n_data_bytes;
}

static int buspirate_program_enable(struct programmer_t *pgm, AVRPART * p)
{
	unsigned char cmd[4];
	unsigned char res[4];

	if (pgm->flag & BP_FLAG_IN_BINMODE) {
		/* Clear configured reset pin(s): CS and/or AUX and/or AUX2 */
		PDATA(pgm)->current_peripherals_config &= ~PDATA(pgm)->reset;
		if (buspirate_expect_bin_byte(pgm, PDATA(pgm)->current_peripherals_config, 0x01) < 0)
			return -1;
	}
	else
		buspirate_expect(pgm, "{\n", "CS ENABLED", 1);

	if (p->op[AVR_OP_PGM_ENABLE] == NULL) {
		avrdude_message(MSG_INFO, "program enable instruction not defined for part \"%s\"\n",
		                p->desc);
		return -1;
	}

	memset(cmd, 0, sizeof(cmd));
	avr_set_bits(p->op[AVR_OP_PGM_ENABLE], cmd);
	pgm->cmd(pgm, cmd, res);

	if (res[2] != cmd[1])
		return -2;

	return 0;
}

static int buspirate_chip_erase(struct programmer_t *pgm, AVRPART * p)
{
	unsigned char cmd[4];
	unsigned char res[4];

	if (p->op[AVR_OP_CHIP_ERASE] == NULL) {
		avrdude_message(MSG_INFO, "chip erase instruction not defined for part \"%s\"\n",
		                p->desc);
		return -1;
	}

	pgm->pgm_led(pgm, ON);

	memset(cmd, 0, sizeof(cmd));

	avr_set_bits(p->op[AVR_OP_CHIP_ERASE], cmd);
	pgm->cmd(pgm, cmd, res);
	usleep(p->chip_erase_delay);
	pgm->initialize(pgm, p);

	pgm->pgm_led(pgm, OFF);

	return 0;
}

/* Interface - management */
static void buspirate_setup(struct programmer_t *pgm)
{
	/* Allocate private data */
	if ((pgm->cookie = calloc(1, sizeof(struct pdata))) == 0) {
		// avrdude_message(MSG_INFO, "%s: buspirate_initpgm(): Out of memory allocating private data\n",
		//                 progname);
		// exit(1);
		avrdude_oom("buspirate_initpgm(): Out of memory allocating private data\n");
	}
	PDATA(pgm)->serial_recv_timeout = 100;
}

static void buspirate_teardown(struct programmer_t *pgm)
{
	free(pgm->cookie);
}
const char buspirate_desc[] = "Using the Bus Pirate's SPI interface for programming";

void buspirate_initpgm(struct programmer_t *pgm)
{
	strcpy(pgm->type, "BusPirate");

	pgm->display        = buspirate_dummy_6;

	/* BusPirate itself related methods */
	pgm->open           = buspirate_open;
	pgm->close          = buspirate_close;
	pgm->enable         = buspirate_enable;
	pgm->disable        = buspirate_disable;
	pgm->initialize     = buspirate_initialize;

	/* Chip related methods */
	pgm->powerup        = buspirate_powerup;
	pgm->powerdown      = buspirate_powerdown;
	pgm->program_enable = buspirate_program_enable;
	pgm->chip_erase     = buspirate_chip_erase;
	pgm->cmd            = buspirate_cmd;
	pgm->read_byte      = avr_read_byte_default;
	pgm->write_byte     = avr_write_byte_default;

	pgm->paged_write    = buspirate_paged_write;
	pgm->paged_load	    = buspirate_paged_load;

	/* Support functions */
	pgm->parseextparams = buspirate_parseextparms;

	pgm->setup          = buspirate_setup;
	pgm->teardown       = buspirate_teardown;
}

/* Bitbang support */

static void buspirate_bb_enable(struct programmer_t *pgm)
{
	unsigned char buf[20] = { '\0' };

	if (bitbang_check_prerequisites(pgm) < 0)
		return;             /* XXX should treat as error */

	avrdude_message(MSG_INFO, "Attempting to initiate BusPirate bitbang binary mode...\n");

	/* Send two CRs to ensure we're not in a sub-menu of the UI if we're in ASCII mode: */
	buspirate_send_bin(pgm, (const unsigned char*)"\n\n", 2);

	/* Clear input buffer: */
	serial_drain(&pgm->fd, 0);

	/* == Switch to binmode - send 20x '\0' == */
	buspirate_send_bin(pgm, buf, sizeof(buf));

	/* Expecting 'BBIOx' reply */
	memset(buf, 0, sizeof(buf));
	buspirate_recv_bin(pgm, buf, 5);
	if (sscanf((char*)buf, "BBIO%1d", &PDATA(pgm)->binmode_version) != 1) {
		avrdude_message(MSG_INFO, "Binary mode not confirmed: '%s'\n", buf);
		buspirate_reset_from_binmode(pgm);
		return;
	}
	avrdude_message(MSG_INFO, "BusPirate binmode version: %d\n",
	                PDATA(pgm)->binmode_version);

	pgm->flag |= BP_FLAG_IN_BINMODE;

	/* Set pin directions and an initial pin status (all high) */
	PDATA(pgm)->pin_dir = 0x12;  /* AUX, MISO input; everything else output */
	buf[0] = PDATA(pgm)->pin_dir | 0x40;
	buspirate_send_bin(pgm, buf, 1);
	buspirate_recv_bin(pgm, buf, 1);

	PDATA(pgm)->pin_val = 0x3f; /* PULLUP, AUX, MOSI, CLK, MISO, CS high */
	buf[0] = PDATA(pgm)->pin_val | 0x80;
	buspirate_send_bin(pgm, buf, 1);
	buspirate_recv_bin(pgm, buf, 1);

	/* Done */
	return;
}

/* 
   Direction:
   010xxxxx
   Input (1) or output (0):
   AUX|MOSI|CLK|MISO|CS

   Output value:
   1xxxxxxx
   High (1) or low(0):
   1|POWER|PULLUP|AUX|MOSI|CLK|MISO|CS

   Both respond with a byte with current status:
   0|POWER|PULLUP|AUX|MOSI|CLK|MISO|CS
*/
static int buspirate_bb_getpin(struct programmer_t *pgm, int pinfunc)
{
	unsigned char buf[10];
	int value = 0;
	int pin = pgm->pinno[pinfunc];

	if (pin & PIN_INVERSE) {
		pin &= PIN_MASK;
		value = 1;
	}

	if (pin < 1 || pin > 5)
		return -1;

	buf[0] = PDATA(pgm)->pin_dir | 0x40;
	if (buspirate_send_bin(pgm, buf, 1) < 0)
		return -1;
	/* Read all of the previously-expected-but-unread bytes */
	while (PDATA(pgm)->unread_bytes > 0) {
		if (buspirate_recv_bin(pgm, buf, 1) < 0)
			return -1;
		PDATA(pgm)->unread_bytes--;
	}

	/* Now read the actual response */
	if (buspirate_recv_bin(pgm, buf, 1) < 0)
		return -1;

	if (buf[0] & (1 << (pin - 1)))
		value ^= 1;

	avrdude_message(MSG_DEBUG, "get pin %d = %d\n", pin, value);

	return value;
}

static int buspirate_bb_setpin_internal(struct programmer_t *pgm, int pin, int value)
{
	unsigned char buf[10];

	if (pin & PIN_INVERSE) {
		value = !value;
		pin &= PIN_MASK;
	}

	if ((pin < 1 || pin > 5) && (pin != 7)) // 7 is POWER
		return -1;

	avrdude_message(MSG_DEBUG, "set pin %d = %d\n", pin, value);

	if (value)
		PDATA(pgm)->pin_val |= (1 << (pin - 1));
	else 
		PDATA(pgm)->pin_val &= ~(1 << (pin - 1));

	buf[0] = PDATA(pgm)->pin_val | 0x80;
	if (buspirate_send_bin(pgm, buf, 1) < 0)
		return -1;
	/* We'll get a byte back, but we don't need to read it now.
	   This is just a quick optimization that saves some USB
	   round trips, improving read times by a factor of 3. */
	PDATA(pgm)->unread_bytes++;

	return 0;
}

static int buspirate_bb_setpin(struct programmer_t *pgm, int pinfunc, int value)
{
	return buspirate_bb_setpin_internal(pgm, pgm->pinno[pinfunc], value);
}


static int buspirate_bb_highpulsepin(struct programmer_t *pgm, int pinfunc)
{
	int ret;
	ret = buspirate_bb_setpin(pgm, pinfunc, 1);
	if (ret < 0)
		return ret;
	return buspirate_bb_setpin(pgm, pinfunc, 0);
}

static void buspirate_bb_powerup(struct programmer_t *pgm)
{
	buspirate_bb_setpin_internal(pgm, 7, 1);
}

static void buspirate_bb_powerdown(struct programmer_t *pgm)
{
	buspirate_bb_setpin_internal(pgm, 7, 0);
}

const char buspirate_bb_desc[] = "Using the Bus Pirate's bitbang interface for programming";

void buspirate_bb_initpgm(struct programmer_t *pgm)
{
	strcpy(pgm->type, "BusPirate_BB");

	pgm_fill_old_pins(pgm); // TODO to be removed if old pin data no longer needed

	pgm->display        = buspirate_dummy_6;

	/* BusPirate itself related methods */
	pgm->setup          = buspirate_setup;
	pgm->teardown       = buspirate_teardown;
	pgm->open           = buspirate_open;
	pgm->close          = buspirate_close;
	pgm->enable         = buspirate_bb_enable;
	pgm->disable        = buspirate_disable;

	/* Chip related methods */
	pgm->initialize     = bitbang_initialize;
	pgm->rdy_led        = bitbang_rdy_led;
	pgm->err_led        = bitbang_err_led;
	pgm->pgm_led        = bitbang_pgm_led;
	pgm->vfy_led        = bitbang_vfy_led;
	pgm->program_enable = bitbang_program_enable;
	pgm->chip_erase     = bitbang_chip_erase;
	pgm->cmd            = bitbang_cmd;
	pgm->cmd_tpi        = bitbang_cmd_tpi;
	pgm->powerup        = buspirate_bb_powerup;
	pgm->powerdown      = buspirate_bb_powerdown;
	pgm->setpin         = buspirate_bb_setpin;
	pgm->getpin         = buspirate_bb_getpin;
	pgm->highpulsepin   = buspirate_bb_highpulsepin;
	pgm->read_byte      = avr_read_byte_default;
	pgm->write_byte     = avr_write_byte_default;
}
