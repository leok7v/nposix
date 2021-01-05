/* Copyright (c) Dmitry "Leo" Kuznetsov 2020 see LICENSE for details */

#include "nposix.h"
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/stat.h>

// just in case this code is compiled by C++ (e.g. cl.exe of msvc)
// to prevent name mangling surround it with begin_c end_c brackets

begin_c

static int_t str_length(const char* s) { return strlen(s); }

static bool str_equal(const char* s1, const char* s2, int_t bytes) {
    return s1 == s2 || bytes > 0 ? strncmp(s1, s2, bytes) == 0 : strcmp(s1, s2) == 0;
}

static double str_to_double(const char* s, int bytes) {
    assertion(0 <= bytes && bytes < 32, "invalid bytes=%d" , bytes);
    if (bytes > 0) {
        char format[16]; // because "v" is not zero terminated need to limit how many characters sscanf can read
        snprintf(format, countof(format), "%c%dlg", '%', (int)bytes);
        errno = 0;
        double d = 0;
        return sscanf(s, format, &d) == 1 ? d : nan("");
    } else {
        char* p = (char*)s;
        double d = strtod(s, &p);
        return p == s ? nan("") : d;
    }
}

static bool str_starts_with(const char* s1, const char* s2) {
    return strstr(s1, s2) == s1;
}

static bool str_contains(const char* s1, const char* s2) {
    return strstr(s1, s2) != null;
}

str_if str = {
    .length = str_length,
    .equal = str_equal,
    .to_double = str_to_double,
    .starts_with = str_starts_with,
    .contains = str_contains
};

enum { RANDOM_MAX = (int)((1UL << 31) - 1) };

static double random_generator_as_int32(void) {
    return (int32_t)random();
}

static double random_generator_as_double(void) {
    // posix verbaly defines random() function range as [0 .. (2^31) - 1]
    return random_generator_as_int32() / (double)RANDOM_MAX; // in [0.0 .. 1.0]
}

//  void (*seed)(unsigned int);
//  char* (*init_state)(unsigned seed, char *state, size_t n);
//  char* (*set_state)(char *state);

random_generator_if random_generator = {
    RANDOM_MAX,
    .as_int32 = random_generator_as_int32,
    .as_double = random_generator_as_double,
    .seed = srandom,
    .init_state = initstate,
    .set_state = setstate
};

static double time_since_epoch(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint_fast64_t ns = ts.tv_sec * (uint_fast64_t)NSEC_PER_SEC + ts.tv_nsec;
    return ns / (double)NSEC_PER_SEC;
}

static double time_in_seconds(void) {
    struct timespec ts = {};
#ifdef __APPLE__
    static clockid_t process_clock_id = CLOCK_PROCESS_CPUTIME_ID;
#else
    static clockid_t process_clock_id;
    if (process_clock_id == 0) {
        if_error_fatal(clock_getcpuclockid(0, &process_clock_id));
    }
#endif
    if_error_fatal(clock_gettime(process_clock_id, &ts));
    uint_fast64_t ns = ts.tv_sec * (uint_fast64_t)NSEC_PER_SEC + ts.tv_nsec;

    // convinience - easy on the eye to see number of seconds since first call not since boot
    static atomic_uint_fast64_t first;
    uint_fast64_t zero = 0;
    atomic_compare_exchange_strong(&first, &zero, ns); // prevent negative seconds()
    if (first == 0) { traceln("WARNING: atomic_compare_exchange_strong() failed?"); first = ns; }
    return ns / (double)NSEC_PER_SEC; // nanoseconds to seconds
}

process_clock_if process_clock = {
    .nsec_per_sec = 1000000000,
    .usec_per_sec = 1000000,
    .msec_per_sec = 1000,
    .time_since_epoch = time_since_epoch,
    .time = time_in_seconds
};

static void mutex_init(mutex_t* m) {
    if_error_fatal(pthread_mutex_init(m, null));
}

static void mutex_lock(mutex_t* m) {
    if_error_fatal(pthread_mutex_lock(m));
}

static int mutex_try_lock(mutex_t* m) {
    int mutex_trylock = pthread_mutex_trylock(m);
    if (mutex_trylock != EBUSY) {
        if_error_fatal(mutex_trylock);
    }
    return mutex_trylock;
}

