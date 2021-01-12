#pragma once
/* Copyright (c) Dmitry "Leo" Kuznetsov 2021 see LICENSE for details */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* n.posix
 
 This is namespace separated version of some (not all) posix functions.
 Most of the functions are fail-fast with stderr output "FATAL: ..." if
 error occurs.
 
 Disadvantages:
    performance:
        Extra call penalty also may affect compiler ability to inline.
        If caller needs to do something like memcpy(~,~,small) fast
        instead of calling mem.copy() it still can.
    fatal errors:
        Fail fast is convenient especially when code is organized as
        setup/tear down once (e.g. all threads are created on startup,
        all sync primitives initialized at startup).
        But there could be situations when caller needs to really
        call pthread_create() and check for too many threads created
        and fall back to some Plan B code instead.
        It still can do it.
     
 Advantages:
    readability:
        Less control structures make logic easier to follow.
    ability to check for absent functionality:
        e.g.
            if (mutex.try_lock == null) {
                // this OS does not implement try_lock()
                // may switch to Plan B and do things differently
            }
    names-pacing:
        Some of posix name choices are amazingly non-specific:
        e.g. marvel at initstate() and setstate() and many others.
    interception:
        e.g.
            heap_alloc = heap.alloc;
            heap.alloc = tracing_alloc; // will trace and call heap_alloc
        may allow to trace all heap memory allocation calls.
        Another possibility is to override heap.alloc to raise memory_low
        flag, notify subscribers and switch to preallocated reserved pool
        to let subscribers complete persistent data serialization on memory
        low event.
*/


/* small run time convenience over posix */

#ifdef __cplusplus
#define begin_c extern "C" {
#define end_c }
#else
#define begin_c
#define end_c
#endif


#ifndef countof // carefully applicable to arrays only
#define countof(array) ((int)(sizeof(array) / sizeof(array[0])))
#endif

#define null NULL // true, false are lowercase in stdbool, right? null_ptr ugly

#ifndef _ERRNO_T
#define _ERRNO_T
typedef int errno_t;
#endif

#define once while (false) // for "do { } once" bracket balance

#ifndef WINDOWS
#   define __path_separator__ '/'
#else
#   define __path_separator__ '\'
#endif

#define __file__ (strrchr(__FILE__, __path_separator__) != null ? \
        strrchr(__FILE__, __path_separator__) + 1 : __FILE__)

#define __location__ __file__, __LINE__, __FUNCTION__

#define println(format, ...) printf(format "\n", ##__VA_ARGS__)
#define print_err(...) fprintf(stderr, __VA_ARGS__)
#define println_err(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)

// because r/t assert() lacks messaging define extended assertion facility
#define assertion(condition, format, ...) do { \
    if (!(condition)) { \
        println_err("assertion(%s) failed at %s:%d %s " format, \
            #condition, __location__, ##__VA_ARGS__); \
        abort(); \
    } \
} once

// because 'assert' is taken by #include <assert.h>
// following https://github.com/munificent/vigil

#define swear(condition)   assertion(condition, "");

#define if_error_return(condition, _undo_, _do_) do { \
    if (condition) { _do_ _undo_ return; } \
} once

#define if_error_return_result(condition, _undo_, _do_) do { \
    int r ## __LINE__ = (condition);\
    if (r ## __LINE__ != 0) { _do_ _undo_ return r; } else { return 0; } \
} once

// filename, line position and function are very helpful for printf debugging:

#if defined(_DEBUG) || defined(DEBUG)
    #define traceln(format, ...) println_err("%s:%d %s " format, \
                __location__, ##__VA_ARGS__)
#else
    #define traceln(format, ...) do { } once
#endif

// when absolutely cannot continue execution:
#define fatal(format, ...) do { \
    println_err("FATAL: %s:%d %s " format, __location__, ##__VA_ARGS__); \
    abort(); \
} once

