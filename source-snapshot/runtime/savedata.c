#include "core.h"
#include "shim.h"
#include "savedata.h"

/* printf() is declared in <stdio.h> and NOWHERE else -- shim.h declares only
   klog(), shim_init() and the arena accessors. Without this include printf is
   implicitly declared as returning int, which is a -Wall warning that is easy
   to scroll past on an older compiler and a HARD ERROR on GCC 14+. The same
   trap is documented at apps/m0diag/main.c:182-188 for malloc().

   The reference (LuaPSX/src/savedata.c) sidesteps this by hand-rolling a hex
   formatter rather than including anything. That is not copied here: the shim's
   printf is already linked into this image, and a second hand-written integer
   formatter is a second thing that can be wrong about the numbers this
   milestone exists to report. */
#include <stdio.h>

/* Ported from LuaPSX/src/savedata.c. See runtime/savedata.h for the four
   non-guessable facts this file encodes and for the return-convention note --
   the one deliberate behavioural divergence from the reference. */

/* The host eboot's import-table slot for sceSaveDataMount. NOT a dlsym result:
   a dlsym-resolved sceSaveDataMount refuses every mount. See savedata.h note 1. */
#define EBOOT_SAVEDATA_MOUNT_GOT 0x3893F0

/* Mount modes. THERE IS NO SD_CREATE HERE AND THERE MUST NEVER BE ONE.
   With a wrong dirName, SD_CREATE silently fabricates a second, empty
   container and mounts THAT -- so the write "succeeds" and goes nowhere.
   Omitting the constant entirely is what makes the mistake unavailable rather
   than merely discouraged, and `make m12-verify` greps this file to prove it. */
#define SD_RO      1
#define SD_RW      2
#define SD_BLOCKS  32768

/* sceSaveDataMount's parameter block -- NOT sceSaveDataMount2's. Modelling the
   Mount2 layout (mountMode at 0x18, no blocks) gets every mount rejected. */
#define SD_P_USERID    0x00
#define SD_P_DIRNAME   0x10
#define SD_P_BLOCKS    0x20
#define SD_P_MOUNTMODE 0x28
#define SD_P_SIZE      128
#define SD_R_SIZE      64

/* The unmount is asynchronous. Poll for the mount point to vanish: 20 tries at
   250 ms is the reference's figure (LuaPSX/src/savedata.c:169-173) and covers
   five seconds, which is far longer than any observed commit. */
#define SD_POLL_TRIES  20
#define SD_POLL_US     250000

/* sceKernelStat's buffer. Oversized on purpose -- the exact struct stat layout
   is not depended on, only whether the call succeeds, so a generous buffer
   removes any chance of the kernel writing past it. */
#define SD_STAT_BYTES  256

static void *sd_gadget;
static void *sd_mount_fn;
static void *sd_umount_fn;
static void *sd_stat_fn;
static void *sd_usleep_fn;
static void *sd_mkdir_fn;
static s32   sd_user_id;
static s32   sd_ready;
static s32   sd_depth;
static char  sd_dir[32];

static void sd_copy_str(char *dst, int size, const char *src) {
    int i = 0;
    for (; i < size; i++) dst[i] = 0;
    for (i = 0; src[i] && i < size - 1; i++) dst[i] = src[i];
}

/* One mount attempt against one directory name. */
static s32 sd_mount_dir(const char *dir, u32 mode) {
    u8   params[SD_P_SIZE];
    u8   result[SD_R_SIZE];
    char name[32];

    for (int i = 0; i < SD_P_SIZE; i++) params[i] = 0;
    for (int i = 0; i < SD_R_SIZE; i++) result[i] = 0;
    sd_copy_str(name, sizeof(name), dir);

    *(u32 *)(params + SD_P_USERID)    = (u32)sd_user_id;
    *(u64 *)(params + SD_P_DIRNAME)   = (u64)name;
    *(u64 *)(params + SD_P_BLOCKS)    = SD_BLOCKS;
    *(u32 *)(params + SD_P_MOUNTMODE) = mode;    /* never SD_CREATE */

    return (s32)NC(sd_gadget, sd_mount_fn, (u64)params, (u64)result, 0, 0, 0, 0);
}

/* Tries the remembered region first, then both known ones.
   SAFE ONLY BECAUSE SD_CREATE IS NEVER SET: a wrong name simply fails, instead
   of fabricating an empty container that would swallow the write. Returns 1 on
   success -- a private predicate, not part of the public 0-is-success API. */
static int sd_mount_any(u32 mode) {
    const char *both[2];
    both[0] = SAVE_DIR_NAME_US;
    both[1] = SAVE_DIR_NAME_EU;

    if (sd_dir[0] && sd_mount_dir(sd_dir, mode) == 0) return 1;

    for (int i = 0; i < 2; i++) {
        if (sd_mount_dir(both[i], mode) == 0) {
            sd_copy_str(sd_dir, sizeof(sd_dir), both[i]);
            return 1;
        }
    }
    return 0;
}

