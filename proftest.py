import signal
import threading
import thread
import time
import math

from wcp import _wcp


_wcp.set_log_level(_wcp.DEBUG)
#_wcp.setup(1, 0)

def bar(name):
    foo(name)

def foo(name):
    numbers = range(100000000)
    while True:
        numbers.reverse()
        numbers.sort()
        print name, 'sorted'
t1 = threading.Thread(target=bar, kwargs={'name':'t1'})
t2 = threading.Thread(target=bar, kwargs={'name':'t2'})
t1.start()
t2.start()
t1.join()
t2.join()
