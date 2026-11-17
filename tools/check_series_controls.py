"""ETCH DS in melonDS: screenshot every screen and check the series controls
(SERIES_SPEC.md 2): LIVE bass A+RIGHT, X accent, SELECT+START and B fills,
hold R, SELECT+UP queue.  usage: tools/check_series_controls.py [OUTDIR]"""
import os, sys, re, struct, subprocess, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools")); os.chdir(ROOT)
import run_melonds as R
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "melonds_series")
src = ['#include <stddef.h>', '#include "ipc.h"']
for f in ["playing", "cur", "queued", "fill", "fill_hold"]:
    src.append("const unsigned off_%s = offsetof(shared_t, %s);" % (f, f))
src.append("const unsigned off_pat0 = offsetof(shared_t, proj.pat);")
with tempfile.TemporaryDirectory() as d:
    c, o = os.path.join(d, "o.c"), os.path.join(d, "o.o")
    open(c, "w").write("\n".join(src) + "\n")
    subprocess.check_call([R.PREFIX + "gcc", "-mcpu=arm946e-s", "-D__NDS__", "-DARM9", "-c", "-Icommon", "-Icorrosion", "-Iarm9/source",
                           "-I/opt/wonderful/thirdparty/blocksds/core/libs/libnds/include", c, "-o", o])
    dump = subprocess.check_output([R.PREFIX + "objdump", "-s", "-j", ".rodata", o], text=True)
    names = subprocess.check_output([R.PREFIX + "nm", o], text=True)
data = b""
for line in dump.splitlines():
    m = re.match(r"^\s*([0-9a-f]{4,8}) ((?:[0-9a-f]{2,8} ?){1,4})", line)
    if m: data += bytes.fromhex(m.group(2).replace(" ", ""))
offs = {}
for line in names.splitlines():
    p = line.split()
    if len(p) == 3 and p[2].startswith("off_"): offs[p[2][4:]] = struct.unpack_from("<I", data, int(p[0], 16))[0]
syms = R.nm_symbols(os.path.join(ROOT, "build", "arm9.elf"))
b = syms["shared_store"]
watch = ["%s=%x:1" % (f, b + offs[f]) for f in ["playing", "cur", "queued", "fill", "fill_hold"]]
watch += ["note0=%x:1" % (b + offs["pat0"] + 0), "flags0=%x:1" % (b + offs["pat0"] + 16)]
T = lambda x: "%d:TOUCH:%d,10,3" % x
script = ["30:SHOT", "90:START:3", "300:SHOT",
          "340:DUMP", "345:A:30", "360:RIGHT:3", "380:DUMP",
          "400:X:3", "410:DUMP",
          "430:SELECT+START:3", "440:DUMP", "450:SELECT+START:3", "460:DUMP",
          "470:B:3", "480:DUMP", "490:B:3", "500:DUMP",
          "510:R:30", "525:DUMP", "545:DUMP",
          "560:SELECT+UP:3", "570:DUMP", "600:SHOT",
          T((640, 67)), "700:SHOT", T((720, 108)), "780:SHOT", T((800, 108)), "860:SHOT",
          T((880, 149)), "940:SHOT", T((960, 190)), "1020:SHOT", T((1040, 231)), "1100:SHOT",
          T((1120, 231)), "1180:SHOT"]
os.makedirs(OUT, exist_ok=True)
res = subprocess.run([os.path.join(ROOT, "tools/melonds_harness/harness"), "etch_ds.nds", "1250", os.path.join(OUT, "doc"),
                      ";".join(script), ";".join(watch)], capture_output=True, text=True)
mem = {}
for line in res.stdout.splitlines():
    p = line.split()
    if p and p[0] == "MEM": mem.setdefault(int(p[1]), {})[p[2]] = int(p[3])
from PIL import Image
for f in sorted(os.listdir(OUT)):
    if f.endswith(".ppm"):
        Image.open(os.path.join(OUT, f)).save(os.path.join(OUT, f[:-4] + ".png")); os.remove(os.path.join(OUT, f))
M = lambda fr, k: mem[fr][k]
ok = []
def chk(name, c, d=""): ok.append(c); print("PASS" if c else "FAIL", name, d)
nb, fb = M(340, "note0"), M(340, "flags0")
chk("LIVE bass A+RIGHT: a semitone up", (fb & 3) == 1 and M(380, "note0") == nb + 1 or (fb & 3) != 1 and (M(380, "flags0") & 3) == 1, "%d -> %d" % (nb, M(380, "note0")))
chk("LIVE bass X: accent toggles", (M(410, "flags0") ^ M(380, "flags0")) & 4 == 4)
chk("SELECT+START latches the fill on, then off", M(440, "fill_hold") == 1 and M(460, "fill_hold") == 0)
chk("B tapped on LIVE toggles the fill", M(480, "fill_hold") == 1 and M(500, "fill_hold") == 0)
chk("hold R: fill while held, off on release", M(525, "fill_hold") == 1 and M(545, "fill_hold") == 0)
chk("SELECT+UP queues the next pattern", M(570, "queued") == (M(570, "cur") + 1) % 32, "cur %d queued %d" % (M(570, "cur"), M(570, "queued")))
chk("still playing", M(570, "playing") == 1)
print("%d/%d series control checks passed" % (sum(ok), len(ok)))
sys.exit(0 if all(ok) else 1)
