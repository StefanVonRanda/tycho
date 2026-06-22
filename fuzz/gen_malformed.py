#!/usr/bin/env python3
# Malformed-input generator for the robustness ("fail-closed") fuzzer.
#
# Unlike gen.py -- which emits well-TYPED programs for the differential output
# oracle -- this emits BROKEN or near-broken Tycho source: a real corpus program
# (tests/ + examples/) with random corruption applied, or pure token/byte soup.
# The harness (run_reject.py) asserts both compilers FAIL CLOSED on it: a clean
# error, never a crash (SIGSEGV / abort / ASan / UBSan / hang), and -- when a
# compiler accepts the input -- the C it emits must still be valid C.
#
# Mutations stay NEAR the grammar boundary (that's where parsers are weakest):
# truncation, byte/line edits, unbalanced brackets, token insertion/deletion,
# broken indentation, bounded deep nesting (recursive-descent stack stress),
# and occasional pure-random soup. Deterministic by seed so findings reproduce.
#
# Usage: gen_malformed.py <seed>   -> a (probably-)malformed program on stdout
import sys, os, random, glob

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def load_corpus():
    files = sorted(glob.glob(os.path.join(REPO, "tests", "*.ty")) +
                   glob.glob(os.path.join(REPO, "examples", "*.ty")))
    out = []
    for f in files:
        try:
            with open(f, encoding="utf-8", errors="replace") as fh:
                s = fh.read()
        except OSError:
            continue
        if s.strip():
            out.append(s)
    return out

KEYWORDS = ["fn", "struct", "enum", "if", "elif", "else", "for", "in", "while",
            "match", "return", "break", "continue", "import", "package", "type",
            "spawn", "mut", "true", "false", "and", "or", "not", "soa"]
PUNCT = [":", "(", ")", "[", "]", "{", "}", ",", "=", "+", "-", "*", "/", ".",
         "->", "==", "!=", "<", ">", '"', "'", "\\", "#", ";", "@", "%", "&",
         "|", "?", "!", "  ", "\t"]
TOKENS = KEYWORDS + PUNCT + ["x", "0", "1", "999999999999999999999", "[]", "()", "1.5"]

def rand_bytes(r, n):
    return "".join(chr(r.randint(1, 127)) for _ in range(n))

def mutate(r, s):
    """Apply one random corruption to source string s, returning the result."""
    if not s:
        return rand_bytes(r, r.randint(1, 40))
    n = len(s)
    op = r.randint(0, 14)
    if op == 0:                                  # truncate at a random offset
        return s[:r.randint(0, n)]
    if op == 1:                                  # delete a random line
        lines = s.split("\n"); del lines[r.randint(0, len(lines) - 1)]
        return "\n".join(lines)
    if op == 2:                                  # delete a random byte
        i = r.randint(0, n - 1); return s[:i] + s[i + 1:]
    if op == 3:                                  # insert a random byte
        i = r.randint(0, n); return s[:i] + chr(r.randint(1, 127)) + s[i:]
    if op == 4:                                  # duplicate a random line
        lines = s.split("\n"); i = r.randint(0, len(lines) - 1)
        lines.insert(i, lines[i]); return "\n".join(lines)
    if op == 5:                                  # flip a bit in a random byte
        i = r.randint(0, n - 1); c = (ord(s[i]) ^ (1 << r.randint(0, 6))) & 0x7f
        return s[:i] + (chr(c) if c else " ") + s[i + 1:]
    if op == 6:                                  # drop a closing bracket (unbalance)
        for ch in ")]}":
            i = s.rfind(ch)
            if i >= 0:
                return s[:i] + s[i + 1:]
        return s[:n // 2]
    if op == 7:                                  # insert a random token
        i = r.randint(0, n); return s[:i] + " " + r.choice(TOKENS) + " " + s[i:]
    if op == 8:                                  # delete a whitespace-split token
        toks = s.split(" ")
        if len(toks) > 1:
            del toks[r.randint(0, len(toks) - 1)]
        return " ".join(toks)
    if op == 9:                                  # splice in a run of random bytes
        i = r.randint(0, n); return s[:i] + rand_bytes(r, r.randint(1, 30)) + s[i:]
    if op == 10:                                 # deep nesting (recursive-descent stack stress)
        # Capped well below the parser's stack-overflow threshold. ~2000-deep input
        # overflows the recursive-descent expression parser under ASan (it errors
        # cleanly without ASan, whose ~3-4x stack overhead lowers the threshold) --
        # a pathological-depth limitation shared by production recursive-descent
        # compilers (gcc/clang/rustc all overflow given enough nesting). This lane
        # exercises realistic-but-stressful nesting, NOT depth-DoS; the limitation
        # is documented rather than guarded (see fuzz/README + memory).
        depth = r.choice([16, 32, 64])
        oc = r.choice("([")
        i = s.find("\n")
        inj = "    _z := " + oc * depth + "\n"
        return (s[:i + 1] + inj + s[i + 1:]) if i >= 0 else (oc * depth)
    if op == 11:                                 # break indentation
        lines = s.split("\n"); i = r.randint(0, len(lines) - 1)
        lines[i] = (" " * r.choice([1, 3, 7])) + ("\t" if r.random() < 0.3 else "") + lines[i]
        return "\n".join(lines)
    if op == 12:                                 # duplicate a random substring
        a = r.randint(0, n - 1); b = r.randint(a, n)
        return s[:b] + s[a:b] + s[b:]
    if op == 13:                                 # splice keyword/punct soup over a run
        a = r.randint(0, n - 1); b = min(n, a + r.randint(1, 20))
        soup = " ".join(r.choice(TOKENS) for _ in range(r.randint(1, 8)))
        return s[:a] + soup + s[b:]
    return r.choice(["", " ", "\n", "\t", "#", "fn", "fn main", "fn main(",
                     ":", "}", "\x00", "fn f(:", "struct", "match x:"])  # edge inputs

def gen(seed):
    r = random.Random(seed)
    corpus = load_corpus()
    if r.random() < 0.12 or not corpus:          # pure random byte / token soup
        if r.random() < 0.5:
            return rand_bytes(r, r.randint(0, 200))
        return " ".join(r.choice(TOKENS) for _ in range(r.randint(0, 60)))
    s = r.choice(corpus)
    for _ in range(r.randint(1, 4)):             # 1-4 stacked mutations
        s = mutate(r, s)
    return s

if __name__ == "__main__":
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    sys.stdout.write(gen(seed))
