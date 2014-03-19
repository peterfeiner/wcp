#include <Python.h>
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

PyMODINIT_FUNC
init_prof(void)
{
    (void) Py_InitModule("_prof", ProfMethods);
}
