#!/usr/bin/env python3
"""
ETCH DS -- by Synth Centric
Proves the audio path is free of division and floating point.

Walks the call graph of build/arm9.elf from the render interrupt
(render_irq) through direct calls, interworking veneers and register-indirect
long calls (ITCM code calls through `ldr rN, =func; blx rN`; conservatively,
every function address in a reachable function's literal pool), and fails if any
reachable function is a division/modulo helper, a soft-float helper or a
libm routine, or if any reachable instruction is a hardware divide.
Also reports where each reachable function lives (ITCM or main RAM).

    tools/check_audio_path.py [build/arm9.elf]
"""
import re
import subprocess
import sys

PREFIX = "/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-"
ROOTS = ["render_irq"]
BAD = re.compile(r"(__aeabi_(i|ui|l|ul)div|__aeabi_(i|ui|l|ul)divmod|__aeabi_[fd](add|sub|mul|div|cmp|2|to)|"
                 r"__(u)?div[sd]i3|__(u)?mod[sd]i3|__divdi3|__udivdi3|__aeabi_[fd]\w+|"
                 r"^(sqrt|exp|log|pow|sin|cos|tan|floor|ceil)f?$)")


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else "build/arm9.elf"
    dis = subprocess.check_output([PREFIX + "objdump", "-d", elf], text=True).splitlines()
    syms = {}
    for line in subprocess.check_output([PREFIX + "nm", elf], text=True).splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "tTwW":
            syms[int(p[0], 16) & ~1] = p[2]
    funcs, words, order = {}, {}, []
    cur = None
    for line in dis:
        m = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
        if m:
            cur = m.group(2)
            funcs[cur] = []
            order.append((int(m.group(1), 16), cur))
            continue
        m = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+\.word\s+0x([0-9a-f]+)", line)
        if m:
            words[int(m.group(1), 16)] = int(m.group(3), 16)
        if cur:
            funcs[cur].append(line)
    addr_of = {n: a for a, n in order}

    def callees(f):
        out = set()
        regs = {}
        for line in funcs.get(f, []):
            m = re.search(r"\tldr\s+(r\d+|ip|lr), \[pc, #-?\d+\]\s+@ \(?([0-9a-f]+)", line)
            if m and int(m.group(2), 16) in words:
                regs[m.group(1)] = words[int(m.group(2), 16)] & ~1
            m = re.search(r"\t(bl|blx|b|b\.w|bx)\s+[0-9a-f]+ <([^>+]+)>", line)
            if m and m.group(2) != f:
                out.add(m.group(2))
            m = re.search(r"\t(blx|bx|mov)\s+(r\d+|ip)$", line.strip()) or re.search(r"\t(blx|bx)\s+(r\d+|ip)\s*$", line)
            if m and m.group(2) in regs and regs[m.group(2)] in syms:
                out.add(syms[regs[m.group(2)]])
            if re.search(r"\t[su]div\s", line):
                out.add("<hardware divide>")
        # conservative: any function address in this function's literal pool
        # (long calls, veneers, function pointers) counts as a callee
        for line in funcs.get(f, []):
            m = re.search(r"\.word\s+0x([0-9a-f]+)", line)
            if m and int(m.group(1), 16) & ~1 in syms:
                out.add(syms[int(m.group(1), 16) & ~1])
        return out

    seen, stack, bad = set(), list(ROOTS), []
    while stack:
        f = stack.pop()
        if f in seen:
            continue
        seen.add(f)
        if BAD.search(f) or f == "<hardware divide>":
            bad.append(f)
            continue
        stack.extend(callees(f))
    print("audio path (reachable from %s): %d functions" % (", ".join(ROOTS), len(seen)))
    for f in sorted(seen):
        a = addr_of.get(f)
        where = "?" if a is None else ("ITCM" if a < 0x02000000 else "main RAM")
        print("  %-36s %s" % (f, where))
    if bad:
        print("FAIL: division / floating point reachable from the audio path:", ", ".join(sorted(bad)))
        sys.exit(1)
    print("OK: no division, modulo or floating-point routine is reachable from the render interrupt")


if __name__ == "__main__":
    main()
