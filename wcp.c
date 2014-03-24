#include <Python.h>
#include <pythread.h>
#ifndef _GNU_SOURCE
#   define _GNU_SOURCE
#endif
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <syscall.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#if 0
static int
do_log(const char *fmt, ...)
{
    int r;
    va_list ap;
    va_start(ap, fmt);
    r = vdprintf(2, fmt, ap);
    va_end(ap);
    return r;
}

static pid_t
gettid(void)
{
    return syscall(SYS_gettid);
}

#define LOG(fmt, args...) ({\
    struct timespec tp;\
    clock_gettime(CLOCK_REALTIME, &tp);\
    do_log("%lu.%09lu [%d:%d]: " fmt "\n",\
           tp.tv_sec, tp.tv_nsec,\
           getpid(), gettid(),\
           ## args);\
})


static inline PyThreadState *
get_tstate(void)
{
#ifdef WITH_THREAD
    /* PyThreadState_Get() doesn't give the state of the current thread if
       the thread doesn't hold the GIL. Read the thread local storage (TLS)
       instead: call PyGILState_GetThisThreadState(). */
    return PyGILState_GetThisThreadState();
#else
    return PyThreadState_Get();
#endif
}

const char*
_Py_DumpTracebackThreads(int fd,
                         PyInterpreterState *interp,
                         PyThreadState *current_thread)
{
    PyThreadState *tstate;
    unsigned int nthreads;

    /* Get the current interpreter from the current thread */
    tstate = PyInterpreterState_ThreadHead(interp);
    if (tstate == NULL)
        return "unable to get the thread head state";

    /* Dump the traceback of each thread */
    tstate = PyInterpreterState_ThreadHead(interp);
    nthreads = 0;
    do
    {
        if (nthreads != 0)
            dprintf(fd, "\n");
/*
        if (nthreads >= MAX_NTHREADS) {
            dprintf(fd, "...\n");
            break;
        }
*/
        dprintf(fd, "%lu\n", tstate->thread_id);
        //dump_traceback(fd, tstate, 0);
        tstate = PyThreadState_Next(tstate);
        nthreads++;
    } while (tstate != NULL);

    return NULL;
}

static void
dump_threads(void)
{
    const char *msg;
    PyThreadState *tstate = get_tstate();
    if (!tstate) {
        LOG("can't get thread state");
        return;
    }
    msg = _Py_DumpTracebackThreads(2, tstate->interp, tstate);
    if (msg)
        LOG("%s", msg);
}

#define GETREG(ucontext, name) ((ucontext)->uc_mcontext.gregs[REG_ ## name])

static void
handle(int sig, siginfo_t *info, void *context_void)
{
    int r;
    struct ucontext *ucontext = context_void;
    unsigned char *pc = (unsigned char *) GETREG(ucontext, RIP);
    if (pc[0] == 0x0f && pc[1] == 0x05) {
        unsigned long syscall_number = GETREG(ucontext, RAX);
        switch(syscall_number) {
            case __NR_read: {
                int n = 1024;
                char path[n];
                int fd = GETREG(ucontext, RDI);
                r = snprintf(path, n, "/proc/self/fd/%d", fd);
                if (r == -1 || r == n)
                    goto unknown_path;
                r = readlink(path, path, n - 1);
                if (r == -1 || r == n - 1)
                    goto unknown_path;
                path[r] = '\0';
                goto good_path;
    unknown_path:
                strncpy(path, "<unknown>", n);
    good_path:
                LOG("interrupted read on fd %d (%s)", fd, path);
                break;
            }
            default: {
                LOG("interrupted syscall no. %lu", syscall_number);
            }
        }
    } else {
        LOG("interrupted pc %p", pc);
    }
    LOG("dumping threads");
    dump_threads();
    //LOG("%s %p %lu %lu [%02x %02x %02x]", strsignal(sig), pc, syscall_number, __NR_wait4, pc[0], pc[1], pc[2]);
}

