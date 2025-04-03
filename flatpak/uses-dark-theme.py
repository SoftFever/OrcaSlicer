#!/usr/bin/env python3

import os
import sys
import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk as gtk


if __name__ == '__main__':
    forced = os.environ.get('ORCA_SLICER_DARK_THEME', 'false') == 'true'
    settings = gtk.Settings.get_default()
    prefer_dark = settings.get_property('gtk-application-prefer-dark-theme')

    if not forced and not prefer_dark:
        sys.exit(1)
    else:
        sys.exit(0)