// mainly for use in unit tests or unrecoverable :
#define if_error_fatal(expression) do { \
    if ((expression) != 0) { fatal("%s", #expression); } \
} once


begin_c

/* Unsigned types are very error prone.
   It is rare (but still possible) that application code
   works with > 2GB in memory objects on 32-bit systems and a whole.
   On 64-bit systems in-mempry object size limitation of 2^63 versus
   2^64 seems immaterial at present day and age.
   Thus to simplify usage of n.posix API int_t type is used
   for object sizes and offsets which is compatible with pointer
   arithmetics. It is possible that sizeof(int_t) > sizeof(int)
   on some systems and thus arrays cannot be indexed by it
   without losing precision. Exercise caution.
*/

typedef intptr_t int_t;

typedef struct {
    void* (*copy)(void* d, const void* s, int_t bytes);
    void* (*move)(void* d, const void* s, int_t bytes);
    void* (*fill)(void* a, uint8_t byte, int_t bytes); // memset
    void* (*zero)(void* a, int_t bytes); // memset(,0,)
    int   (*compare)(const void* left, const void* right, int_t bytes);
    bool  (*equals)(const void* left, const void* right, int_t bytes);
} mem_if; // "_if" stands for "interface"

extern mem_if mem;

typedef struct {
    int_t (*length)(const char* s);
    bool (*equals)(const char* s1, const char* s2, int_t bytes);
    /* to_double() can be used as to_int32() */
    double (*to_double)(const char* s, int bytes, errno_t *error); // may be NaN
    void (*to_int64)(int64_t* d, const char* s, int bytes, errno_t *error);
    bool (*starts_with)(const char* s, const char* prefix);
    bool (*contains)(const char* s, const char* substring);
} str_if;

extern str_if str;

typedef struct {
    const uint64_t initial_seed;
    uint64_t seed; // only little endian 48 bits used
    uint64_t mult;
    uint16_t add;
    const int32_t minimum; // -2^31
    const int32_t maximum; // 2^31 - 1
    // next_xxxx() functions operate on internal global above seed
    int32_t (*next_int32)(void);  // [minimum..maximum) exclusive
    int32_t (*next_uint32)(void); // [0..maximum) exclusive
    double  (*next_double)(void);  // [0.0 .. 1.0) exclusive
    // next_seeded_xxxx() functions operate on provided seed
    int32_t (*next_seeded_int32)(uint64_t *seed);
    int32_t (*next_seeded_uint32)(uint64_t *seed);
    double  (*next_seeded_double)(uint64_t *seed);
} random_generator_if;

extern random_generator_if random_generator;

typedef struct {
    void* (*alloc)(int_t bytes); // traditional naming
    void* (*realloc)(void* data, int_t bytes);
    void* (*free)(void* data);
    // allocate() is convenience for alloc() + memset(p, 0, bytes)
    void* (*allocate)(int_t bytes);
} heap_if;

extern heap_if heap;

typedef struct {
    const int64_t nsec_per_sec; // nanoseconds  1,000,000,000
    const int64_t usec_per_sec; // microseconds 1,000,000
    const int64_t msec_per_sec; // milliseconds 1,000
    double (*time_since_epoch)(void); // returns number of seconds since 1970
    double (*time)(void); // seconds since unspecified starting point
} process_clock_if;

extern process_clock_if process_clock;

typedef pthread_cond_t event_t;
typedef pthread_mutex_t mutex_t;

typedef struct {
    void (*init)(mutex_t* m);
    void (*lock)(mutex_t* m);
    errno_t (*try_lock)(mutex_t* m); // 0 or EBUSY only
    void (*unlock)(mutex_t* m);
    void (*dispose)(mutex_t* m);
} mutex_if;

extern mutex_if mutex;

typedef struct {
    void (*init)(event_t* e);
    void (*signal)(event_t* e);
    void (*wait)(event_t* e, mutex_t* m);
    errno_t (*timed_wait)(event_t* e, mutex_t* m, double seconds); // ETIMEDOUT
    void (*dispose)(event_t* e);
} event_if;

extern event_if event;

typedef pthread_t thread_t;

typedef struct {
    void (*start)(thread_t* t, void (*function)(void*), void* p,
                          int_t stack_size, bool detached);
    void (*join)(thread_t t);
    void (*sleep)(double seconds); // sleeps for at least specified time
} threads_if;

extern threads_if threads;

typedef struct {
    errno_t (*file_readonly)(const char* filename, void* *data, int_t *bytes);
    errno_t (*file_readwrite)(const char* filename, int_t offset, int_t size,
                          void* *data, int_t *bytes);
    errno_t (*file_unmap)(void* data, int_t bytes);
} memmap_if;

extern memmap_if memmap;

typedef struct {
    bool is_debug_build;
} nposix_if;

extern nposix_if np;

#ifndef NO_TESTS

void nposix_test(void);

#endif

end_c
