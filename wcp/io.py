# Copyright (C) 2014  Peter Feiner

import os
import inspect
import collections

EVENT_TYPES = {
    'SAMPLE': 0,
    'START': 1,
    'STOP': 2,
}
EVENT_NAMES = dict((v, k) for k, v in EVENT_TYPES.items())
for k, v in EVENT_TYPES.items():
    globals()['%s_EVENT' % k] = v

class Frame(object):
    def __init__(self, filename, lineno, name, firstlineno):
        self.filename = filename
        self.lineno = lineno
        self.name = name
        self.firstlineno = firstlineno

    def __hash__(self):
        return hash((self.filename, self.lineno))

    def __eq__(self, other):
        return self.filename == other.filename and\
               self.lineno == other.lineno

    def __repr__(self):
        return 'Frame(%s, %s, %s, %s)' %\
               (self.filename, self.lineno, self.name, self.firstlineno)

    def __str__(self):
        return '%s:%d in %s' %\
               (self.filename, self.lineno, self.name)

class Event(object):
    def __init__(self, time_, pid, tid, event_type, data=None):
        self.time = time_
        self.pid = pid
        self.tid = tid
        self.event_type = event_type
        self.data = data

    def __repr__(self):
        return 'Event(%r, %r, %r, %s, %r)' %\
               (self.time, self.pid, self.tid, EVENT_NAMES[self.event_type],
                self.data)

    def __str__(self):
        return '%s at %rs, pid=%s tid=%s: %s' %\
               (EVENT_NAMES[self.event_type], self.time, self.pid, self.tid,
                self.data)

class SampleData(object):
    def __init__(self, frames):
        self.frames = frames

    def __repr__(self):
        return 'SampleData(%r)' % self.frames

    def __str__(self):
        return '\n'.join(map(str, self.frames))

def read_frames(fp):
    frames = []
    while True:
        if fp.peek() == '\n':
            read_const(fp, '\n')
            break
        filename = read_cstr(fp)
        lineno = int(read_cstr(fp))
        name = read_cstr(fp)
        firstlineno = int(read_cstr(fp))
        read_const(fp, '\n')
        frames.append(Frame(filename, lineno, name, firstlineno))
    return frames
    
def read_cstr(fp):
    out = []
    while True:
        c = fp.read(1)
        if c == '':
            raise IOError('End of file')
        elif c == '\0':
            return ''.join(out)
        out.append(c)

def read_const(fp, expected):
    actual = fp.read(len(expected))
    if actual != expected:
        raise IOError('Expected %r, got %r' % (expected, actual))

def eof(fp):
    return fp.peek() == ''

def read_header(fp):
    time_ = float(read_cstr(fp))
    pid = int(read_cstr(fp))
    tid = int(read_cstr(fp))
    event_type = int(read_cstr(fp))
    read_const(fp, '\n')
    return Event(time_, pid, tid, event_type)

def read_sample_data(fp):
    frames = read_frames(fp)
    return SampleData(frames)

class PeekableFile(object):
    def __init__(self, fp):
        self.fp = fp
        self.buf = ''

    def read(self, n=-1):
        if n == 0:
            return ''
        r = self.buf + self.fp.read(n - len(self.buf))
        self.buf = ''
        return r

    def peek(self):
        if not self.buf:
            self.buf = self.fp.read(1)
        return self.buf

def read_events(fp):
    fp = PeekableFile(fp)
    try:
        while True:
            if eof(fp):
                break
            event = read_header(fp)
            if event.event_type == SAMPLE_EVENT:
                event.data = read_sample_data(fp)
            yield event
    except IOError:
        raise
        # Read as many as we can.
        return
