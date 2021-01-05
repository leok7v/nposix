# n.posix
"not" a posix supplicant but close enough, or "namespaced" posix or whatever you can think "n" stands for...

Most important to me is two qualities:

* namespacing and partitioning with ability of interception
* fail fast semantics

## Why

Because `posix` is the only known 'semi-portable' OS interface but has some shortcomings:

* overlaps with ANSI and C standard itself (e.g. see: http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2438.htm)
* slightly obsolete in some parts
* cumbersome to use
* promotes bad habbits
* partially deprected (`strtok()` anyone?)
* unresolved global errno
* implemented and not implemented differently on different popular platforms

Still better then obsolete BSD `bcopy()` ... or obscure Microsoft `beginthreadex` ... family of functions.

I've noticed that programming against straight posix promotes bad habbits like

```
pthread_mutex_lock(&m);
// both lock and unlock can silently fail if memory is corrupted or mutex was not [yet|already] initialized
pthread_mutex_unlock(&m);
```

```
if (pthread_mutex_lock(&m) != 0) { /* now what? */ }
// it not much better because there is actually no Plan B short of abort()
if (pthread_mutex_unlock(&m) != 0) { /* now what? */ }
```
