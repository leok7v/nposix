/* Copyright (c) Dmitry "Leo" Kuznetsov 2020 see LICENSE for details */

#include "nposix.h"
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/stat.h>

// just in case this code is compiled by C++ (e.g. cl.exe of msvc)
// to prevent name mangling surround it with begin_c end_c brackets

begin_c

static void* mem_copy(void* a, const void* b, int_t bytes) {
    return memcpy(a, b, bytes);
}

static void* mem_move(void* a, const void* b, int_t bytes) {
    return memmove(a, b, bytes);
}

static void* mem_fill(void* a, uint8_t b, int_t bytes) {
    return memset(a, b, bytes);
}

static void* mem_zero(void* a, int_t bytes) {
    return memset(a, 0, bytes);
}

static int mem_compare(const void* left, const void* right, int_t bytes) {
    return memcmp(left, right, bytes);
}

/* If you never spent hours starting at missing "== 0" (which was not there
   to stare at) after memcmp() you might have been living in paradise: */

static bool mem_equal(const void* a, const void* b, int_t bytes) {
    return memcmp(a, b, bytes) == 0;
}

mem_if mem = {
    .copy = mem_copy,
    .move = mem_move,
    .fill = mem_fill,
    .zero = mem_zero,
    .compare = mem_compare,
    .equal = mem_equal
};

static int_t str_length(const char* s) { return strlen(s); }

static bool str_equal(const char* s1, const char* s2, int_t bytes) {
    return s1 == s2 || bytes > 0 ?
           strncmp(s1, s2, bytes) == 0 : strcmp(s1, s2) == 0;
}

static double str_to_double(const char* s, int bytes, int* error) {
    assertion(0 <= bytes && bytes < 64, "invalid bytes=%d" , bytes);
    if (bytes > 64) {
        *error = E2BIG;
        return nan("");
    } else if (bytes > 0) {
        char format[16];
        // because "s" is NOT zero terminated need to limit
        // how many characters sscanf can read
        snprintf(format, countof(format), "%c%dlg", '%', (int)bytes);
        double d = 0;
        errno = 0;
        int n = sscanf(s, format, &d);
        *error = errno;
        if (*error == 0 && n != 1) { *error = ERANGE; }
        return n == 1 ? d : nan("");
    } else {
        char* p = (char*)s;
        errno = 0;
        double d = strtod(s, &p);
        if (p == s) { *error = errno; }
        if (p == s && *error == 0) { *error = ERANGE; }
        return p == s ? nan("") : d;
    }
}