int plat_savedata_init(void *G, void *D, void *load_mod,
                       u64 eboot_base, s32 user_id, void *kmkdir) {
    sd_gadget  = G;
    sd_user_id = user_id;
    sd_mkdir_fn = kmkdir;
    sd_ready   = 0;
    sd_depth   = 0;

    /* The import-table entry, NOT dlsym -- savedata.h note 1. */
    sd_mount_fn = (void *)*(u64 *)(eboot_base + EBOOT_SAVEDATA_MOUNT_GOT);
    if (!sd_mount_fn) {
        klog("savedata: eboot mount import is null\n");
        return PLAT_SD_ENOMOUNTFN;
    }

    s32 mod = (s32)NC(G, load_mod, (u64)"libSceSaveData.sprx", 0, 0, 0, 0, 0);
    if (mod < 0) {
        klog("savedata: libSceSaveData.sprx failed to load\n");
        return PLAT_SD_ENOMODULE;
    }

    sd_umount_fn = SYM(G, D, mod, "sceSaveDataUmount");
    sd_stat_fn   = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelStat");
    sd_usleep_fn = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelUsleep");

    /* Initialisation is best-effort: the reference tries 3 then 2 and does not
       treat a failure as fatal, because the mount import works regardless. The
       return value is logged rather than acted on, so a firmware that names it
       differently does not cost the whole milestone. */
    void *init = SYM(G, D, mod, "sceSaveDataInitialize3");
    if (!init) init = SYM(G, D, mod, "sceSaveDataInitialize2");
    if (init) {
        s32 ir = (s32)NC(G, init, 0, 0, 0, 0, 0, 0);
        printf("savedata: initialize ret 0x%X\n", (unsigned)ir);
    } else {
        klog("savedata: no sceSaveDataInitialize3/2 -- continuing\n");
    }

    if (!sd_umount_fn) {
        klog("savedata: sceSaveDataUmount not resolvable\n");
        return PLAT_SD_ENOUMOUNT;
    }

    sd_copy_str(sd_dir, sizeof(sd_dir), SAVE_DIR_NAME);
    sd_ready = 1;
    klog("savedata: ready (eboot mount import)\n");
    return PLAT_SD_OK;
}

int plat_savedata_is_ready(void) { return sd_ready; }

int plat_savedata_begin_write(void) {
    if (!sd_ready) return PLAT_SD_ENOTREADY;
    if (sd_depth++ > 0) return PLAT_SD_OK;      /* already open */

    /* A mount cannot be upgraded in place, so drop the read-only one first. */
    NC(sd_gadget, sd_umount_fn, (u64)SAVEDATA_MOUNT_POINT, 0, 0, 0, 0, 0);

    if (!sd_mount_any(SD_RW)) {
        klog("savedata: RW mount refused, restoring read-only\n");
        sd_mount_any(SD_RO);                    /* leave it as we found it */
        sd_depth = 0;
        return PLAT_SD_ERWMOUNT;
    }
    return PLAT_SD_OK;
}

int plat_savedata_end_write(void) {
    if (!sd_ready) return PLAT_SD_ENOTREADY;
    if (--sd_depth > 0) return PLAT_SD_OK;
    if (sd_depth < 0) { sd_depth = 0; return PLAT_SD_OK; }

    int rc = PLAT_SD_OK;

    /* THE COMMIT. Bytes reach the container here and nowhere else. */
    NC(sd_gadget, sd_umount_fn, (u64)SAVEDATA_MOUNT_POINT, 0, 0, 0, 0, 0);

    /* ...and it is asynchronous, so wait for the mount point to disappear.
       sceKernelStat returning NON-ZERO means the path is gone, which is the
       success condition -- the polarity is easy to invert by accident. */
    if (sd_stat_fn) {
        u8 st[SD_STAT_BYTES];
        int gone = 0;
        for (int i = 0; i < SD_POLL_TRIES; i++) {
            if ((s32)NC(sd_gadget, sd_stat_fn, (u64)SAVEDATA_MOUNT_POINT,
                        (u64)st, 0, 0, 0, 0) != 0) {
                gone = 1;
                break;
            }
            if (sd_usleep_fn)
                NC(sd_gadget, sd_usleep_fn, SD_POLL_US, 0, 0, 0, 0, 0);
        }
        if (!gone) {
            klog("savedata: WARNING mount point still present after unmount\n");
            rc = PLAT_SD_ESTATTIMEOUT;
        }
    }

    /* MANDATORY. Without the read-only mount back in place the console cannot
       close the game from the PS menu. Reported even if the commit already
       flagged a timeout, because this is the more serious of the two. */
    if (!sd_mount_any(SD_RO)) {
        klog("savedata: WARNING could not restore read-only mount\n");
        rc = PLAT_SD_ERESTORE;
    }

    return rc;
}

int plat_savedata_mkdir(const char *dir) {
    if (!sd_ready || !sd_mkdir_fn) return PLAT_SD_ENOTREADY;

    s32 r = (s32)NC(sd_gadget, sd_mkdir_fn, (u64)dir, 0x1FF, 0, 0, 0, 0);
    if (r == 0) return PLAT_SD_OK;

    /* Already there is success: every caller is asking "is the directory
       present now", not "did I personally create it". Both the SCE-coded and
       the raw BSD spelling of EEXIST are accepted, because which one surfaces
       depends on the entry point the firmware exposes. */
    if ((u32)r == 0x80020011u || r == -17) return PLAT_SD_OK;

    printf("savedata: mkdir %s failed, ret 0x%X\n", dir, (unsigned)r);
    return (int)r;
}
