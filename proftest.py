import signal
import _prof
import thread
print _prof.get_thread_id()
print thread.get_ident()
def main():
    print _prof.get_thread_id()
    print thread.get_ident()
    import os
    os._exit(0)
thread.start_new_thread(main, ())
signal.pause()