static PyObject *
prof_start(PyObject *self, PyObject *args)
{
    struct sigaction act;
    struct itimerval timer;
    long sec, usec;

    if (!PyArg_ParseTuple(args, "ll", &sec, &usec))
        return NULL;

    timer.it_value.tv_sec = sec;
    timer.it_value.tv_usec = usec;
    timer.it_interval = timer.it_value;

    act.sa_sigaction = handle;
    act.sa_flags = SA_SIGINFO | SA_RESTART;
    if (sigemptyset(&act.sa_mask))
        return PyErr_SetFromErrno(PyExc_OSError);

    if (sigaddset(&act.sa_mask, SIGALRM))
        return PyErr_SetFromErrno(PyExc_OSError);

    if (sigaction(SIGALRM, &act, NULL))
        return PyErr_SetFromErrno(PyExc_OSError);

    if (setitimer(ITIMER_REAL, &timer, NULL))
        return PyErr_SetFromErrno(PyExc_OSError);

    Py_RETURN_NONE;
}

static PyObject *
prof_stop(PyObject *self, PyObject *args)
{
    struct itimerval timer;

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_interval = timer.it_value;

    if (setitimer(ITIMER_REAL, &timer, NULL))
        return PyErr_SetFromErrno(PyExc_OSError);

    if (signal(SIGALRM, SIG_DFL) == SIG_ERR)
        return PyErr_SetFromErrno(PyExc_OSError);

    Py_RETURN_NONE;
}

static PyMethodDef ProfMethods[] = {
    {"start", prof_start, METH_VARARGS, "Start profiling."},
    {"stop", prof_stop, METH_VARARGS, "Stop profiling."},
    {NULL, NULL, 0, NULL}
};

#endif

static void wcp_unregister_signal_handler(int sig);

#define WCP_DEBUG 2
#define WCP_INFO 1
#define WCP_ERROR 0

static int wcp_log_fd = 2;
static int wcp_log_level = WCP_ERROR;

static pid_t
gettid(void) {
    return syscall(SYS_gettid);
}

static int
wcp_log_printf(const char *fmt, ...) {
    int r;
    const int n = 1024;
    char buf[n];
    va_list ap;
    va_start(ap, fmt);
    r = vsnprintf(buf, n, fmt, ap);
    if (r == n)
        strcpy(&buf[n - 6], "...\n");
    r = write(wcp_log_fd, buf, strlen(buf));
    va_end(ap);
    return r;
}

#define WCP_LOG(level, fmt, args...) do {\
    if ((level) <= wcp_log_level) {\
        const char *level_name;\
        switch (level) {\
            case WCP_DEBUG: level_name = "debug"; break;\
            case WCP_INFO: level_name = "info "; break;\
            case WCP_ERROR: level_name = "error"; break;\
            default: level_name = "?    ";\
        }\
        wcp_log_printf("wcp %s [%d:%d] at " __FILE__ ":%d in %s: " fmt "\n",\
                       level_name, getpid(), gettid(), __LINE__, __FUNCTION__, \
                       ## args);\
    }\
} while (0)

#define WCP_ABORT(fmt, args...) do {\
    WCP_LOG(WCP_ERROR, fmt, ## args);\
    abort();\
} while (0)

#define WCP_ASSERT(expr) do {\
    if (!(expr))\
        WCP_ABORT("assertion failed: assert(" #expr ")");\
} while (0)

/* From getdents(2). */
struct linux_dirent {
   unsigned long  d_ino;     /* Inode number */
   unsigned long  d_off;     /* Offset to next linux_dirent */
   unsigned short d_reclen;  /* Length of this linux_dirent */
   char           d_name[];  /* Filename (null-terminated) */
                             /* length is actually (d_reclen - 2 -
                                offsetof(struct linux_dirent, d_name) */
   /*
   char           pad;       // Zero padding byte
   char           d_type;    // File type (only since Linux 2.6.4;
                             // offset is (d_reclen - 1))
   */
};

