#pragma once
/* Copyright (c) Dmitry "Leo" Kuznetsov 2021 see LICENSE for details */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* nanoposix
 
 This is namespace separated version of some (not all) posix functions.
 Most of the functions are fail-fast with stderr output "FATAL: ..." if
 error occurs.
 
 Disadvantages:
    performance:
        Extra call penalty also may affect compiler ability to inline.
        If caller needs to do something like memcpy(~,~,small) fast
        instead of calling mem.copy() it still can.
    need for errors:
        Fail fast is convinient especially when code is organized as
        setup/tear down once (e.g. all threads are created on startup,
        all sync primitives initialized at startup).
        But there could be situations when caller needs to really
        call pthread_create() and check for too many threads created
        and fall back to some Plan B code instead.
        It still can.
     
 Advantages:
    readability:
        less control structures easier to follow logic
    ability to check for absent functionality:
        e.g.
            if (mutext.try_lock == null) {
                // this OS does not implement try_lock()
                // may switch to Plan B and do things differently
            }
    namespacing:
        some of posix name choices are amazingly non-specific:
        e.g. marvel at initstate() and setstate() and many others
    ability to override:
        e.g.
            heap_alloc = heap.alloc;
            heap.alloc = tracing_alloc; // will trace and call heap_alloc
        may allow to trace all heap memory allocation calls
*/


/* small run time convinience over posix */

#ifdef __cplusplus
#define begin_c extern "C" {
#define end_c }
#else
#define begin_c
#define end_c
#endif

#define countof(array) ((int)(sizeof(array) / sizeof(array[0]))) // carefully applicable to arrays only

#define null NULL // true, false are lowercase in stdbool, right?

#define once while (false) // for "do { } once" bracket balance

#ifndef WINDOWS
#   define __path_separator__ '/'
#else
#   define __path_separator__ '\'
#endif

#define __file__ (strrchr(__FILE__, __path_separator__) != null ? \
        strrchr(__FILE__, __path_separator__) + 1 : __FILE__)

#define __location__ __file__, __LINE__, __FUNCTION__

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

/* when absolutely cannot continue execution: */
#define fatal(format, ...) do { \
    println_err("FATAL: %s:%d %s " format, __location__, ##__VA_ARGS__); \
    abort(); \
} once

// mainly for use in unit tests or unrecoverable :
#define if_error_fatal(expression) do { \
    if ((expression) != 0) { fatal("%s", #expression); } \
} once

begin_c

// TODO: explain why signed

typedef intptr_t int_t;

typedef struct {
    void* (*copy)(void* d, const void* s, int_t bytes);
    void* (*move)(void* d, const void* s, int_t bytes);
    int (*compare)(const void* left, const void* right, int_t bytes);
} mem_if; // "_if" stands for "interface"

extern mem_if mem;

typedef struct {
    int_t (*length)(const char* s);
    bool (*equal)(const char* s1, const char* s2, int_t bytes);
    double (*to_double)(const char* s, int bytes); // returns NaN on error
    bool (*starts_with)(const char* s, const char* prefix);
    bool (*contains)(const char* s, const char* substring);
} str_if;

extern str_if str;

typedef struct {
    int32_t max; // 2^31 - 1
    double (*as_int32)(void); // [0..max]
    double (*as_double)(void);
    void (*seed)(unsigned int);
    char* (*init_state)(unsigned seed, char *state, size_t n);
    char* (*set_state)(const char *state);
} random_generator_if;

extern random_generator_if random_generator;

typedef struct {
    void* (*alloc)(intptr_t bytes);
    void* (*realloc)(void* data, intptr_t bytes);
    void* (*free)(void* data);
} heap_if;

extern heap_if heap;


#ifndef NSEC_PER_SEC // suppose to be in time.h but it actually depends
#define NSEC_PER_SEC 1000000000L
#endif

typedef struct {
    const int64_t nsec_per_sec; // nanoseconds  1,000,000,000
    const int64_t usec_per_sec; // microseconds 1,000,000
    const int64_t msec_per_sec; // milliseconds 1,000
    double (*time_since_epoch)(void); // returns number of seconds since 01/01/1970
    double (*time)(void); // returns number of seconds since unspecified unspecified starting point
} process_clock_if;

extern process_clock_if process_clock;

typedef pthread_cond_t event_t;
typedef pthread_mutex_t mutex_t;

typedef struct {
    void (*init)(mutex_t* m);
    void (*lock)(mutex_t* m);
    int  (*try_lock)(mutex_t* m); // 0 or EBUSY only
    void (*unlock)(mutex_t* m);
    void (*dispose)(mutex_t* m);
} mutex_if;

extern mutex_if mutex;

typedef struct {
    void (*init)(event_t* e);
    void (*signal)(event_t* e);
    void (*wait)(event_t* e, mutex_t* m);
    int (*timed_wait)(event_t* e, mutex_t* m, double seconds); // 0 or ETIMEDOUT
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
    int (*file_readonly)(const char* filename, void* *data, int_t *bytes);
    int (*file_readwrite)(const char* filename, void* *data, int_t *bytes);
    int (*file_unmap)(void* data, int_t bytes);
    void* (*allocated)(int_t bytes); // modern sbrk
    void* (*free)(void* data, int_t bytes); // modern setbrk
    void* (*sbrk)(int_t bytes);
    void* (*setbrk)(void* address);
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
