/* LUAport M1 -- <sys/types.h> for the gpSP closure ONLY.
 *
 * gpsp/libretro/libretro-common/include/streams/file_stream.h line 31 includes
 * <sys/types.h> unconditionally. The M0 build never included it, so no
 * equivalent exists under runtime/libc/.
 *
 * Without this file the include would resolve to the HOST glibc header, which
 * is exactly the host-header bleed the freestanding build exists to prevent:
 * glibc's sys/types.h drags in <features.h>, __BEGIN_DECLS, and a pile of
 * typedefs that can and do disagree with runtime/libc's minimal set. The build
 * uses -ffreestanding but NOT -nostdinc, so nothing else stops that resolution.
 *
 * Only the types the libretro headers actually name are declared. Everything
 * here is a typedef -- this file emits no code and no data, and therefore
 * contributes nothing to the M1 size measurement.
 *
 * M1-include-path only; runtime/libc/ is untouched so M0 is unaffected.
 */
#ifndef LUAPORT_M1_SYS_TYPES_H
#define LUAPORT_M1_SYS_TYPES_H

#include <stddef.h>

/* LP64: long is 64-bit. These match the host ABI the payload is built for. */
#ifndef __ssize_t_defined
#define __ssize_t_defined
typedef long ssize_t;
#endif

typedef long off_t;
typedef long long off64_t;

typedef unsigned int  mode_t;
typedef unsigned int  uid_t;
typedef unsigned int  gid_t;
typedef long          time_t_sys;   /* NOT `time_t`: runtime/libc/time.h owns
                                       that name and defines it as `long`.
                                       Redefining it here would be a duplicate
                                       typedef in every TU that includes both. */

#endif