static void mutex_unlock(mutex_t* m) {
    if_error_fatal(pthread_mutex_unlock(m));
}

static void mutex_dispose(mutex_t* m) {
    if_error_fatal(pthread_mutex_destroy(m));
}

mutex_if mutex = {
    .init = mutex_init,
    .lock = mutex_lock,
    .try_lock = mutex_try_lock,
    .unlock = mutex_unlock,
    .dispose = mutex_dispose
};

static void event_init(event_t* e) {
    if_error_fatal(pthread_cond_init(e, null));
}

static void event_signal(event_t* e) {
    if_error_fatal(pthread_cond_signal(e));
}

static void event_wait(event_t* e, mutex_t* m) {
    if_error_fatal(pthread_cond_wait(e, m));
}

static int event_timed_wait(event_t* e, mutex_t* m, double seconds) {
    // Nothing is ever easy with posix.
    // pthread_cond_timedwait() uses *unspecified* abstime
    // which is a guess work for each OS
    // see: https://github.com/apple/darwin-libpthread/blob/master/tests/cond_timed.c
    struct timespec now = {};
    if_error_fatal(clock_gettime(CLOCK_REALTIME, &now)); // since epoch
    uint64_t now_ns = now.tv_sec * process_clock.nsec_per_sec + now.tv_nsec;
    uint64_t wait_ns =seconds * process_clock.nsec_per_sec;
    uint64_t abs_ns = now_ns + wait_ns;
    struct timespec ts = {};
    ts.tv_sec = (time_t)(abs_ns / process_clock.nsec_per_sec);
    ts.tv_nsec = (long)(abs_ns % process_clock.nsec_per_sec);
    double time = process_clock.time();
    int timedwait_result = pthread_cond_timedwait(e, m, &ts);
    time = process_clock.time() - time;
//  traceln("%.6f wait %.3fs actual %.3fs", np.seconds(), seconds, np.seconds() - time);
    if (timedwait_result != 0 && timedwait_result != ETIMEDOUT) {
        if_error_fatal(timedwait_result);
    }
    // CLOCK_REALTIME may be adjusted on the fly by (e.g. NTP) which may
    // result in spurious early wake on (e.g. at Day Time Savings adjustment)
    if (timedwait_result == ETIMEDOUT && time < seconds) {
        traceln("WARNING: spurious early wake %.3fs while waiting for %.3fs",
                time, seconds);
    }
    return timedwait_result;
}

static void event_dispose(event_t* e) {
    if_error_fatal(pthread_cond_destroy(e));
}

event_if event = {
    .init = event_init,
    .signal = event_signal,
    .wait = event_wait,
    .timed_wait = event_timed_wait,
    .dispose = event_dispose
};

static void thread_sleep(double secs) {
    double deadline = process_clock.time() + secs;
    for (;;) {
        double s = deadline - process_clock.time();
        if (s <= 0) { break; }
        struct timespec req = { (time_t)s,
            (long)(((int64_t)(s * (double)NSEC_PER_SEC)) % NSEC_PER_SEC) };
        nanosleep(&req, null); // can be interrupted
    }
}

static void thread_start(thread_t* t, void (*function)(void*), void* p,
                          int_t stack_size, bool detached) {
    void* (*f)(void*) = (void* (*)(void*))function;
    pthread_attr_t a;
    if_error_fatal(pthread_attr_init(&a));
    if (stack_size > 0) {
        if_error_fatal(pthread_attr_setstacksize(&a, stack_size));
    }
    if (detached > 0) {
        if_error_fatal(pthread_attr_setdetachstate(&a, 1));
    }
    if_error_fatal(pthread_create(t, &a, f, p));
    if_error_fatal(pthread_attr_destroy(&a));
}

void thread_join(thread_t t) { if_error_fatal(pthread_join(t, null)); }

threads_if threads = {
    thread_start,
    thread_join,
    thread_sleep
};

