#ifndef LUAPORT_SAVEDATA_H
#define LUAPORT_SAVEDATA_H

#include "core.h"

/* -------------------------------------------------------------------------
 * LUAport -- read-write access to the host title's savedata container.
 *
 * Ported from LuaPSX/src/savedata.c and LuaPSX/src/savedata.h. This is a PORT,
 * not a rewrite: the mount mechanics below are not derivable from the public
 * libSceSaveData surface, they come from a known-working implementation, and
 * every one of them has already cost someone a debugging session.
 *
 * WHY THIS FILE EXISTS AT ALL. /savedata0 is mounted READ-ONLY inside the
 * ps2emu sandbox. Until M12 nothing in LUAport mounted it -- it appeared only
 * as a read-only fallback search path (adapters/gba/gba_bios.c:108 and
 * adapters/gba/gba_rom.c:118) and nothing ever wrote there. So a GBA save
 * cannot simply be fopen()ed into it. The container has to be remounted
 * read-write, written, and unmounted again -- AND THE UNMOUNT IS WHAT COMMITS.
 *
 * FOUR THINGS HERE ARE NOT GUESSABLE. They are carried across verbatim, and
 * each is recorded with the failure it produces when it is got wrong:
 *
 *   1. sceSaveDataMount RESOLVED THROUGH dlsym REFUSES EVERY MOUNT. Only the
 *      function pointer sitting in the host eboot's own import table works, so
 *      the address is read straight out of it at EBOOT_SAVEDATA_MOUNT_GOT.
 *
 *   2. THE PARAMETER BLOCK IS sceSaveDataMount's, NOT sceSaveDataMount2's:
 *      userId at 0x00, dirName at 0x10, blocks at 0x20, mountMode at 0x28,
 *      titleId left NULL. An earlier version of the reference modelled the
 *      Mount2 layout instead -- mountMode at 0x18, no blocks -- and EVERY mount
 *      was rejected.
 *
 *   3. SD_CREATE MUST NEVER BE SET. With the wrong dirName it silently builds a
 *      SECOND, EMPTY container and mounts that, so the write appears to succeed
 *      and goes nowhere. There is deliberately no SD_CREATE constant anywhere
 *      in this port, and the M12 verifier greps runtime/savedata.c to prove it.
 *
 *   4. RESTORING THE READ-ONLY MOUNT IS MANDATORY. Skip it and the console
 *      cannot close the game from the PS menu -- the operator's only remaining
 *      option is a hard power cycle.
 *
 * THE HOST TITLE IS SHARED WITH LuaPSX, WHICH IS WHY THE OFFSET CARRIES OVER.
 * runtime/core.h:62-63 and LuaPSX/src/core.h:27-28 hold byte-identical
 * EBOOT_GS_THREAD / EBOOT_VIDOUT values, and runtime/platform.h:28 names the
 * host as Star Wars Racer Revenge, CUSA03474 -- the same title whose savedata
 * directory is SLUS-20268. The mount import offset is therefore a property of
 * an eboot this project already depends on, not a new assumption.
 *
 * WHAT THIS FILE DOES NOT KNOW ABOUT. There is no GBA concept anywhere in this
 * translation unit: no .sav, no backup_type, no gamepak, no emulator state. It
 * mounts a container and hands back a writable window. Wiring that window to
 * gpSP's save memory is M12B and lives in adapters/, not here.
 * ------------------------------------------------------------------------- */

/* The savedata directory INSIDE the container, WITHOUT the sdimg_ prefix that
   the image file itself carries. It is region specific, so both are tried.
   Carried from LuaPSX/src/savedata.h:8-11 unchanged. */
#define SAVE_DIR_NAME_US     "SLUS-20268"      /* CUSA03474 -- this host */
#define SAVE_DIR_NAME_EU     "SLES-50366"      /* CUSA03492 */
#define SAVE_DIR_NAME        SAVE_DIR_NAME_US
#define SAVEDATA_MOUNT_POINT "/savedata0"