/* Async-signal safe. Counts the number of files in /proc/self/task/. */
static int
process_thread_count(void)
{
    int fd;
    int count;
    char buf[1024];

    fd = open("/proc/self/task", O_RDONLY | O_DIRECTORY);
    if (fd == -1)
        WCP_ABORT("open /proc/self/task: %r");

    count = 0;
    for (;;) {
        /* Count the files in /proc/self/task. Can't use opendir / readdir_r
         * because they may not be async-signal safe; examination of eglibc 2.15
         * shows locks. */
        int pos;
        int r = syscall(SYS_getdents, fd, buf, sizeof(buf));
        if (r == -1)
            WCP_ABORT("getdents on /proc/self/task: %r");
        if (r == 0) {
            close(fd);
            /* At least one for each of ".", "..", and this thread. */
            WCP_ASSERT(count >= 3);
            return count - 2;
        }
        for (pos = 0; pos < r; ) {
            struct linux_dirent *dirent = (struct linux_dirent *) &buf[pos];
            WCP_ASSERT((char *) &dirent->d_reclen < buf + r);
            WCP_ASSERT(dirent->d_reclen + pos <= r);
            pos += dirent->d_reclen;
            count += 1;
        }
    }
}

#define WCP_MAX_TRY_DEPTH 5

struct wcp_tls {
    PyThreadState *tstate;
    int try_depth;
    sigjmp_buf try_bufs[WCP_MAX_TRY_DEPTH];
};

/* Our thread local storage. Used to cache lookups of PyThreadState objects.
 *
 * To make our TLS async-signal safe, we have to be very careful about
 * allocating it. The pthread interface is out of the question because
 * pthread_setspecific calls calloc on each thread's first access. Using
 * __thread works, but there's a tricky detail.
 *
 * For __thread variables in the executable and .so files loaded by the loader,
 * variables is allocated at load time (i.e., when the program starts). However,
 * storage for __thread variables in dynamically loaded .so files, such as
 * imported python modules, is lazily malloc()ated when each thread first
 * accesses the variable... unless the variable is declared with the
 * tls_model("initial-exec") attribute. This attribute isn't used in general
 * because lazy allocation is generally desirable [1] and there are a limited
 * number of inital-exec __thread variables permitted per process.
 *
 * [1] See http://www.akkadia.org/drepper/tls.pdf for a comprehensive treatment
 *     of how __thread is implemented.
 *
 * [2] See https://sourceware.org/ml/libc-alpha/2012-06/msg00365.html for a
 *     discussion of accessing thread local storage from signal handlers.
 */
static __thread __attribute__((tls_model ("initial-exec")))
    struct wcp_tls wcp_current;

static void
wcp_disable_signals(sigset_t *oldset) {
    sigset_t full;

    if (sigfillset(&full))
        WCP_ABORT("sigfillset: %r");

    if (pthread_sigmask(SIG_SETMASK, &full, oldset))
        WCP_ABORT("pthread_sigmask: %r");
}

static void
wcp_restore_signals(sigset_t *oldset) {
    if (pthread_sigmask(SIG_SETMASK, oldset, NULL))
        WCP_ABORT("pthread_sigmask: %r");
}

#define WCP_TRY_EXCEPT(try_expr, except_expr) ({\
    typeof(try_expr) r;\
    int old_try_depth;\
    int jumped;\
    sigset_t oldset;\
    wcp_disable_signals(&oldset);\
    WCP_ASSERT(wcp_current.try_depth < WCP_MAX_TRY_DEPTH);\
    jumped = sigsetjmp(wcp_current.try_bufs[wcp_current.try_depth++], 1);\
    old_try_depth = wcp_current.try_depth;\
    wcp_restore_signals(&oldset);\
    if (jumped)\
        r = (except_expr);\
    else\
        r = (try_expr);\
    wcp_disable_signals(NULL);\
    WCP_ASSERT(wcp_current.try_depth == old_try_depth);\
    wcp_current.try_depth -= 1;\
    wcp_restore_signals(&oldset);\
    r;\
})

