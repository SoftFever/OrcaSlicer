#
# avrdude - A Downloader/Uploader for AVR device programmers
# Copyright (C) 2003  Theodore A. Roth  <troth@openavr.org>
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

#
# This Makefile will only be used on windows based systems.
#

local_install_list = \
	giveio.sys \
	install_giveio.bat \
	remove_giveio.bat \
	status_giveio.bat

EXTRA_DIST   = \
	giveio.c \
	$(local_install_list)

bin_PROGRAMS = loaddrv

loaddrv_SOURCES = \
	loaddrv.c \
	loaddrv.h

install-exec-local:
	$(mkinstalldirs) $(DESTDIR)$(bindir)
	@list='$(local_install_list)'; for file in $$list; do \
		echo " $(INSTALL_PROGRAM_ENV) $(INSTALL_PROGRAM) \
			$(srcdir)/$$file $(DESTDIR)$(bindir)/$$file"; \
		$(INSTALL_PROGRAM_ENV) $(INSTALL_PROGRAM) $(srcdir)/$$file \
			$(DESTDIR)$(bindir)/$$file; \
	done

uninstall-local:
	@for file in $(local_install_list); do \
		echo " rm -f $(DESTDIR)$(bindir)/$$file"; \
		rm -f $(DESTDIR)$(bindir)/$$file; \
	done

