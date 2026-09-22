#!/bin/sh
# Verifies the linked image is genuinely position independent.
#
# The subtle failure this exists to catch: with `-static` (rather than
# -static-pie) the linker emits an ET_EXEC and then relaxes every RIP-relative
# GOT load into an absolute immediate --
#
#     mov ppuSetReg8@GOTPCREL(%rip),%r9   ->   mov $0xae10,%r9
#
# The build succeeds, `readelf -r` shows no relocations, and the payload even
# starts, because file-static symbols still get a plain `lea`. Only cross-TU
# global function addresses are wrong, so a dispatch table such as mem.c's
# memSet8ptr[] fills with link-time offsets and the first call through one
# jumps into unmapped memory. On console that reads as a random crash several
# frames in.

ELF=$1
READELF=${READELF:-readelf}
OBJDUMP=${OBJDUMP:-objdump}

[ -f "$ELF" ] || { echo "check_image: no such file: $ELF"; exit 1; }

type=$("$READELF" -h "$ELF" | awk -F: '/^  Type:/ { print $2 }' | awk '{print $1}')
if [ "$type" != "DYN" ]; then
    echo "check_image: FAIL -- ELF type is $type, expected DYN"
    echo "  Link with -static-pie. Plain -static (even alongside -pie) yields"
    echo "  ET_EXEC, and the linker then relaxes GOT loads to absolute"
    echo "  immediates, breaking every cross-TU function pointer."
    exit 1
fi

# Any surviving `mov $0x<addr>,<reg>` whose immediate lands on a known function
# is an un-relaxed absolute reference.
#
# Only FULL 64-BIT destination registers count. A relaxed GOT load always
# materialises a whole pointer, so it targets %rax..%r15; a 32/16/8-bit
# destination (%r8d, %r9w, %r8b, %eax) cannot hold a function address and is
# just an ordinary integer constant. Matching those too made this a numeric
# coincidence test: any constant equal to some function's address failed the
# image -- e.g. the audio clamp `mov $0x7fff,%r8d` (INT16_MAX) once a layout
# shift happened to place a function entry at 0x7fff. The trailing \b matters:
# grep -o would otherwise match the `%r8` prefix inside `%r8d`.
"$READELF" -sW "$ELF" | awk '$4 == "FUNC" { print $2 }'     | sed 's/^0*//' | tr 'A-F' 'a-f' | sort -u > /tmp/ci_funcs.txt

"$OBJDUMP" -d "$ELF" 2>/dev/null     | grep -oE 'mov +\$0x[0-9a-f]+,%(r[abcd]x|rsi|rdi|rbp|rsp|r8|r9|r1[0-5])\b'     | grep -oE '0x[0-9a-f]+' | sed 's/^0x0*//' | sort -u > /tmp/ci_imms.txt

bad=$(comm -12 /tmp/ci_funcs.txt /tmp/ci_imms.txt)
rm -f /tmp/ci_funcs.txt /tmp/ci_imms.txt

if [ -n "$bad" ]; then
    echo "check_image: FAIL -- position independence is broken."
    echo "  These absolute immediates target function addresses:"
    for a in $bad; do echo "    0x$a"; done
    echo "  Link with -static-pie."
    exit 1
fi

echo "check_image: ET_DYN, no absolute function immediates"

# The GOT must be walkable by _start's relocation loop: bounds present and
# 8-aligned. Non-zero slots hold link-time addresses that only that loop fixes.
gs=$("$READELF" -sW "$ELF" | awk '$8 == "__got_start" { print $2 }')
ge=$("$READELF" -sW "$ELF" | awk '$8 == "__got_end"   { print $2 }')
if [ -z "$gs" ] || [ -z "$ge" ]; then
    echo "check_image: FAIL -- __got_start/__got_end missing; _start cannot"
    echo "  relocate the GOT and cross-TU function pointers will be wrong."
    exit 1
fi
if [ $(( 0x$gs % 8 )) -ne 0 ]; then
    echo "check_image: FAIL -- __got_start 0x$gs is not 8-byte aligned"
    exit 1
fi
echo "check_image: GOT 0x$gs..0x$ge ($(( (0x$ge - 0x$gs) / 8 )) slots, relocated at boot)"