/* Async-signal safe. Best effort attempt to find the current thread's Python
 * thread state. Tries to find this thread by traversing Python's linked lists
 * of interpreters and threads. The traversal races with thread and interpreter
 * creation, thus the "best effort".
 *
 * Deleterious side effects of the racy traversal are avoided by handling
 * SIGSEGV and limiting the traversal to process_thread_count() iterations.
 *
 * Returns NULL if this thread can't be found (i.e., a race with thread creation
 * or the current thread isn't a Python thread). TODO: It would be nice to be
 * able to definitively determine if a thread isn't a Python thread so we
 * wouldn't have to call find_current_pythread_state on every interrupt.
 *
 * Note that we can't simply call PyGILState_GetThisThreadState because it's not
 * async-signal safe. Why isn't it? Because it calls malloc and grabs keymutex
 * in Python/thread.c. */
static PyThreadState *
wcp_find_current_tstate(void)
{
    /* PyThread_get_thread_ident accesses thread local storage via a call to
     * pthread_self. Although TLS accesses aren't inheriently async-signal safe,
     * pthread_self's TLS access is. Since pthread_self uses pthread_getspecific
     * and pthread_getspecifc just reads data (unlike pthread_setspecific, which
     * calls calloc when a key is first accessed by a thread), we're in the
     * clear. */
    long current_thread_id = PyThread_get_thread_ident();
    int thread_count = process_thread_count();
    int threads_seen = 0;
    PyInterpreterState *interp = PyInterpreterState_Head();

    WCP_LOG(WCP_DEBUG, "currently %d threads", thread_count);

    for (; interp; interp = PyInterpreterState_Next(interp))
    {
        PyThreadState *tstate = PyInterpreterState_ThreadHead(interp);
        for (; tstate; tstate = PyThreadState_Next(tstate))
        {
            if (threads_seen == thread_count) {
                return NULL;
            }

            if (tstate->thread_id == current_thread_id)
                return tstate;

            threads_seen += 1;
        }
    }

    return NULL;
}

static PyThreadState *
wcp_current_tstate(void)
{
    if (wcp_current.tstate == NULL)
        wcp_current.tstate = WCP_TRY_EXCEPT(wcp_find_current_tstate(), NULL);
    return wcp_current.tstate;
}

static PyObject *
wcp_get_thread_id(PyObject *self, PyObject *args)
{
    PyThreadState *tstate;

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    tstate = wcp_current_tstate();
    if (tstate == NULL) {
        /* Should never fail because we hold the GIL and no new threads are
         * being created. */
        PyErr_SetString(PyExc_Exception, "wcp_current_tstate failed");
        return NULL;
    }

    return PyLong_FromLong(tstate->thread_id);
}

static PyObject *
wcp_raise_os_error(const char *fmt, ...)
{
    char *msg;
    PyObject *v;
    int r;
    int i = errno;

    va_list ap;
    va_start(ap, fmt);
    r = vasprintf(&msg, fmt, ap);
    va_end(ap);

    if (r)
        return PyErr_NoMemory();

    v = Py_BuildValue("(is)", i, msg);
    free(msg);

    if (v == NULL)
        return PyErr_NoMemory();

    PyErr_SetObject(PyExc_OSError, v);
    Py_DECREF(v);

    return NULL;
}

static void
wcp_invoke_default_handler(int sig)
{
    sigset_t mask;
    wcp_unregister_signal_handler(sig);
    sigfillset(&mask);
    sigdelset(&mask, sig);
    raise(sig);
    sigsuspend(&mask);
};

