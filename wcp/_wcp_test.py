import thread
import threading
import pytest

from . import _wcp

def test_fault_handling():
    _wcp.test_fault_handling()
    _wcp.test_fault_handling()

def test_fault_handling_threads():
    threads = []
    for i in range(10):
        t = threading.Thread(target=_wcp.test_fault_handling)
        threads.append(t)
        t.start()
    for t in threads:
        t.join()

def test_set_get_log_level():
    assert _wcp.get_log_level() == _wcp.ERROR

    pytest.raises(TypeError, _wcp.set_log_level, 'x')
    pytest.raises(ValueError, _wcp.set_log_level, _wcp.DEBUG + 1)

    _wcp.set_log_level(_wcp.DEBUG)
    assert _wcp.get_log_level() == _wcp.DEBUG

    _wcp.set_log_level(_wcp.ERROR)
    assert _wcp.get_log_level() == _wcp.ERROR

def test_set_get_log_fd():
    assert _wcp.get_log_fd() == 2
    _wcp.set_log_fd(2)
    pytest.raises(TypeError, _wcp.set_log_fd, 'x')

def test_get_thread_id():
    assert thread.get_ident() == _wcp.get_thread_id()

    ids = []
    def main():
        ids.append(thread.get_ident())
        ids.append(_wcp.get_thread_id())
    t = threading.Thread(target=main)
    t.start()
    t.join()
    assert ids[0] != thread.get_ident()
    assert ids[0] == ids[1]
