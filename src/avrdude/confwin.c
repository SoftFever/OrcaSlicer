/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2003-2004  Eric B. Weddington <eric@ecentral.com>
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


#include "avrdude.h"
#include "libavrdude.h"

#if defined(WIN32NATIVE)

#include <limits.h>
#include <windows.h>


static char *filename;


void win_sys_config_set(char sys_config[PATH_MAX])
{
    sys_config[0] = 0;
    
    /* Use Windows API call to search for the Windows default system config file.*/
    SearchPath(NULL, "avrdude.conf", NULL, PATH_MAX, sys_config, &filename);
    return;
}


void win_usr_config_set(char usr_config[PATH_MAX])
{
    usr_config[0] = 0;
    
    /* Use Windows API call to search for the Windows default user config file. */
	SearchPath(NULL, "avrdude.rc", NULL, PATH_MAX, usr_config, &filename);
    return;
}


#endif


