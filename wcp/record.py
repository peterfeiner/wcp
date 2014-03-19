# Copyright (C) 2014  Peter Feiner

import sys
import os
import inspect
import __builtin__

try:
    from . import _record_impl as record_impl
except ImportError:
    from . import record_impl

class Options(object):
    frequency = 10
    out_fd = None
    ignore = None
    follow_fork = True
    autostart = True
    stop_signal = None
    start_signal = None
    sample_greenlets = False

def setup(options):
    record_impl.setup(options)

def stop():
    record_impl.stop()

def start():
    record_impl.start()

def toggle():
    record_impl.toggle()

def record_script(argv, options):
    # TODO: Find script_path in $PATH.
    options.ignore = record_script.__code__
    setup(options)
    sys.argv = argv
    execfile(argv[0], {'__name__': '__main__',
                       '__builtins__': __builtin__,
                       '__file__': argv[0],
                       '__doc__': None})
