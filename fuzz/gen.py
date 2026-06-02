#!/usr/bin/env python3
# Type-directed random Hier program generator for soundness fuzzing.
# Emits a well-typed, deterministic, terminating program that stresses the
# arena/value-semantics paths: copy-binds (`b := a`), heap built in loops/blocks,
# pushes, struct/array nesting, returns, inout, match. It accumulates an int
# checksum across all the state it builds and prints it once at the end, so two
# correct compilers must produce byte-identical output (the differential oracle).
#
# Usage: gen.py <seed>   -> a .hi program on stdout
import sys, random

SCALARS = ["int", "string"]
def arr(t): return "[" + t + "]"

class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.out = []
        self.uid = 0
        self.structs = {}   # name -> [(field, type)]
        self.enums = {}     # name -> leaf payload type ("int"/"string")
        self.funcs = []     # list of (name, [(pname,ptype,inout)], ret)

    def fresh(self, p="v"):
        self.uid += 1
        return p + str(self.uid)

    def emit(self, ind, s):
        self.out.append("    " * ind + s)

    # ---- types ---------------------------------------------------------
    def types_simple(self):
        ts = ["int", "string", "[int]", "[string]", "Option(int)", "(int, string)", "{string:int}", "[Option(int)]"]
        ts += list(self.structs.keys())
        return ts

    def is_heap(self, t):
        return (t == "string" or t.startswith("[") or t.startswith("(") or t.startswith("{")
                or t == "Option(int)" or t in self.structs or t in self.enums)

    # ---- expressions (type-directed) -----------------------------------
    # env: dict name->type. Returns a string expression of type `t`.
    def gen_expr(self, t, env, depth=0):
        choices = []
        vars_t = [n for n, ty in env.items() if ty == t]
        if vars_t:
            choices.append(("var", lambda: self.r.choice(vars_t)))
        if t == "int":
            choices.append(("lit", lambda: str(self.r.randint(0, 9))))
            if depth < 2:
                choices.append(("add", lambda: "(" + self.gen_expr("int", env, depth+1) + " + " + self.gen_expr("int", env, depth+1) + ")"))
            # len of an array var
            av = [n for n, ty in env.items() if ty.startswith("[")]
            if av:
                choices.append(("len", lambda: "len(" + self.r.choice(av) + ")"))
            sv = [n for n, ty in env.items() if ty == "string"]
            if sv:
                choices.append(("slen", lambda: "len(" + self.r.choice(sv) + ")"))
        elif t == "string":
            choices.append(("lit", lambda: '"' + self.r.choice(["a","bb","ccc","",">"]) + '"'))
            if depth < 2:
                choices.append(("cat", lambda: "(" + self.gen_expr("string", env, depth+1) + " + " + self.gen_expr("string", env, depth+1) + ")"))
                choices.append(("s", lambda: "str(" + self.gen_expr("int", env, depth+1) + ")"))
        elif t.startswith("["):
            el = t[1:-1]
            def mkel(j):                                    # first option-array elem must be Some (None infers its type)
                if el == "Option(int)" and j > 0 and self.r.random() < 0.4:
                    return "None"
                return self.gen_expr(el, env, 2)
            choices.append(("lit", lambda: "[" + ", ".join(mkel(j) for j in range(self.r.randint(1,3))) + "]"))
        elif t == "Option(int)":
            choices.append(("some", lambda: "Some(" + self.gen_expr("int", env, 2) + ")"))
        elif t == "(int, string)":
            choices.append(("tup", lambda: "(" + self.gen_expr("int", env, 2) + ", " + self.gen_expr("string", env, 2) + ")"))
        elif t == "{string:int}":
            choices.append(("map", lambda: "[" + ", ".join('"k' + str(j) + '": ' + self.gen_expr("int", env, 2) for j in range(self.r.randint(1,3))) + "]"))
        elif t in self.structs:
            fields = self.structs[t]
            choices.append(("ctor", lambda: t + "(" + ", ".join(self.gen_expr(ft, env, 2) for _, ft in fields) + ")"))
        if not choices:
            # fallback: a literal of t
            return self.fallback_lit(t, env)
        return self.r.choice(choices)[1]()

    def fallback_lit(self, t, env):
        if t == "int": return str(self.r.randint(0,9))
        if t == "string": return '"x"'
        if t == "Option(int)": return "Some(0)"
        if t == "(int, string)": return '(0, "x")'
        if t == "{string:int}": return '["k0": 0]'
        if t.startswith("["):
            el = t[1:-1]
            return "[" + self.fallback_lit(el, env) + "]"
        if t in self.structs:
            return t + "(" + ", ".join(self.fallback_lit(ft, env) for _, ft in self.structs[t]) + ")"
        return "0"

    # ---- contribute a value's checksum into `acc` ----------------------
    def checksum_into(self, ind, name, t, env):
        if t == "int":
            self.emit(ind, "acc = acc + " + name)
        elif t == "string":
            self.emit(ind, "acc = acc + len(" + name + ")")
        elif t.startswith("[") and t[1:-1] == "int":
            i = self.fresh("i")
            self.emit(ind, "for " + i + " in range(len(" + name + ")):")
            self.emit(ind+1, "acc = acc + " + name + "[" + i + "]")
        elif t.startswith("[") and t[1:-1] == "string":
            i = self.fresh("i")
            self.emit(ind, "for " + i + " in range(len(" + name + ")):")
            self.emit(ind+1, "acc = acc + len(" + name + "[" + i + "])")
        elif t == "Option(int)":
            self.emit(ind, "match " + name + ":")
            self.emit(ind+1, "Some(x):")
            self.emit(ind+2, "acc = acc + x")
            self.emit(ind+1, "None:")
            self.emit(ind+2, "acc = acc + 0")
        elif t == "(int, string)":
            self.emit(ind, "acc = acc + " + name + ".0")
            self.emit(ind, "acc = acc + len(" + name + ".1)")
        elif t == "{string:int}":
            ks = self.fresh("k"); ii = self.fresh("i")
            self.emit(ind, ks + " := keys(" + name + ")")
            self.emit(ind, "for " + ii + " in range(len(" + ks + ")):")
            self.emit(ind+1, "acc = acc + map_get(" + name + ", " + ks + "[" + ii + "], 0)")
        elif t == "[Option(int)]":
            ii = self.fresh("i")
            self.emit(ind, "for " + ii + " in range(len(" + name + ")):")
            self.emit(ind+1, "match " + name + "[" + ii + "]:")
            self.emit(ind+2, "Some(x):")
            self.emit(ind+3, "acc = acc + x")
            self.emit(ind+2, "None:")
            self.emit(ind+3, "acc = acc + 0")
        elif t in self.structs:
            for f, ft in self.structs[t]:
                self.checksum_into(ind, name + "." + f, ft, env)
        elif t in self.enums:
            self.emit(ind, "acc = acc + sum" + t + "(" + name + ")")

    # ---- statements ----------------------------------------------------
    def gen_stmt(self, ind, env, budget):
        kinds = ["decl", "copybind", "checksum"]
        heap_vars = [(n, ty) for n, ty in env.items() if self.is_heap(ty)]
        arr_vars = [(n, ty) for n, ty in env.items() if ty.startswith("[")]
        struct_vars = [(n, ty) for n, ty in env.items() if ty in self.structs]
        if heap_vars: kinds.append("copybind")
        if arr_vars: kinds += ["push", "push"]
        if struct_vars: kinds.append("fieldmut")
        if budget > 2: kinds += ["loop", "if"]
        if self.enums: kinds += ["enum_use", "enum_use"]
        kinds += ["inout_fill", "call_ret"]
        k = self.r.choice(kinds)

        if k == "enum_use":
            en = self.r.choice(list(self.enums.keys()))
            n = self.fresh("e")
            self.emit(ind, n + " := build" + en + "(" + str(self.r.randint(1, 4)) + ")")
            env[n] = en
            self.emit(ind, "acc = acc + sum" + en + "(" + n + ")")
            return
        if k == "inout_fill":                       # mutate a caller array through an inout param (_ina_)
            a = self.fresh("a")
            self.emit(ind, a + " := []int")
            env[a] = "[int]"
            self.emit(ind, "fillA(&" + a + ", " + str(self.r.randint(1, 6)) + ")")
            return
        if k == "call_ret":                         # bind a heap value returned from a helper (return-slot)
            a = self.fresh("a")
            self.emit(ind, a + " := mkarr(" + str(self.r.randint(1, 6)) + ")")
            env[a] = "[int]"
            return

        if k == "decl":
            t = self.r.choice(self.types_simple())
            n = self.fresh()
            self.emit(ind, n + " := " + self.gen_expr(t, env, 0))
            env[n] = t
        elif k == "copybind" and heap_vars:
            n0, t0 = self.r.choice(heap_vars)   # value-semantics stressor: b := a (deep copy / move)
            n = self.fresh()
            self.emit(ind, n + " := " + n0)
            env[n] = t0
        elif k == "push" and arr_vars:
            n0, t0 = self.r.choice(arr_vars)
            el = t0[1:-1]
            self.emit(ind, "push(" + n0 + ", " + self.gen_expr(el, env, 1) + ")")
        elif k == "fieldmut" and struct_vars:
            n0, t0 = self.r.choice(struct_vars)
            f, ft = self.r.choice(self.structs[t0])
            self.emit(ind, n0 + "." + f + " = " + self.gen_expr(ft, env, 1))
        elif k == "loop" and budget > 2:
            i = self.fresh("i")
            self.emit(ind, "for " + i + " in range(" + str(self.r.randint(1,4)) + "):")
            benv = dict(env); benv[i] = "int"
            self.gen_block(ind+1, benv, budget-2)
        elif k == "if" and budget > 2:
            self.emit(ind, "if " + self.gen_expr("int", env, 1) + " < " + str(self.r.randint(1,5)) + ":")
            self.gen_block(ind+1, dict(env), budget-2)
        else:  # checksum a random in-scope var
            cand = list(env.items())
            if cand:
                n0, t0 = self.r.choice(cand)
                self.checksum_into(ind, n0, t0, env)

    def gen_block(self, ind, env, budget):
        n = self.r.randint(1, max(1, budget))
        emitted = False
        for _ in range(n):
            before = len(self.out)
            self.gen_stmt(ind, env, budget)
            if len(self.out) > before: emitted = True
        if not emitted:
            self.emit(ind, "acc = acc + 1")

    # ---- top level -----------------------------------------------------
    def gen_struct(self):
        name = "S" + str(len(self.structs))
        nf = self.r.randint(1, 3)
        fields = []
        pool = ["int", "string", "[int]", "[string]"]
        for j in range(nf):
            fields.append(("f" + str(j), self.r.choice(pool)))
        self.structs[name] = fields

    def emit_enum(self, name, leaf_t):
        L, N = "L_" + name, "N_" + name
        self.out += ["enum " + name + ":", "    " + L + "(" + leaf_t + ")", "    " + N + "(" + name + ", " + name + ")", ""]
        leaf_lit = "d" if leaf_t == "int" else 'str(d)'
        self.out += ["fn build" + name + "(d: int) -> " + name + ":",
                     "    if d < 1:",
                     "        return " + L + "(" + leaf_lit + ")",
                     "    return " + N + "(build" + name + "(d - 1), build" + name + "(d - 1))", ""]
        xcontrib = "x" if leaf_t == "int" else "len(x)"
        self.out += ["fn sum" + name + "(t: " + name + ") -> int:",
                     "    match t:",
                     "        " + L + "(x):",
                     "            return " + xcontrib,
                     "        " + N + "(l, r):",
                     "            return (sum" + name + "(l) + sum" + name + "(r))", ""]

    def emit_helpers(self):
        self.out += ["fn fillA(xs: inout [int], n: int):", "    for i in range(n):", "        push(xs, i)", ""]
        self.out += ["fn mkarr(n: int) -> [int]:", "    r := []int", "    for i in range(n):", "        push(r, (i + 1))", "    return r", ""]

    def generate(self):
        for _ in range(self.r.randint(0, 2)):
            self.gen_struct()
        for _ in range(self.r.randint(0, 2)):
            self.enums["E" + str(len(self.enums))] = self.r.choice(["int", "string"])
        # struct defs
        for name, fields in self.structs.items():
            self.out.append("struct " + name + ":")
            for f, ft in fields:
                self.emit(1, f + ": " + ft)
            self.out.append("")
        # enum defs + their recursive build/sum fns, and the fixed helpers
        for name, leaf_t in self.enums.items():
            self.emit_enum(name, leaf_t)
        self.emit_helpers()
        # main
        self.out.append("fn main():")
        self.emit(1, "acc := 0")
        env = {}
        self.gen_block(1, env, self.r.randint(8, 18))
        self.emit(1, 'print(str(acc) + "\\n")')
        return "\n".join(self.out) + "\n"

if __name__ == "__main__":
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    sys.stdout.write(Gen(seed).generate())