static void
wcp_handle_fault(int sig, siginfo_t *info, struct ucontext *ucontext)
{
    WCP_LOG(WCP_DEBUG, "handling fault %d, try_depth is %d",
            sig, wcp_current.try_depth);
    WCP_ASSERT(sig == SIGSEGV || sig == SIGBUS);
    if (wcp_current.try_depth == 0) {
        WCP_LOG(WCP_ERROR, "received signal %d when try_depth is 0; aborting",
                sig);
        wcp_invoke_default_handler(sig);
        WCP_ABORT("default signal %d handler should have killed me ...", sig);
    }
    siglongjmp(wcp_current.try_bufs[wcp_current.try_depth - 1], 1);
}

static void
wcp_handle_sample(int sig, siginfo_t *info, struct ucontext *ucontext)
{
    PyThreadState *tstate = wcp_current_tstate();
    WCP_LOG(WCP_DEBUG, "SIGPROF - tstate is %p", tstate);
}

static void
wcp_handle_signal(int sig, siginfo_t *info, void *context_void)
{
    struct ucontext *ucontext = context_void;
    switch (sig) {
        case SIGBUS:
        case SIGSEGV:
            wcp_handle_fault(sig, info, ucontext);
            break;
        case SIGPROF:
            wcp_handle_sample(sig, info, ucontext);
            break;
        default:
            WCP_ABORT("unexpected signal: %d", sig);
    }
}

static void
wcp_register_signal_handler(int sig, int sa_flags)
{
    struct sigaction act;
    act.sa_sigaction = wcp_handle_signal;
    act.sa_flags = sa_flags | SA_SIGINFO;

    if (sigemptyset(&act.sa_mask))
        wcp_raise_os_error("sigemptyset: %r");

    if (sigaddset(&act.sa_mask, SIGPROF))
        wcp_raise_os_error("sigaddset: %r");

    if (sigaction(sig, &act, NULL))
        wcp_raise_os_error("sigaction: %r");
}

static void
wcp_unregister_signal_handler(int sig)
{
    if (signal(sig, SIG_DFL) == SIG_ERR)
        WCP_ABORT("signal: %r");
}

static PyObject *
wcp_setup(PyObject *self, PyObject *args)
{
    struct itimerval timer;
    long sec, usec;

    if (!PyArg_ParseTuple(args, "ll", &sec, &usec))
        return NULL;

    timer.it_value.tv_sec = sec;
    timer.it_value.tv_usec = usec;
    timer.it_interval = timer.it_value;

    wcp_register_signal_handler(SIGPROF, SA_RESTART);
    if (PyErr_Occurred())
        return NULL;

    if (setitimer(ITIMER_PROF, &timer, NULL))
        return wcp_raise_os_error("setitimer: %r");

    Py_RETURN_NONE;
}

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

static char
wcp_read(const char *c)
{
    return ACCESS_ONCE(*c);
}

static char
wcp_try_read_except(const char *c, char except)
{
    return WCP_TRY_EXCEPT(wcp_read(c), except);
}


