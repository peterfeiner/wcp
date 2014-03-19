# Copyright (C) 2014  Peter Feiner

import sys

def safe_import(name):
    if 'eventlet' in sys.modules:
        import eventlet.patcher
        return eventlet.patcher.original(name)
    else:
        return __import__(name)

fcntl = safe_import('fcntl')
os = safe_import('os')
select = safe_import('select')
threading = safe_import('threading')
time = safe_import('time')
signal = safe_import('signal')

import contextlib
import gc
import inspect
import cStringIO

from . import io

class State(object):
    def __init__(self):
        self.reset()

    def reset(self):
        self.thread = None
        self.pipe = None
        self.options = None
        self.sampling = False

state = State()

def set_cloexec(fd):
    fcntl.fcntl(fd, fcntl.F_SETFD, fcntl.FD_CLOEXEC)

def safe_write(fd, buf):
    r = 0
    while r < len(buf):
        r = os.write(fd, buf[r:])

@contextlib.contextmanager
def flock(fd, op=fcntl.LOCK_EX):
    fcntl.flock(fd, op)
    try:
        yield
    finally:
        fcntl.flock(fd, fcntl.LOCK_UN)

def reopen(fd, mode):
        new_fd = os.open('/proc/self/fd/%d' % fd, mode)
        os.close(fd)
        return new_fd

def event_header(now, pid, tid, event):
    return '%f\0%d\0%d\0%s\0\n' % (now, pid, tid, event)

def write_start_stop_event(event):
    with flock(state.options.out_fd):
        safe_write(state.options.out_fd,
                   event_header(time.time(), os.getpid(), 0, event))

def write_stop():
    write_start_stop_event(io.STOP_EVENT)

def write_start():
    write_start_stop_event(io.START_EVENT)

def write_sample(now, pid, tid, frame):
    buf = cStringIO.StringIO()
    buf.write(event_header(now, pid, tid, io.SAMPLE_EVENT))
    while frame is not None:
        if frame.f_code == state.options.ignore:
            break
        buf.write('%s\0%d\0%s\0%d\0\n' % 
                  (os.path.abspath(frame.f_code.co_filename),
                   frame.f_lineno,
                   frame.f_code.co_name,
                   frame.f_code.co_firstlineno))
        frame = frame.f_back
    buf.write('\n')

    with flock(state.options.out_fd):
        safe_write(state.options.out_fd, buf.getvalue())

orig_greenlet = None
all_greenlets = None
greenlet_lock = threading.Lock()

def hijack_greenlet():
    global orig_greenlet
    global all_greenlets

    import greenlet

    try:
        if orig_greenlet is None:
            orig_greenlet = greenlet.greenlet
        all_greenlets = set()

        class Greenlet(orig_greenlet):
            def __init__(self, *args, **kwargs):
                orig_greenlet.__init__(self, *args, **kwargs)
                with greenlet_lock:
                    all_greenlets.add(self)

        # Grab all of the greenlet objects created before we hijacked.
        with greenlet_lock:
            for o in gc.get_objects():
                if isinstance(o, orig_greenlet):
                    all_greenlets.add(o)
            orig_greenlet = greenlet.greenlet
            greenlet.greenlet = Greenlet
    except:
        orig_greenlet = None
        raise

def frames():
    for tid, frame in sys._current_frames().iteritems():
        yield tid, frame
    if state.options.sample_greenlets:
        with greenlet_lock:
            all_greenlets_copy = set(all_greenlets)
        dead_greenlets = set()
        try:
            for gt in all_greenlets:
                if gt.dead:
                    dead_greenlets.add(gt)
                elif gt.gr_frame is not None:
                    yield id(gt), gt.gr_frame
        finally:
            with greenlet_lock:
                all_greenlets.difference_update(dead_greenlets)

def collect_sample():
    now = time.time()
    pid = os.getpid()
    current_tid = threading.current_thread().ident
    for tid, frame in frames():
        if tid != current_tid:
            write_sample(now, pid, tid, frame)

def main_loop():
    period = float(1) / state.options.frequency
    last_sample_time = time.time() - period
    if state.sampling:
        write_start()
    while True:
        if state.sampling:
            timeout = period
        else:
            timeout = None

        ready = select.select([state.pipe[0]], [], [], timeout)
        if ready[0]:
            msg = os.read(state.pipe[0], 1)
            if msg in (START_MSG, TOGGLE_MSG) and not state.sampling:
                write_start()
                state.sampling = True
            elif msg in (STOP_MSG, TOGGLE_MSG) and state.sampling:
                write_stop()
                state.sampling = False
            else:
                raise Exception('Unknown message %r' % msg)

        if not state.sampling:
            continue

        time_since_last_sample = time.time() - last_sample_time
        if time_since_last_sample >= period:
            collect_sample()
            last_sample_time = time.time()
            timeout = period
        else:
            timeout = period - time_since_last_sample

orig_os_fork = os.fork
def fork():
    r, w = os.pipe()
    pid = orig_os_fork()
    if pid == 0:
        os.close(r)
        if threading.current_thread() == state.thread:
            # Forking from our own thread. We could handle this enough to let an
            # exec() happen before returning to the sampling loop.
            raise NotImplementedError('fork() in sampling loop')
        else:
            if state.options.follow_fork and state.thread is not None:
                options = state.options
                options.autostart = state.sampling
                options.out_fd = reopen(options.out_fd,
                                        os.O_WRONLY | os.O_APPEND)
                os.close(state.pipe[0])
                os.close(state.pipe[1])
                state.reset()
                setup(options)
        os.write(w, 'a')
        os.close(w)
    else:
        os.close(w)
        os.read(r, 1)
        os.close(r)
    return pid

def setup(options):
    if state.thread is not None:
        raise Exception('Profiling already started')

    if options.sample_greenlets:
        hijack_greenlet()

    os.fork = fork
    state.options = options
    state.pipe = os.pipe()
    set_cloexec(state.pipe[0])
    set_cloexec(state.pipe[1])

    state.sampling = options.autostart

    def setup_handler(signo, handler):
        if signo is not None:
            x = signal.signal(signo, lambda signo, frame: handler())

    if options.start_signal == options.stop_signal:
        setup_handler(options.start_signal, toggle)
    else:
        setup_handler(options.start_signal, start)
        setup_handler(options.stop_signal, stop)

    # Start thread after signal handlers are setup so tests can safely send
    # signals as soon as the first event is emitted.
    state.thread = threading.Thread(target=main_loop, name='wcp')
    state.thread.daemon = True
    state.thread.start()

START_MSG = 's'
STOP_MSG = 'S'
TOGGLE_MSG = 't'
DETACH_MSG = 'd'

def start():
    os.write(state.pipe[1], START_MSG)

def stop():
    os.write(state.pipe[1], STOP_MSG)

def toggle():
    os.write(state.pipe[1], TOGGLE_MSG)
