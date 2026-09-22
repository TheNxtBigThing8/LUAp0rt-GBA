#ifndef LUAMD_UNISTD_H
#define LUAMD_UNISTD_H

/* Deliberately empty.

   clown68000.c includes <unistd.h> purely to give _POSIX_VERSION a chance to
   appear, and then picks sigsetjmp over setjmp if it finds >= 200112L, on the
   grounds that setjmp is slow on the BSDs. There is no signal mask to save
   inside a payload with no signal handling beyond our own fault trap, so the
   plain path is both correct and faster. Leaving _POSIX_VERSION undefined is
   how we select it. */

#endif
