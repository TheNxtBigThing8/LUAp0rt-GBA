#ifndef LUAMD_SETJMP_H
#define LUAMD_SETJMP_H

/* clown68000 uses setjmp/longjmp to unwind 68000 exceptions (address errors,
   illegal instructions, TRAPs) out of the middle of the interpreter, so this
   is not optional -- a stub that never returns twice would turn every guest
   exception into a fall-through and corrupt guest state.

   Only the callee-saved registers, the stack pointer and the return address
   need saving; the x86-64 SysV caller already assumes everything else is
   clobbered by a call.

   'returns_twice' is the load-bearing part. Without it GCC has no idea this
   function can come back a second time, so it happily keeps caller locals in
   registers across the call -- and longjmp restores the register file from
   under it. That produces stale locals rather than a crash, which is the
   worst way for this to fail. */

typedef unsigned long jmp_buf[8];

__attribute__((returns_twice)) int setjmp(jmp_buf env);
__attribute__((noreturn))     void longjmp(jmp_buf env, int val);

/* clown68000 only reaches for the sig* variants when _POSIX_VERSION says they
   exist; src/libc/unistd.h makes sure it never does. Defined anyway so that a
   future core update that uses them unconditionally still builds. */
typedef jmp_buf sigjmp_buf;
#define sigsetjmp(env, save)  setjmp(env)
#define siglongjmp(env, val)  longjmp(env, val)

#endif
