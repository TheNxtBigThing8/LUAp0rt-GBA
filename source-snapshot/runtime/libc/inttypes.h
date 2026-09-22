/* fixGB only wants the fixed-width typedefs; gcc's freestanding <stdint.h>
   already provides them. The PRI* macros are never used by the core. */
#ifndef LUACOREEMU_INTTYPES_H
#define LUACOREEMU_INTTYPES_H
#include <stdint.h>
#endif
