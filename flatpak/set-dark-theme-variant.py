#!/usr/bin/env python3

import Xlib
import Xlib.display
import time
import subprocess
import os
import sys


disp = Xlib.display.Display()
root = disp.screen().root

NET_CLIENT_LIST = disp.intern_atom('_NET_CLIENT_LIST')


def set_theme_variant_by_window_id(id, variant):
    # Use subprocess to call
    # xprop and set the variant from id.
    try:
        s = subprocess.call(['xprop', '-f', '_GTK_THEME_VARIANT', '8u', '-set', '_GTK_THEME_VARIANT', variant, '-id', str(id)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if s == 0:
            return True
        return False
    except Exception as ex:
        return False


def set_theme_variant_from_win_id_collection(win_id_collection, variant):
    # Loop though all of the collected
    # window ids and set theme variant
    for win_id in win_id_collection:
        set_theme_variant_by_window_id(win_id, variant)


def collection_win_id_from_wm_class_name(win_class_name):

    collect = []

    # Loop though all of the windows
    # and collect id's those that match
    # win_class: prusa-slicer
    for win_id in root.get_full_property(NET_CLIENT_LIST, Xlib.X.AnyPropertyType).value:
        try:
            win = disp.create_resource_object('window', win_id)
            if not win.get_wm_transient_for():
                win_class = win.get_wm_class()
                if win_id and win_class_name in win_class:
                    collect.append(
                        win_id) if win_id not in collect else collect
        except Xlib.error.BadWindow:
            pass

    return collect


if __name__ == '__main__':

    if os.environ.get('PRUSA_SLICER_DARK_THEME', 'false') != 'true':
        sys.exit(0)

    # Listen for X Property Change events.
    root.change_attributes(event_mask=Xlib.X.PropertyChangeMask)
    # the class name of the slicer window
    win_class_name = 'prusa-slicer'
    # the variant to set
    variant = 'dark'

    start = time.time()

    while True:
        # collect all of the window ids
        collect = collection_win_id_from_wm_class_name(win_class_name)
        # give PrusaSlicer window 2 secs to
        # collect the wanted window ids
        # set the theme variant and exit
        if time.time() - start <= 2:
            # disp.next_event() blocks if no events are
            # queued. In combination with while True
            # it creates a very simple event loop.
            disp.next_event()
            set_theme_variant_from_win_id_collection(collect, variant)
        else:
            break
