# n.posix
"not quite" a posix supplanter but may come close enough

(or whatever you can think "n" stands for... see: http://stallman.org/articles/posix.html)

Most important to me is are qualities:

* interfaces
* name-spacing and partitioning with ability of interception
* fail fast semantics

"POSIX" was just a label for "Portable Operating System [interface]" ("X" was almost 
`silent` but may as well stand for a `mess`).
https://en.wikipedia.org/wiki/POSIX

## Why

There is a need to language, mean and lean runtime (expressed as in the language 
with the ability to link it to application domain code) that allow software engineers 
to express execution on a wide variety of platforms. The goal was used to be called
"portability" and shy into "compatibility between operating systems".

Platform (contrary to "operating systems") is some execution environment that 
allow code to run. Short of "bare metal" (that can also be addressed by minimalistic
implementation of n.posix) the sample set of platform may include:
* Unixes (plural - AIX, HP-UX, Solaris and many others)
* Linux and BSD
* macOS
* Ubuntu
* Windows
* SoC RT/OSes
* ...

There is no shortage of ways to achieve this goal. Just to name the few and to give you 
some examples (and foot for thoughts):
* standards ANSI ISO/IEC 9899:1999 and alike for both C and C++...
* new programming languages (C++, Java, ObjC, Swift, Go, Rust)
* frameworks (STL, boost, MinGW, Cygwin)
* hypervisors and precooked containers (docker and alike)
* ...

To highlight some problems with approaches:
* standards step on each other (`_open()` versus `open()`, `realloc()` examples)
* new programming languages come with ever growing runtime, 
  need complicated compilers support on each platform, introduce new execution
  paradigms like: ARC or garbage collector, exceptions etc
* frameworks often try to solve dual goal - provide set of commonly used algorithms and 
  OS abstraction layer - which results in bloated, complicated, sub-performant code.
* hypervisors and containers run specific Un*x, *NIX, or *N?X implementation in a clumsy 
  memory/storage expensive container with astronomically slow boot time.

This little pet-project address only API part of `posix`.

`posix` itself is the only 'semi-portable' OS interface but has some shortcomings:

* overlaps with ANSI and C standard itself 
  (e.g. see: http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2438.htm)
* slightly obsolete in some parts
* cumbersome to use
* promotes bad habits
* partially deprecated (`strtok()` anyone?)
* unresolved global errno
* implemented and not implemented differently on different popular platforms
* meant to live in a single unistd.h header but spread out and mixed with std*.h

Still better than obsolete BSD `bcopy()` ... or obscure 
Microsoft `beginthreadex` ... family of functions.

Here is an example of programming against straight posix promotes bad habits 
and also advocates for fail fast semantics.

```
pthread_mutex_lock(&m);
// both lock and unlock can silently fail if memory is corrupted 
// or mutex was not [yet|already] initialized
pthread_mutex_unlock(&m);
```


```
// checking results is laborious, makes code less readable
// and unfortunately near useless:
if (pthread_mutex_lock(&m) != 0) { /* now what? */ }
// it not much better because there is actually no Plan B short of abort()
if (pthread_mutex_unlock(&m) != 0) { /* now what? */ }
```

## How

The `n.posix` is not a **supplanter** of a big mess normal C program live in. 
It's rather an attempt to show the `way` to bring some order into chaos.

### interfaces

```
typedef struct {
    void* (*alloc)(int_t bytes); // traditional naming
    void* (*realloc)(void* data, int_t bytes);
    void* (*free)(void* data);
    // allocate() is convenience for alloc() + memset(p, 0, bytes)
    void* (*allocate)(int_t bytes);
} heap_if;
```
Cons: Interfaces have execution penalty of calling dereferenced function (introducing one extra
memory access cycle and preventing inlining for most of the compilers ).
Though same can be said (with emphasis) about C++ virtual functions.

Pros: Interfaces can be implemented globally
```
extern heap_if heap;
```
and as well as be passed as the parameters or be part of other data structures.
This allows simple inheritance and aggregation patterns to be implemented easily

Individual functions inside interfaces can be checked to be implemented or not at runtime:

```
if (threads.start == null) {
    // this platform does not support threads
    // code will resort to single thread event driven scheduling instead
}
```
Interfaces allow tiny brain dead implementation on bare-metal systems which 
despite simplicity may give the code some mileage w/o fragmentation (e.g. for 
embedded system that only allocates data at startup):

```
uint8_t* heap_start; // initialized externally
uint8_t* heap_end;   // or by linker
static uint8_t* heap = start;

static void* heap_alloc(int_t bytes) { 
    void* p = null;
    if (heap + bytes < heap_end) { p = heap; heap += bytes; }
    return p;
}

static void heap_free(void* p) { /* nothing */ }

static void* heap_allocate(int_t bytes) {
    void* p = alloc(bytes);
    if (p != null) { mem.fill(p, 0, bytes); }
    return p;
}

heap_if heap = {
    .alloc = heap_alloc,
    .realloc = null, // intentionally not implemented
    .free = heap_free,
    .allocate = heap_allocate
};
```

### partitioning

Instead of further polluting global namespace `n.posix` code exposes handful of global
variables of `interface` type that are statically initialized by linker.
Careful choice of names e.g.:
* `mem` for memory
* `str` for strings
* `heap` for heap
* `threads` for pthreads
* ...
allows to separate vast amount of functionality into small and air tight containers in
expense necessity for the rest of the code to deal with fixed naming of 'namespaces'.
Name collisions in existing codebases can be resolved as usual by extra wrapping
in runtime or preprocessor.

### fail fast semantics

POSIX has two major mechanisms of returning errors:
* returning -1 and setting `errno` (that also necessitated existence of thread local variables  
  in multithreaded code)
* result (positive value != 0 is treater as error and 0 is OK)

The `errno` mechanism (even with the availability of thread local variables) is a horrible way
of returning error status to upper level of callers. If will be is avoided and wrapped where
necessary in `n.posix` code.

Using function result for error status is acceptable for simple functions but 
may be cumbersome for the functions that need to return results.

For such functions the pattern like
```
typedef struct {
   // ...
   double (*to_double)(const char* s, int bytes, errno_t *error);
   // ...
} str_if;
```
is acceptable.

A lot of functions in `posix` return excessive error information, which is useful 
for e.g. `n.posix` implementation but a burden for the simple application code.
In such situations `n.posix` handles errors internally (making them fatal) and exposes
functions that do not return error information. 

It can be a convenient deficiency but for a complicated scenarios where such errors
need to be handled by callers code it can always resort back to regular `posix` API.

### inheritance and aggregation

The `n.posix` itself does not use any inheritance or aggregation at this moment (but 
may start using it in the future). This is how inheritance and aggregation may be 
accomplished by example:

```
typedef struct {
   heap_if recycler; // inherits and implements thread only heap
   // ...
} looper_if;
```

E.g. looper that connects message queue to a thread may choose to implement recycler
pattern heap (fixed array of linked lists of memory regions that do not touch synchronization
prone `heap.alloc()` to allocated and free queue elements) and expose it or,
short of it's own implementation, may just inherit and expose global `heap` implementation
before implementing it's own:

```
looper_if looper = {
   .recycler = heap, // inheritance alike C++
} ;
```

Some more complicated interfaces may aggregate other interfaces like:

```
typedef struct {
    void (*format)(const char* format_specification, ...);
} formatter_if;

typedef struct {
    int (*read)(void* data, int_t bytes, errno_t *error);
    int (*write)(const void* data, int_t bytes, errno_t *error);
} stream_if;


typedef struct {
    stream_if stream;
    formatter_if formatter;
} formatted_stream_if;
```

### work in progress

The `n.posix` does not aim to be "everything for everyone". It is always work in progress
and subject to change w/o notice. 

I implement it for myself and maintain it myself and use in my own projects.
Implementation is trivial the architecture of the API is what matters.

You may want to consider similar architectural approach for other APIs in your system.

Jan 12, 2021 leo.kuzentsov@gmail.com