static void str_to_int64(int64_t* d, const char* s, int bytes, int* error) {
    *error = 0;
    assertion(0 <= bytes && bytes < 64, "invalid bytes=%d" , bytes);
    if (bytes > 64) {
        *error = E2BIG;
    } else if (bytes > 0) {
        char format[16];
        // because "s" is NOT zero terminated need to limit
        // how many characters sscanf can read
        snprintf(format, countof(format), "%c%d" SCNi64, '%', (int)bytes);
        errno = 0;
        int n = sscanf(s, format, d);
        *error = errno;
        if (*error == 0 && n != 1) { *error = ERANGE; }
    } else {
        errno = 0;
        char* p = (char*)s;
        *d = (int64_t)strtoll(s, &p, 0);
        if (p == s) { *error = errno; }
        if (p == s && *error == 0) { *error = ERANGE; }
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
    .to_int64 = str_to_int64,
    .starts_with = str_starts_with,
    .contains = str_contains
};

typedef struct random_48bit_seed_s {
    uint16_t s0;
    uint16_t s1;
    uint16_t s2;
} random_48bit_seed_t;

static random_48bit_seed_t random_48bit(uint64_t *seed) {
    random_48bit_seed_t x = {
        .s0 = (uint16_t)(*seed >> 00),
        .s1 = (uint16_t)(*seed >> 16),
        .s2 = (uint16_t)(*seed >> 32),
    };
    uint16_t m0 = (uint16_t)(random_generator.mult >> 00);
    uint16_t m1 = (uint16_t)(random_generator.mult >> 16);
    uint16_t m2 = (uint16_t)(random_generator.mult >> 32);
    const uint32_t add = (uint32_t)random_generator.add;
    uint32_t accu = (uint32_t)m0 * (uint32_t)x.s0 + add;
    uint16_t t0 = (uint16_t)accu; /* lower 16 bits */
    accu >>= sizeof(uint16_t) * 8;
    accu += (uint32_t)m0 * x.s1 + (uint32_t)m1 * (uint32_t)x.s0;
    uint16_t t1 = (uint16_t)accu; /* middle 16 bits */
    accu >>= sizeof(uint16_t) * 8;
    accu += m0 * x.s2 + m1 * x.s1 + m2 * x.s0;
    x.s0 = t0;
    x.s1 = t1;
    x.s2 = (uint16_t)accu;
    *seed = (uint64_t)t0 | (((uint32_t)t1) << 16) | (((uint64_t)accu) << 32);
    return x;
}

static int32_t random_next_seeded_uint32(uint64_t *seed) { // aka nrand48()
    random_48bit_seed_t x = random_48bit(seed);
    return ((int32_t)x.s2 << 15) + ((int32_t)x.s1 >> 1);
}

static int32_t random_next_seeded_int32(uint64_t *seed) { // aka jrand48()
    random_48bit_seed_t x = random_48bit(seed);
    return ((int32_t)x.s2 << 16) + (int32_t)x.s1;
}

static double random_next_seeded_double(uint64_t *seed) { // aka erand48()
    random_48bit_seed_t x = random_48bit(seed);
    return ldexp((double)x.s0, -48) + ldexp((double)x.s1, -32) +
           ldexp((double)x.s2, -16);
}

static int32_t random_next_uint32(void) {
    return random_next_seeded_uint32(&random_generator.seed);
}

static int32_t random_next_int32(void) {
    return random_next_seeded_int32(&random_generator.seed);
}

static double random_next_double(void) { // aka drand48()
    return random_next_seeded_double(&random_generator.seed);
}

random_generator_if random_generator = {
    .initial_seed = 0x1234ABCD330E,
    .seed = 0x1234ABCD330E, // { 330E ABCD 1234 }
    .mult = 0x0005DEECE66D, // { E66D DEEC 0005 }
    .add  = 0x000B,
    .minimum = (int)(-(1LL << 31)),
    .maximum = (int)((1ULL << 31) - 1),
    .next_int32 = random_next_int32,
    .next_uint32 = random_next_uint32,
    .next_double = random_next_double,
    .next_seeded_int32 = random_next_seeded_int32,
    .next_seeded_uint32 = random_next_seeded_uint32,
    .next_seeded_double = random_next_seeded_double,
};

static double time_since_epoch(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ns = ts.tv_sec * (uint64_t)process_clock.nsec_per_sec + ts.tv_nsec;
    return ns / (double)process_clock.nsec_per_sec;
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
    uint_fast64_t ns = ts.tv_sec * (uint_fast64_t)process_clock.nsec_per_sec + ts.tv_nsec;
#ifdef PROCESS_CLOCK_TIME_ZERO_AT_FIRST_CALL
    // convinience - number of seconds since first call
    static atomic_uint_fast64_t first;
    uint_fast64_t zero = 0;
    atomic_compare_exchange_strong(&first, &zero, ns); // prevent negative
    if (first == 0) {
        traceln("atomic_compare_exchange_strong() failed?");
        first = ns; // Plan B
    }
    ns = ns - first;
#endif
    return ns / (double)process_clock.nsec_per_sec; // nanoseconds to seconds
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
    // e.g. see:
    // https://github.com/apple/darwin-libpthread/blob/master/tests/cond_timed.c
    struct timespec now = {};
    if_error_fatal(clock_gettime(CLOCK_REALTIME, &now)); // since epoch
    uint64_t now_ns = now.tv_sec * process_clock.nsec_per_sec + now.tv_nsec;
    uint64_t wait_ns = seconds * process_clock.nsec_per_sec;
    uint64_t abs_ns = now_ns + wait_ns;
    struct timespec ts = {};
    ts.tv_sec = (time_t)(abs_ns / process_clock.nsec_per_sec);
    ts.tv_nsec = (long)(abs_ns % process_clock.nsec_per_sec);
    double time = process_clock.time();
    int timedwait_result = pthread_cond_timedwait(e, m, &ts);
    time = process_clock.time() - time;
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
        const double nsd = s * (double)process_clock.nsec_per_sec;
        long ns = (long)(((int64_t)nsd) % process_clock.nsec_per_sec);
        struct timespec req = { (time_t)s, ns };
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

static void memmap_file(const char* fn, int_t offset, int_t size,
                        void* *data, int_t *bytes, int* r, bool readonly) {
    assertion(*data == null && *bytes == 0 && *r == 0,
              "invalid (uninitialized or reused) parameters");
    if_error_return(*data != null || *bytes != 0, { *r = EINVAL; }, {});
    assertion(offset >= 0, "negative offset = %lld is invalid", (int64_t)offset);
    if_error_return(offset < 0, { *r = EINVAL; }, {});
    int f = open(fn, readonly ? O_RDONLY : O_RDWR);
    if_error_return(f < 0, { *r = errno; }, {});
    if (size <= 0) { // use file size instead of specified size:
        struct stat s = {};
        if_error_return(fstat(f, &s) < 0, { *r = errno; }, { close(f); });
        size = (int)s.st_size;
    }
    const int protection = PROT_READ | (readonly ? 0 : PROT_WRITE);
    const int flags = readonly ? MAP_PRIVATE : MAP_SHARED;
    *bytes = size;
    *data = mmap(null, size, protection, flags, f, 0);
    if_error_return(*data == null, { *r = errno; }, { close(f); });
    if_error_fatal(close(f));
}

static int memmap_file_readonly(const char* fn, void* *data, int_t *bytes) {
    int r = 0;
    memmap_file(fn, 0, 0, data, bytes, &r, true);
    return r;
}

static int memmap_file_readwrite(const char* fn, int_t offset, int_t size,
                                 void* *data, int_t *bytes) {
    int r = 0;
    memmap_file(fn, offset, size, data, bytes, &r, false);
    return r;
}

static int memmap_file_unmap(void *data, int_t bytes) {
    int r = 0;
    if (data != null && bytes != 0) {
        if_error_fatal(munmap((void*)data, bytes));
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

static void nposix_test_mem() {
    enum { n = 16 };
    uint8_t c[n] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
    uint8_t a[n]; // uninitialized
    mem.fill(a, 0xFF, n);
    for (int i = 0; i < n; i++) { swear(a[i] == 0xFF); }
    mem.zero(a, n);
    for (int i = 0; i < n; i++) { swear(a[i] == 0); }
    mem.copy(a, c, n);
    swear(mem.equal(a, c, n));
    swear(mem.compare(a, c, n) == 0);
    for (int i = 0; i < n; i++) { swear(a[i] == c[i]); }
    a[8] = 0xFF;
    swear(mem.compare(a, c, n) > 0);
    swear(mem.compare(c, a, n) < 0);
    mem.copy(a, c, n);
    mem.move(&a[0], &a[1], n - 1);
    for (int i = 0; i < n - 1; i++) { swear(a[i] == c[i + 1]); }
    mem.copy(a, c, n);
    // https://pubs.opengroup.org/onlinepubs/9699919799/
    // "If copying takes place between objects that overlap,
    //  the behavior is undefined"
    mem.copy(&a[0], &a[1], n - 1);
    for (int i = 0; i < n - 1; i++) {
        if (a[i] != c[i+1]) { // this can happen:
            traceln("a[%d]=%d != c[%d]=%d", i, a[i], i + 1, c[i + 1]);
        }
    }
}

static void nposix_test_str() {
    char s[4];
    assertion(countof(s) == 4, "is countof() broken?");
    strcpy(s, "abc");
    assertion(str.equal("abc", "abc", 0), "compare to itself");
    assertion(str.equal(s,     "abc", 0), "zero terminated not equal");
    assertion(str.equal("abc", s,     0), "zero terminated not equal");
    s[3] = 'z'; // str[] is not zero terminated anymore
    assertion(str.equal(s,     "abc",  3), "non-zero terminated not equal");
    assertion(str.equal("abc", s,      3), "non-zero terminated not equal");
    assertion(str.equal(s,     "abcd", 3), "non-zero terminated not equal");
    assertion(str.equal("abcd", s,     3), "non-zero terminated not equal");
    assertion(!str.equal("abc", "xyz", 0), "should not be equal");
    assertion(!str.equal("abc", "xyz", 3), "compare only 3 bytes");
    // str_to_double
    char numeral[16]= {};
    // 9 bytes:      012345678
    strcpy(numeral, "123.456E0");
    numeral[9] = '2';
    int error = 0;
    assertion(str.to_double(numeral, 9, &error) == 123.456 && error == 0,
              "str.to_double(%*.*s,  9)=%g error %d %s",
              10, 10, numeral, str.to_double(numeral, 9, &error),
              error, strerror(error));
    assertion(str.to_double(numeral, 10, &error) == 12345.6 && error == 0,
              "str.to_double(%*.*s, 10)=%g error %d %s",
              10, 10, numeral, str.to_double(numeral, 10, &error),
              error, strerror(error));
    // hex zero terminated
    strcpy(numeral, "0x123");
    error = 0;
    int64_t i64 = 0;
    str.to_int64(&i64, numeral, 0, &error);
    assertion(i64 == 0x123 && error == 0,
              "str.to_int64(%*.*s, 0)=%lld error %d %s",
              6, 6, numeral, i64, error, strerror(error));
    // hex fixed width not zero terminated:
    numeral[5] = '4';
    error = 0;
    i64 = 0;
    str.to_int64(&i64, numeral, 5, &error);
    assertion(i64 == 0x123 && error == 0,
              "str.to_int64(%*.*s, 5)=%lld error %d %s",
              6, 6, numeral, i64, error, strerror(error));

    // octal zero terminated and not-terminated:
    strcpy(numeral, "0123");
    error = 0;
    i64 = 0;
    str.to_int64(&i64, numeral, 0, &error);
    assertion(i64 == 0123 && error == 0,
              "str.to_int64(%*.*s, 0)=%lld error %d %s",
              4, 4, numeral, i64, error, strerror(error));
    // octal fixed width not zero terminated:
    numeral[4] = '4';
    error = 0;
    i64 = 0;
    str.to_int64(&i64, numeral, 4, &error);
    assertion(i64 == 0123 && error == 0,
              "str.to_int64(%*.*s, 4)=%lld error %d %s",
              5, 5, numeral, i64, error, strerror(error));
    // decimal zero terminated
    strcpy(numeral, "123");
    error = 0;
    i64 = 0;
    str.to_int64(&i64, numeral, 0, &error);
    assertion(i64 == 123 && error == 0,
              "str.to_int64(%*.*s, 0)=%lld error %d %s",
              3, 3, numeral, i64, error, strerror(error));
    // decimal fixed width not zero terminated:
    numeral[3] = '4';
    error = 0;
    i64 = 0;
    str.to_int64(&i64, numeral, 3, &error);
    assertion(i64 == 123 && error == 0,
              "str.to_int64(%*.*s, 3)=%lld error %d %s",
              4, 4, numeral, i64, error, strerror(error));
}

static void nposix_test_random_generator() {
    enum { n = 1000 * 1000 };
    int64_t histogram[100] = {};
    for (int i = 0; i < n; i++) {
        double r = random_generator.next_double();
        int bin = (int)(r * countof(histogram));
        assertion(0 <= bin && bin < countof(histogram), "bin=%d", bin);
        histogram[bin]++;
    }
    for (int i = 0; i < countof(histogram); i++) {
        double percent = histogram[i] * 100.0 / n;
        if (percent < 0.9 || percent > 1.1) {
            traceln("[%d]=%.2f%%", i, percent);
        }
    }
}

static void nposix_test_process_clock() {

}

static void nposix_test_threads() {
    const double time_to_wait = 0.000123 * (random_generator.next_double() + 0.1);
    double deadline = process_clock.time() + time_to_wait;
    threads.sleep(time_to_wait);
    double time = process_clock.time();
    assertion(time >= deadline, "time=%.9f deadline=%.9f", time, deadline);
}

static void nposix_test_memmap() {
    char filename[4096] = {};
    strcpy(filename, "testXXXXXX");
    int fd = mkstemp(filename);
    assertion(fd >= 0, "failed to create temporary file \"%s\"", filename);
    if (fd >= 0) {
        int k = (int)write(fd, "abc", 3);
        assertion(k == 3, "fwrite(\"abc\", 3, 1, f) failed %d bytes", k);
        close(fd);
        int_t bytes = 0;
        void* data = null;
        int r = memmap.file_readonly(filename, &data, &bytes);
        assertion(r == 0 && bytes == 3 && memcmp(data, "abc", 3) == 0,
                  "expected 3 bytes in \"abc\" at %p, %d", data, (int)bytes);
        memmap.file_unmap(data, bytes);
        data = null;
        bytes = 0;
        r = memmap.file_readwrite(filename, 0, 3, &data, &bytes);
        assertion(r == 0 && bytes == 3 && memcmp(data, "abc", 3) == 0,
                  "expected 3 bytes in \"abc\" at %p, %d", data, (int)bytes);
        mem.copy(data, "xyz", 3);
        assertion(memcmp(data, "xyz", 3) == 0,
                  "expected 3 bytes in \"xyz\" at %p, %d", data, (int)bytes);
        memmap.file_unmap(data, bytes);
        fd = open(filename, O_RDONLY);
        assertion(fd >= 0, "failed to open file \"%s\"", filename);
        if (fd >= 0) {
            char content[4];
            int k = (int)read(fd, content, 3);
            assertion(k == 3, "read(%d \"%s\", , 3)=%d", fd, filename, k);
            assertion(mem.equal(content, "xyz", 3), "expected \"xyz\"");
            close(fd);
        }
        unlink(filename);
    }
}

void nposix_test(void) {
    nposix_test_mem();
    nposix_test_str();
    nposix_test_random_generator();
    nposix_test_process_clock();
    nposix_test_threads();
    nposix_test_memmap();
}

#endif

end_c
