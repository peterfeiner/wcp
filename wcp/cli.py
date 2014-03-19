# Copyright (C) 2014  Peter Feiner

import argparse
import sys
import os
import signal

from . import record
from . import report

SIGNALS =  {}
for name in dir(signal):
    if name.startswith('SIG') and not name.startswith('SIG_'):
        SIGNALS[name] = getattr(signal, name)

def parse_signal(string):
    if string is None:
        return None
    try:
        return int(string)
    except ValueError:
        name = string.upper()
        if not name.startswith('SIG'):
            name = 'SIG%s' %  name
        try:
            return SIGNALS[name]
        except KeyError:
            raise Exception('Invalid signal %r. Use a number or a name: %s'\
                            % (string, ', '.join(SIGNALS.keys())))


def record_main(args):
    parser = argparse.ArgumentParser(prog='wcp record')
    parser.add_argument('script_path',
                        help='Python script to profile.')
    parser.add_argument('-f', '--frequency', default=10, type=float,
                        help='Number of samples per second. Default is 10.')
    parser.add_argument('-o', '--output', default='wcp.data',
                        help='Output file. Default is wcp.data')
    parser.add_argument('-d', '--detach-fork', action='store_true',
                        help='Do not sample child processes.')
    parser.add_argument('-g', '--sample-greenlets', action='store_true',
                        help='Treat greenlet coroutintes like threads.'),
    parser.add_argument('-s', '--start-signal', default=None,
                        help='Start sampling on this signal. '
                             'Disabled by default.'),
    parser.add_argument('-S', '--stop-signal', default=None,
                        help='Stop sampling on this signal. '
                             'Disabled by default.'),
    parser.add_argument('-n', '--no-autostart', action='store_true',
                        help='Do not start sampling; wait for signal.')
    opts, script_args = parser.parse_known_args(args)
    argv = [opts.script_path] + script_args

    record_opts = record.Options()
    record_opts.frequency = opts.frequency
    record_opts.out_fd = os.open(opts.output,
                                 os.O_WRONLY | os.O_TRUNC |
                                 os.O_CREAT | os.O_APPEND,
                                 0666)
    record_opts.follow_fork = not opts.detach_fork
    record_opts.sample_greenlets = opts.sample_greenlets
    record_opts.autostart = not opts.no_autostart

    start_signal = parse_signal(opts.start_signal)
    stop_signal = parse_signal(opts.stop_signal)
    if start_signal is None and opts.no_autostart:
        raise Exception('Need start signal if autostart is disabled.')
    if stop_signal is None:
        stop_signal = start_signal
    record_opts.stop_signal = stop_signal
    record_opts.start_signal = start_signal

    record.record_script(argv, record_opts)

def report_main(args):
    parser = argparse.ArgumentParser(prog='wcp report')
    parser.add_argument('-d', '--data-path', default='wcp.data',
                        help='Sample file. Default is wcp.data.')
    parser.add_argument('-t', '--top-down', action='store_true',
                        help='Root call chain at entry points.')
    opts = parser.parse_args(args)

    report_opts = report.Options()
    report_opts.data_path = opts.data_path
    report_opts.top_down = opts.top_down
    report.write(report_opts, sys.stdout)

def main():
    add_help = True
    i = 0
    for i in range(1, len(sys.argv)):
        arg = sys.argv[i]
        if arg.startswith('-') and not arg.startswith('--') and 'h' in arg or\
           arg == '--help':
            break
        if not arg.startswith('-'):
            add_help = False
            break

    commands = dict([(name[:-5], value) for name, value in globals().items()
                     if name.endswith('_main')])

    parser = argparse.ArgumentParser(add_help=add_help)
    parser.add_argument('command', help='one of %s' % ', '.join(commands))
    opts = parser.parse_args(sys.argv[1:i + 1])
    command_args = sys.argv[i + 1:]

    try:
        command_main = commands[opts.command]
    except KeyError:
        sys.stderr.write('invalid command: %s\n' % opts.command)
        sys.exit(1)

    command_main(command_args)

if __name__ == '__main__':
    main() 