static PyObject *
wcp_test_fault_handling(PyObject *self, PyObject *args)
{
    int fd;
    char path[] = {"/tmp/XXXXXX"};
    char *m;
    char c;
    int status;
    pid_t child;

    if (!PyArg_ParseTuple(args, ""))
        return NULL;

    /* SIGSEGV */
    WCP_ASSERT(wcp_try_read_except(NULL, 'a') == 'a');

    /* SIGBUS */
    fd = mkstemp(path);
    WCP_ASSERT(fd != -1);
    WCP_ASSERT(!unlink(path));
    m = (char *) mmap(NULL, PAGE_SIZE, PROT_WRITE | PROT_READ,
                      MAP_PRIVATE, fd, 0);
    WCP_ASSERT(m != MAP_FAILED);
    WCP_ASSERT(wcp_try_read_except(m, 'a') == 'a');
    WCP_ASSERT(!munmap(m, PAGE_SIZE));
    WCP_ASSERT(!close(fd));

    /* Nested. */
    c = WCP_TRY_EXCEPT(wcp_try_read_except(NULL, 'a'), 'b');
    WCP_ASSERT(c == 'a');

    /* Fault in except expression. */
    c = WCP_TRY_EXCEPT(wcp_read(NULL), wcp_try_read_except(NULL, 'a'));
    WCP_ASSERT(c == 'a');

    /* Make sure we die if we're not expecting a fault. */
    child = fork();
    if (child == 0) {
        wcp_read(NULL);
        exit(0);
    }
    WCP_ASSERT(child != -1);
    WCP_ASSERT(waitpid(child, &status, 0) == child);
    WCP_ASSERT(WIFSIGNALED(status));
    WCP_ASSERT(WTERMSIG(status) == SIGSEGV);
    WCP_LOG(WCP_INFO, "^^^ good, child %d killed by signal %d :-)", child, SIGSEGV);

    Py_RETURN_NONE;
}

static PyObject *
wcp_set_log_level(PyObject *self, PyObject *args)
{
    int level;
    if (!PyArg_ParseTuple(args, "i", &level))
        return NULL;

    if (level < WCP_ERROR || level > WCP_DEBUG)
        return PyErr_Format(PyExc_ValueError, "log level %d out of bounds",
                            level);

    wcp_log_level = level;

    Py_RETURN_NONE;
}

static PyObject *
wcp_get_log_level(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;
    return PyInt_FromLong(wcp_log_level);
}

static PyObject *
wcp_set_log_fd(PyObject *self, PyObject *args)
{
    long fd;
    if (!PyArg_ParseTuple(args, "l", &fd))
        return NULL;

    wcp_log_fd = fd;

    Py_RETURN_NONE;
}

static PyObject *
wcp_get_log_fd(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;
    return PyInt_FromLong(wcp_log_fd);
}

static PyMethodDef ProfMethods[] = {
/*
    {"start", prof_start, METH_VARARGS, "Start profiling."},
    {"stop", prof_stop, METH_VARARGS, "Stop profiling."},
    */
    {"setup", wcp_setup, METH_VARARGS, "Setup profiling."},
    {"get_thread_id", wcp_get_thread_id, METH_VARARGS, "Get current thread id."},
    {"test_fault_handling", wcp_test_fault_handling, METH_VARARGS, ""},
    {"get_log_level", wcp_get_log_level, METH_VARARGS, ""},
    {"set_log_level", wcp_set_log_level, METH_VARARGS, ""},
    {"set_log_fd", wcp_set_log_fd, METH_VARARGS, ""},
    {"get_log_fd", wcp_get_log_fd, METH_VARARGS, ""},
    {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC
init_wcp(void)
{
    PyObject *self;
    PyObject *v;
    int r;

    wcp_register_signal_handler(SIGSEGV, 0);
    if (PyErr_Occurred())
        goto error;

    wcp_register_signal_handler(SIGBUS, 0);
    if (PyErr_Occurred())
        goto error_sigsegv;

    self = Py_InitModule("_wcp", ProfMethods);
    if (!self)
        goto error_sigbus;

#define EXPORT_LOG_LEVEL(level)\
    v = PyInt_FromLong(WCP_ ##level);\
    if (!v)\
        goto error_sigbus;\
    r = PyObject_SetAttrString(self, #level, v);\
    Py_DecRef(v);\
    if (r == -1)\
        goto error_sigbus;
    EXPORT_LOG_LEVEL(ERROR)
    EXPORT_LOG_LEVEL(INFO)
    EXPORT_LOG_LEVEL(DEBUG)
#undef EXPORT_LOG_LEVEL

out:
    return;
error_sigbus:
    wcp_unregister_signal_handler(SIGBUS);
error_sigsegv:
    wcp_unregister_signal_handler(SIGSEGV);
error:
    goto out;
}
