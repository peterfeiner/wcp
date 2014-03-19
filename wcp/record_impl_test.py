# Copyright (C) 2014  Peter Feiner

import os
import sys
import pytest
import traceback
import signal
import time

import wcp.io as io
import wcp.record as record

class Runnee(object):

    def __init__(self, options, code, read_fp):
        self.pid = os.fork()
        self.read_fp = read_fp
        self.exit_status = None
        if self.pid == 0:
            try:
                code = 'import wcp\nwcp.record_impl.setup(options)\n%s' % code
                exec(code, {'__builtins__': __builtins__}, {'options': options})
                sys.stderr.flush()
                os._exit(0)
            except:
                try:
                    traceback.print_exc()
                    for i, line in enumerate(code.split('\n')):
                        sys.stderr.write('%3d: %s\n' % (i + 1, line))
                    sys.stderr.flush()
                finally:
                    os._exit(1)

    def wait(self, exit_code=None, exit_signal=None):
        pid, self.exit_status = os.waitpid(self.pid, 0)
        assert pid == self.pid
        if exit_code is not None:
            assert os.WIFEXITED(self.exit_status)
            assert os.WEXITSTATUS(self.exit_status) == exit_code
        if exit_signal is not None:
            assert os.WIFSIGNALED(self.exit_status)
            assert os.WTERMSIG(self.exit_status) == exit_signal
        self.read_fp.close()

    def kill(self, signo):
        os.kill(self.pid, signo)

    def drain_kill_and_wait(self, signo):
        self.kill(signo)
        for event in self.read_events():
            pass
        self.wait(exit_signal=signo)

    def read_event(self, expected_type=None):
        for event in self.read_events():
            if expected_type is not None:
                assert event.event_type == expected_type
            return event
        assert expected_type is None,\
               'No events, but expected %s' % io.EVENT_NAMES[expected_type]
        return None

    def read_start_event(self):
        return self.read_event(io.START_EVENT)

    def read_sample_event(self):
        return self.read_event(io.SAMPLE_EVENT)

    def read_stop_event(self):
        return self.read_event(io.SAMPLE_EVENT)

    def read_events(self):
        for event in io.read_events(self.read_fp):
            yield event

class Runner(object):
    def __init__(self):
        self.options = record.Options()
        self.children = []

    def run(self, code):
        r, w = os.pipe()
        try:
            self.options.out_fd = w
            child = Runnee(self.options, code, os.fdopen(r, 'r'))
        finally:
            os.close(w)
        self.children.append(child)
        return child

    def cleanup_children(self):
        for child in self.children:
            if child.exit_status is None:
                child.kill(signal.SIGKILL)
                child.wait()

@pytest.fixture
def runner(request):
    r = Runner()
    request.addfinalizer(r.cleanup_children)
    return r

def test_wait_for_start(runner):
    start = time.time()
    r = runner.run('import signal\nsignal.pause()')
    e = r.read_event()
    assert e.event_type == io.START_EVENT
    assert e.pid == r.pid
    assert e.tid == 0
    assert e.time >= start
    assert e.time <= time.time()
    assert e.data is None
    r.drain_kill_and_wait(signal.SIGTERM)

def test_wait_for_sample(runner):
    r = runner.run('import signal\nsignal.pause()')
    e1 = r.read_event()
    assert e1.event_type == io.START_EVENT
    assert e1.data is None
    e2 = r.read_event()
    assert e2.event_type == io.SAMPLE_EVENT
    assert e2.pid == r.pid
    assert e2.tid != 0
    assert e2.time > e1.time
    assert isinstance(e2.data, io.SampleData)
    assert 'test_wait_for_sample' in str(e2.data.frames)
    r.drain_kill_and_wait(signal.SIGTERM)

def test_follow_fork(runner):
    pipe = os.pipe()
    r = runner.run('''\
import signal
import os
pid = os.fork()
if pid != 0:
    assert os.write(%d, '%%10d' %% pid) == 10
    os.waitpid(pid, 0)
else:
    signal.pause()''' % pipe[1])

    grandchild = int(os.read(pipe[0], 10))

    seen = set()

    for e in r.read_events():
        record = (e.pid, e.event_type)
        assert e.event_type in (io.START_EVENT, io.SAMPLE_EVENT)
        assert e.pid in (r.pid, grandchild)
        if e.event_type == io.START_EVENT:
            assert record not in seen
        seen.add(record)
        if len(seen) == 4:
            break

    os.kill(grandchild, signal.SIGTERM)
    r.wait(exit_code=0)

def test_no_follow_fork(runner):
    runner.options.follow_fork = False
    r = runner.run('''\
import os
import time
assert len(os.listdir('/proc/self/task')) == 2
pid = os.fork()
if pid == 0:
    time.sleep(0.5)
    assert len(os.listdir('/proc/self/task')) == 1
    os._exit(0)
child_pid, status = os.waitpid(pid, 0)
assert child_pid == pid
assert os.WIFEXITED(status)
assert os.WEXITSTATUS(status) == 0''')
    for e in r.read_events():
        assert e.pid == r.pid
    r.wait(exit_code=0)

def test_ignore(runner):
    def shiver_me_timbers():
        return runner.run('import signal\nsignal.pause()')

    def i_be_the_calling_function():
        return shiver_me_timbers()

    r = i_be_the_calling_function()
    r.read_start_event()
    e = r.read_event()
    assert 'shiver_me_timbers' in str(e.data.frames)
    assert 'i_be_the_calling_function' in str(e.data.frames)
    r.drain_kill_and_wait(signal.SIGTERM)

    runner.options.ignore = shiver_me_timbers.__code__
    r = runner.run('import signal\nsignal.pause()')
    r.read_start_event()
    e = r.read_event()
    assert 'shiver_me_timbers' not in str(e.data.frames)
    assert 'i_be_the_calling_function' not in str(e.data.frames)
    r.drain_kill_and_wait(signal.SIGTERM)

def test_no_autostart(runner):
    runner.options.autostart = False 
    r = runner.run('import time\ntime.sleep(1)')
    assert r.read_event() == None
    r.wait(exit_code=0)

def test_start_signal(runner):
    runner.options.autostart = False 
    runner.options.start_signal = signal.SIGUSR1
    r = runner.run('''\
import signal
while True:
    signal.pause()''')

    pre_start = time.time()
    time.sleep(1)
    r.kill(signal.SIGUSR1)
    e = r.read_start_event()
    assert e.time > pre_start
    r.read_sample_event()
    r.drain_kill_and_wait(signal.SIGTERM)

def test_stop_signal(runner):
    runner.options.stop_signal = signal.SIGUSR1
    r = runner.run('''\
import signal
while True:
    signal.pause()''')
    # Need to read start event before signalling so we know that the signal
    # handler has been installed.
    r.read_start_event()
    r.kill(signal.SIGUSR1)
    for e in r.read_events():
        if e.event_type == io.STOP_EVENT:
            break
        assert e.event_type == io.SAMPLE_EVENT
    else:
        assert False, 'No stop event'
    r.kill(signal.SIGTERM)
    # After the stop event, no more events can come.
    assert r.read_event() == None
    r.wait(exit_signal=signal.SIGTERM)

def test_toggle_signal():
    pass

def test_threads():
    pass

def test_greenlets():
    pass

def test_existing_greenlet():
    pass

def test_eventlet_import():
    pass
