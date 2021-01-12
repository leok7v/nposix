#include "nposix.h"

int main(int argc, const char* argv[]) {
    nposix_test();
    traceln("Hello %s", "World");
    println();
//  assertion(false, "This is expected to fail and terminate execution");
//  swear("foo" == "bar"); // should not ever be here
    traceln("Goodbye %", "Universe");
    return 0;
}
