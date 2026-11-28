#!/usr/bin/env python3
# random ops on wide ints, rill output against python big ints
# usage: tools/widetest.py [rounds] [seed]
import os
import random
import subprocess
import sys
import tempfile

BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "rill")
OPS = ["+", "-", "*", "/", "%", "&", "<<", ">>", "<", ">", "="]
TYPES = [("u65", 65, False), ("i65", 65, True), ("u67", 67, False), ("i67", 67, True),
         ("u100", 100, False), ("i100", 100, True), ("u128", 128, False), ("i128", 128, True)]


def norm(v, w, signed):
    v &= (1 << w) - 1
    if signed and v >> (w - 1):
        v -= 1 << w
    return v


def pick(w, signed):
    hi = (1 << (w - 1)) - 1 if signed else (1 << w) - 1
    lo = -hi if signed else 0  # the signed minimum has no literal, so it is left out
    r = random.random()
    if r < 0.2:
        v = random.choice([0, 1, 2, 3, hi, hi - 1, lo, lo + 1, 1 << 63, (1 << 64) - 1, 1 << 64])
    elif r < 0.5:
        v = random.randint(-1000, 1000)
    else:
        v = random.randint(lo, hi)
    return max(lo, min(hi, v))


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def expect(op, a, b, w, signed):
    if op == "<":
        return "#t" if a < b else "#f"
    if op == ">":
        return "#t" if a > b else "#f"
    if op == "=":
        return "#t" if a == b else "#f"
    v = {"+": lambda: a + b, "-": lambda: a - b, "*": lambda: a * b,
         "/": lambda: tdiv(a, b), "%": lambda: a - b * tdiv(a, b),
         "&": lambda: a & b, "<<": lambda: a << b, ">>": lambda: a >> b}[op]()
    return str(norm(v, w, signed))


def bind(name, ty, v):
    return f"{name} : {ty} <~ " + (f"0 - {-v}" if v < 0 else str(v))


def program(ty, w, signed, n=8):
    src, want = [], []
    for i in range(n):
        a, b, c = pick(w, signed), pick(w, signed), random.randint(0, 127)
        src += [bind(f"a{i}", ty, a), bind(f"b{i}", ty, b), bind(f"c{i}", ty, c)]
        for op in OPS:
            if op in ("/", "%") and b == 0:
                continue
            shift = op in ("<<", ">>")
            src.append(f">>! a{i} {op} {'c' if shift else 'b'}{i}")
            rhs = c if shift else b
            want.append((f"{a} {op} {rhs} as {ty}", expect(op, a, rhs, w, signed)))
    return "\n".join(src) + "\n", want


def main():
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else random.randrange(1 << 30)
    random.seed(seed)
    print("seed", seed)
    bad = checked = 0
    with tempfile.TemporaryDirectory() as d:
        src_path, exe = os.path.join(d, "t.rill"), os.path.join(d, "t")
        for _ in range(rounds):
            for ty, w, signed in TYPES:
                text, want = program(ty, w, signed)
                with open(src_path, "w") as f:
                    f.write(text)
                built = subprocess.run([BIN, src_path, "-o", exe], capture_output=True, text=True)
                if built.returncode:
                    print("build failed for", ty)
                    print(built.stderr)
                    return 1
                got = subprocess.run([exe], capture_output=True, text=True).stdout.split("\n")[:-1]
                if len(got) != len(want):
                    print(f"{ty}: printed {len(got)} lines, wanted {len(want)}")
                    return 1
                for g, (what, e) in zip(got, want):
                    checked += 1
                    if g != e:
                        bad += 1
                        print(f"MISMATCH {what}: got {g}, python says {e}")
    print(f"{checked} checked, {bad} wrong")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
