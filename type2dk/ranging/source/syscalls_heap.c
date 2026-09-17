/* Bounded newlib heap, independent of the FreeRTOS heap. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
extern char _pvHeapStart, _pvHeapLimit;
void *_sbrk(ptrdiff_t increment)
{
    static char *current;
    if(!current)current=&_pvHeapStart;
    if(increment<0 || (uintptr_t)increment>(uintptr_t)(&_pvHeapLimit-current)){
        errno=ENOMEM;return (void *)-1;
    }
    char *old=current;current+=increment;return old;
}
