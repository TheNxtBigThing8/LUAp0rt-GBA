#ifndef LUAMD_ASSERT_H
#define LUAMD_ASSERT_H

/* The core is built with NDEBUG, so every assert compiles out. Kept as a real
   header rather than relying on the host's because there is no host. */

#define assert(x) ((void)0)

#endif