/* -------------------------------------------------------------- returns ----
 * EVERY STATUS-RETURNING ENTRY POINT BELOW USES 0 FOR SUCCESS.
 *
 * THIS IS THE ONE DELIBERATE BEHAVIOURAL DIVERGENCE FROM THE REFERENCE, AND IT
 * IS A BUG FIX RATHER THAN A TIDY-UP. LuaPSX's savedata_init() returns 1 for
 * SUCCESS while savedata_begin_write() in the very same header returns 0 for
 * success. That mismatch has already caused one real, logged defect: mcard.c
 * read the init result as 0-is-success and silently disabled memory-card
 * persistence on a perfectly healthy savedata mount, while the log two lines
 * above cheerfully said "savedata: ready".
 *
 * The reference left the inversion alone because changing it would silently
 * flip the meaning for any caller not updated in the same breath. LUAport has
 * NO existing callers -- this file is new here -- so the normalisation is free
 * now and impossible later. It is done once, at the only moment it costs
 * nothing.
 *
 * plat_savedata_is_ready() IS THE ONE EXCEPTION AND IT IS NOT A STATUS. It is a
 * PREDICATE: it answers a yes/no question and returns 1 for yes. Making a
 * function called "is_ready" return 0 for "yes, ready" in the name of a uniform
 * convention would recreate exactly the class of confusion this block exists to
 * remove. */
#define PLAT_SD_OK             0
#define PLAT_SD_ENOTREADY     -1   /* init never succeeded                    */
#define PLAT_SD_ENOMOUNTFN    -2   /* the eboot import slot held NULL         */
#define PLAT_SD_ENOMODULE     -3   /* libSceSaveData.sprx would not load      */
#define PLAT_SD_ENOUMOUNT     -4   /* sceSaveDataUmount would not resolve     */
#define PLAT_SD_ERWMOUNT      -5   /* the read-write mount was refused        */
#define PLAT_SD_ESTATTIMEOUT  -6   /* the mount point never disappeared       */
#define PLAT_SD_ERESTORE      -7   /* THE READ-ONLY MOUNT WAS NOT RESTORED    */

/* Resolves the mount primitive out of the host eboot's import table, loads
   libSceSaveData and remembers the kernel mkdir for plat_savedata_mkdir().
   Must run before any write window is opened.

   kmkdir is sceKernelMkdir, resolved by the caller. It may be NULL, in which
   case plat_savedata_mkdir() reports PLAT_SD_ENOTREADY rather than pretending
   to have created anything.

   Returns PLAT_SD_OK (0) on success -- see the returns note above. */
int plat_savedata_init(void *G, void *D, void *load_mod,
                       u64 eboot_base, s32 user_id, void *kmkdir);

/* PREDICATE, NOT A STATUS: 1 when the mount primitive and libSceSaveData are
   both available, 0 otherwise. */
int plat_savedata_is_ready(void);

/* Opens a read-write window on the container: drops the read-only mount and
   remounts read-write. A mount cannot be upgraded in place, which is why the
   read-only one has to go first.

   Returns PLAT_SD_OK (0) on success. Nests, so callers may pair begin/end
   freely. On failure the read-only mount is restored before returning, so a
   refused window never leaves the console in the state that blocks closing the
   game.

   KEEP THE WINDOW SHORT. Prepare the bytes outside it, then copy in. */
int plat_savedata_begin_write(void);

/* Closes the window. THE UNMOUNT INSIDE THIS CALL IS WHAT COMMITS THE BYTES --
   nothing reaches the container until it runs. The unmount completes
   ASYNCHRONOUSLY, so this polls for the mount point to disappear before
   restoring the read-only mount.

   Returns PLAT_SD_OK (0) on success. PLAT_SD_ERESTORE is the serious one: the
   bytes committed but the read-only mount did not come back, and the game can
   no longer be closed from the PS menu. */
int plat_savedata_end_write(void);

/* Creates a directory inside the container. Only legal INSIDE a write window --
   the container is read-only outside one. Returns PLAT_SD_OK (0) on success.

   An already-existing directory is reported as success: the kernel returns
   EEXIST, and for every caller here "the directory is there now" is the
   question actually being asked. */
int plat_savedata_mkdir(const char *dir);

#endif
