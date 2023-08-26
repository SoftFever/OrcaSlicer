#
# avrdude - A Downloader/Uploader for AVR device programmers
# Copyright (C) 2003, 2004  Theodore A. Roth  <troth@openavr.org>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#

#
# $Id$
#

EXTRA_DIST   = \
	ChangeLog \
	ChangeLog-2001 \
	ChangeLog-2002 \
	ChangeLog-2003 \
	ChangeLog-2004-2006 \
	ChangeLog-2007 \
	ChangeLog-2008 \
	ChangeLog-2009 \
	ChangeLog-2010 \
	ChangeLog-2011 \
	ChangeLog-2012 \
	ChangeLog-2013 \
	avrdude.1 \
	avrdude.spec \
	bootstrap

CLEANFILES = \
	config_gram.c \
	config_gram.h \
	lexer.c

BUILT_SOURCES = $(CLEANFILES)

#SUBDIRS      = doc @WINDOWS_DIRS@
#DIST_SUBDIRS = doc windows

# . lets build this directory before the following in SUBDIRS
SUBDIRS = .
# doc comes here, and we want to use the built avrdude to generate the parts list
SUBDIRS += @SUBDIRS_AC@
SUBDIRS += @WINDOWS_DIRS@
DIST_SUBDIRS = @DIST_SUBDIRS_AC@

AM_YFLAGS    = -d

avrdude_CPPFLAGS = -DCONFIG_DIR=\"$(sysconfdir)\"

libavrdude_a_CPPFLAGS = -DCONFIG_DIR=\"$(sysconfdir)\"
libavrdude_la_CPPFLAGS = $(libavrdude_a_CPPFLAGS)

avrdude_CFLAGS   = @ENABLE_WARNINGS@

libavrdude_a_CFLAGS   = @ENABLE_WARNINGS@
libavrdude_la_CFLAGS  = $(libavrdude_a_CFLAGS)

avrdude_LDADD  = $(top_builddir)/$(noinst_LIBRARIES) @LIBUSB_1_0@ @LIBHIDAPI@ @LIBUSB@ @LIBFTDI1@ @LIBFTDI@ @LIBHID@ @LIBELF@ @LIBPTHREAD@ -lm

bin_PROGRAMS = avrdude

noinst_LIBRARIES = libavrdude.a
lib_LTLIBRARIES = libavrdude.la

# automake thinks these generated files should be in the distribution,
# but this might cause trouble for some users, so we rather don't want
# to have them there.
#
# See
#
# https://savannah.nongnu.org/bugs/index.php?func=detailitem&item_id=15536
#
# for why we don't want to have them.
dist-hook:
	rm -f \
	$(distdir)/lexer.c \
	$(distdir)/config_gram.c \
	$(distdir)/config_gram.h

libavrdude_a_SOURCES = \
	config_gram.y \
	lexer.l \
	arduino.h \
	arduino.c \
	avr.c \
	avr910.c \
	avr910.h \
	avrdude.h \
	avrftdi.c \
	avrftdi.h \
	avrftdi_private.h \
	avrftdi_tpi.c \
	avrftdi_tpi.h \
	avrpart.c \
	bitbang.c \
	bitbang.h \
	buspirate.c \
	buspirate.h \
	butterfly.c \
	butterfly.h \
	config.c \
	config.h \
	confwin.c \
	crc16.c \
	crc16.h \
	dfu.c \
	dfu.h \
	fileio.c \
	flip1.c \
	flip1.h \
	flip2.c \
	flip2.h \
	freebsd_ppi.h \
	ft245r.c \
	ft245r.h \
	jtagmkI.c \
	jtagmkI.h \
	jtagmkI_private.h \
	jtagmkII.c \
	jtagmkII.h \
	jtagmkII_private.h \
	jtag3.c \
	jtag3.h \
	jtag3_private.h \
	libavrdude.h \
	linuxgpio.c \
	linuxgpio.h \
	linux_ppdev.h \
	lists.c \
	my_ddk_hidsdi.h \
	par.c \
	par.h \
	pgm.c \
	pgm_type.c \
	pickit2.c \
	pickit2.h \
	pindefs.c \
	ppi.c \
	ppi.h \
	ppiwin.c \
	safemode.c \
	serbb.h \
	serbb_posix.c \
	serbb_win32.c \
	ser_avrdoper.c \
	ser_posix.c \
	ser_win32.c \
	solaris_ecpp.h \
	stk500.c \
	stk500.h \
	stk500_private.h \
	stk500v2.c \
	stk500v2.h \
	stk500v2_private.h \
	stk500generic.c \
	stk500generic.h \
	tpi.h \
	usbasp.c \
	usbasp.h \
	usbdevs.h \
	usb_hidapi.c \
	usb_libusb.c \
	usbtiny.h \
	usbtiny.c \
	update.c \
	wiring.h \
	wiring.c
libavrdude_la_SOURCES = $(libavrdude_a_SOURCES)
libavrdude_la_LDFLAGS = -version-info 1:0

include_HEADERS = libavrdude.h

avrdude_SOURCES = \
	main.c \
	term.c \
	term.h

man_MANS = avrdude.1

sysconf_DATA = avrdude.conf

install-exec-local: backup-avrdude-conf

distclean-local:
	rm -f avrdude.conf

# This will get run before the config file is installed.
backup-avrdude-conf:
	@echo "Backing up avrdude.conf in ${DESTDIR}${sysconfdir}"
	@if test -e ${DESTDIR}${sysconfdir}/avrdude.conf; then \
		cp -pR ${DESTDIR}${sysconfdir}/avrdude.conf \
			${DESTDIR}${sysconfdir}/avrdude.conf.bak; \
	fi

ACLOCAL_AMFLAGS = -I m4