static void memmap_file(const char* filename, void* *data, int_t *bytes, int* r, bool readonly) {
    assertion(*data == null && *bytes == 0 && *r == 0, "invalid (uninitialized or reused) parameters");
    if_error_return(*data != null || *bytes != 0, { *r = EINVAL; }, {});
    int f = open(filename, O_RDONLY);
    if_error_return(f < 0, { *r = errno; }, {});
    struct stat s = {};
    if_error_return(fstat(f, &s) < 0, { *r = errno; }, { close(f); });
    if_error_return(s.st_size == 0 || s.st_size > (1ULL << ((sizeof(int) * 8) - 1)), { *r = E2BIG; }, { close(f); });
    *bytes = (int)s.st_size;
    *data = mmap(null, *bytes, PROT_READ, MAP_PRIVATE, f, 0);
    if_error_return(*data == null, { *r = errno; }, { close(f); });
    *r = close(f);
    assertion(*r == 0, "posix r/t failed to close successfully opened file something is seriously wrong");
}

static int memmap_file_readonly(const char* filename, void* *data, int_t *bytes) {
    int r = 0;
    memmap_file(filename, data, bytes, &r, true);
    return r;
}

static int memmap_file_readwrite(const char* filename, void* *data, int_t *bytes) {
    int r = 0;
    memmap_file(filename, data, bytes, &r, false);
    return r;
}

static int memmap_file_unmap(void *data, int_t bytes) {
    int r = 0;
    if (data != null && bytes != 0) {
        r = munmap((void*)data, bytes);
        assertion(r == 0, "posix r/t failed to unmap successfully memory mapped region something is seriously wrong");
    } else {
        r = EINVAL;
    }
    return r;
}

memmap_if memmap = {
    .file_readonly = memmap_file_readonly,
    .file_readwrite = memmap_file_readwrite, // TODO
    .file_unmap = memmap_file_unmap,
    // TODO: the rest of it
};


#if (defined(DEBUG) || defined(_DEBUG)) && !defined(NDEBUG)
enum { is_debug_build = 1 };
#else
enum { is_debug_build = 0 };
#endif

nposix_if np = {
    (bool)is_debug_build
};

#ifndef NO_TESTS

void nposix_test(void) {
    char s[4];
    assertion(countof(s) == 4, "is countof() broken?");
    strcpy(s, "abc");
    assertion(str.equal("abc", "abc", 0), "compare to itself");
    assertion(str.equal(s,     "abc", 0), "zero terminated strings must be equal");
    assertion(str.equal("abc", s,     0), "zero terminated strings must be equal");
    s[3] = 'z'; // str[] is not zero terminated anymore
    assertion(str.equal(s,     "abc",  3), "non-zero terminated strings must be equal");
    assertion(str.equal("abc", s,      3), "non-zero terminated strings must be equal");
    assertion(str.equal(s,     "abcd", 3), "non-zero terminated strings must be equal");
    assertion(str.equal("abcd", s,     3), "non-zero terminated strings must be equal");
    assertion(!str.equal("abc", "xyz", 0), "should not be equal");
    assertion(!str.equal("abc", "xyz", 3), "compare only 3 bytes");
    // str_to_double
    char numeral[16]= {};
    // 9 bytes:      012345678
    strcpy(numeral, "123.456E0");
    numeral[9] = '2';
    assertion(str.to_double(numeral,  9) == 123.456,
              "str_todouble(%*.*s,  9)=%g",
              10, 10, numeral, str.to_double(numeral, 9));
    assertion(str.to_double(numeral, 10) == 12345.6,
              "str_todouble(%*.*s, 10)=%g",
              10, 10, numeral, str.to_double(numeral, 10));
    // fmmap & funmmap
    char filename[4096] = {};
    strcpy(filename, "testXXXXXX");
    int fd = mkstemp(filename);
    assertion(fd >= 0, "failed to create temporary file");
    if (fd >= 0) {
        int k = (int)write(fd, "abc", 3);
        assertion(k == 3, "failed to fwrite(\"abc\", 3, 1, f) written %d bytes", k);
        close(fd);
        int_t bytes = 0;
        void* data = null;
        int r = memmap.file_readonly(filename, &data, &bytes);
        assertion(r == 0 && bytes == 3 && memcmp(data, "abc", 3) == 0,
                  "expected 3 bytes in mapped \"abc\" at %p, %d", data, (int)bytes);
        memmap.file_unmap(data, bytes);
        unlink(filename);
    }
    const double time_to_wait = 0.000123 * (random_generator.as_double() + 0.1);
    double deadline = process_clock.time() + time_to_wait;
    threads.sleep(time_to_wait);
    double time = process_clock.time();
    assertion(time >= deadline, "time=%.9f deadline=%.9f", time, deadline);
}

#endif

end_c
