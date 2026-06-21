#!/usr/bin/env python3
# Type-directed random Hier program generator for soundness fuzzing.
# Emits a well-typed, deterministic, terminating program that stresses the
# arena/value-semantics paths: copy-binds (`b := a`), heap built in loops/blocks,
# pushes, struct/array nesting, returns, mut, match. It accumulates an int
# checksum across all the state it builds and prints it once at the end, so two
# correct compilers must produce byte-identical output (the differential oracle).
# It ALSO emits value-semantics self-checks (the `vscheck` kind): copy a heap
# value, mutate the COPY, and assert the ORIGINAL is unchanged, calling die() if
# not. These catch deep-copy/aliasing miscompiles present in BOTH compilers --
# which the hierc-vs-hierc0 differential structurally cannot, since the two would
# agree on the same wrong answer; the self-check turns such a bug into a fault.
# Types covered: int, float, string, char, arrays ([int]/[string]/[float]),
# structs, recursive enums, Option(int), Result([int], string), (int, string)
# tuples, {string:int}/{string:float} maps, `type Nt = int` newtypes, slices,
# SOA core ops (soa []Struct), or_return (Ok-unwrap / Err-propagate).
# Oracle rule: every value reduces into the int checksum (floats/newtypes via
# to_int), and nothing is emitted that can fault at runtime in a valid program
# (e.g. array slices use only whole-array forms, since OOB array slices exit(1)).
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
        self.newtypes = {}  # name -> base type ("int") ; `type Name = int`
        self.funcs = []     # list of (name, [(pname,ptype,mut)], ret)
        self.want_result = False   # set when a Result helper is needed
        # loop counters / foreach vars: must NEVER be written by a generated
        # statement. A compound assign like `i -= i` on a range counter resets
        # it every iteration -> the loop never terminates (latent bug surfaced
        # when run.py stopped counting timeouts as skips).
        self.loop_vars = set()

    def fresh(self, p="v"):
        self.uid += 1
        return p + str(self.uid)

    def emit(self, ind, s):
        self.out.append("    " * ind + s)

    def _kv(self, t):
        # Split a map type "{K:V}" on its TOP-LEVEL colon. V may itself be a map
        # ("{string:{string:int}}") or array, so a plain split(":") is wrong.
        # For a single-colon type this returns exactly what split(":") would.
        s = t[1:-1]; depth = 0
        for i, c in enumerate(s):
            if c in "{[(": depth += 1
            elif c in "}])": depth -= 1
            elif c == ":" and depth == 0: return s[:i], s[i+1:]
        raise ValueError("not a map type: " + t)

    # ---- types ---------------------------------------------------------
    def types_simple(self):
        ts = ["int", "string", "float", "[int]", "[string]", "[float]", "Option(int)",
              "Option(string)", "(int, string)", "{string:int}", "{string:float}",
              "{int:int}", "{int:float}", "{string:string}",   # composite (heap) map value
              "{string:[int]}", "{int:[string]}",               # array-valued maps: the #2 m[k]-place surface
              "{string:{string:int}}",                          # nested-map value: inner map emitted before outer
              "[Option(int)]", "[Option(string)]"]
        ts += list(self.structs.keys())
        ts += list(self.newtypes.keys())
        return ts

    def is_heap(self, t):
        return (t == "string" or t.startswith("[") or t.startswith("(") or t.startswith("{")
                or t.startswith("Option(") or t.startswith("Result(")
                or t in self.structs or t in self.enums)

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
                # modulo / bitwise / shift / unary ~ (int-only). All kept bounded so
                # the int checksum can't overflow (UBSan would flag that): `% L` and
                # `& 7`/`& 31` cap the magnitude; shift amount is 0..3.
                ie = lambda: self.gen_expr("int", env, depth+1)
                choices.append(("mod",  lambda: "(" + ie() + " % " + str(self.r.randint(1, 9)) + ")"))
                # subtraction: same magnitude class as add (operands stay small).
                choices.append(("sub",  lambda: "(" + ie() + " - " + ie() + ")"))
                # multiplication: operands masked (& 31, & 7) so the product is
                # bounded <= 217 -- same keep-it-in-range convention as shl/bnot
                # (UBSan flags signed overflow, see the note above).
                choices.append(("mul",  lambda: "((" + ie() + " & 31) * (" + ie() + " & 7))"))
                # division: nonzero LITERAL divisor 1..9, the same guard pattern
                # mod uses above -- division by zero is impossible by construction.
                choices.append(("div",  lambda: "(" + ie() + " / " + str(self.r.randint(1, 9)) + ")"))
                choices.append(("band", lambda: "(" + ie() + " & " + ie() + ")"))
                choices.append(("bor",  lambda: "(" + ie() + " | " + ie() + ")"))
                choices.append(("bxor", lambda: "(" + ie() + " ^ " + ie() + ")"))
                choices.append(("shl",  lambda: "((" + ie() + " & 7) << " + str(self.r.randint(0, 3)) + ")"))
                choices.append(("shr",  lambda: "(" + ie() + " >> " + str(self.r.randint(0, 3)) + ")"))
                choices.append(("bnot", lambda: "(~(" + ie() + " & 31))"))
                # str(bool): a comparison / and / not -> "true"/"false"; len() is 4 or 5,
                # so the int depends on the bool value AND both compilers must agree on the
                # words (this is the str(bool) / distinct-bool-type differential oracle).
                choices.append(("strcmp", lambda: "len(str(" + ie() + " > " + ie() + "))"))
                choices.append(("strand", lambda: "len(str(" + ie() + " < " + ie() + " and " + ie() + " >= " + ie() + "))"))
                choices.append(("strnot", lambda: "len(str(not (" + ie() + " == " + ie() + ")))"))
                # FFI: extern calls returning int — scalar args, ptr round-trip
                # (handle->unwrap), is_null on a ptr and on the null literal, and
                # str args/returns (slen + the arena-copied echo/nullify lengths).
                se = lambda: self.gen_expr("string", env, depth+1)
                choices.append(("fzaddi",    lambda: "fz_addi(" + ie() + ", " + ie() + ")"))
                choices.append(("fzunwrap",  lambda: "fz_unwrap(fz_handle(" + ie() + "))"))
                choices.append(("fzisnull",  lambda: "len(str(is_null(fz_handle(" + ie() + "))))"))
                choices.append(("fznull",    lambda: "len(str(is_null(null)))"))
                choices.append(("fzslen",    lambda: "fz_slen(" + se() + ")"))
                choices.append(("fzecholen", lambda: "len(fz_echo(" + se() + "))"))
                choices.append(("fznulllen", lambda: "len(fz_nullify(" + se() + "))"))
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
                # FFI: an arena-copied C string return woven into the string flow
                choices.append(("fzecho", lambda: "fz_echo(" + self.gen_expr("string", env, depth+1) + ")"))
        elif t == "float":
            choices.append(("lit", lambda: self.r.choice(["0.0", "1.5", "2.0", "3.25", "4.0"])))
            choices.append(("tof", lambda: "to_float(" + self.gen_expr("int", env, depth+1) + ")"))
            if depth < 2:
                choices.append(("add", lambda: "(" + self.gen_expr("float", env, depth+1) + " + " + self.gen_expr("float", env, depth+1) + ")"))
                choices.append(("fzaddf", lambda: "fz_addf(" + self.gen_expr("float", env, depth+1) + ", " + self.gen_expr("float", env, depth+1) + ")"))
        elif t.startswith("["):
            el = t[1:-1]
            def mkel(j):                                    # first option-array elem must be Some (None infers its type)
                if el.startswith("Option(") and j > 0 and self.r.random() < 0.4:
                    return "None"
                return self.gen_expr(el, env, 2)
            choices.append(("lit", lambda: "[" + ", ".join(mkel(j) for j in range(self.r.randint(1,3))) + "]"))
        elif t.startswith("Option("):
            inner = t[7:-1]                                  # "int" or "string" (heap payload)
            choices.append(("some", lambda: "Some(" + self.gen_expr(inner, env, 2) + ")"))
        elif t == "(int, string)":
            choices.append(("tup", lambda: "(" + self.gen_expr("int", env, 2) + ", " + self.gen_expr("string", env, 2) + ")"))
        elif t.startswith("{") and t.endswith("}"):
            kt, vt = self._kv(t)   # key/value: "string"/"int" keys, "int"/"float" values
            def mkkv(j):                  # distinct keys: "k0".. for string, 0.. for int
                k = '"k' + str(j) + '"' if kt == "string" else str(j)
                return k + ": " + self.gen_expr(vt, env, 2)
            choices.append(("map", lambda: "[" + ", ".join(mkkv(j) for j in range(self.r.randint(1,3))) + "]"))
        elif t in self.structs:
            fields = self.structs[t]
            choices.append(("ctor", lambda: t + "(" + ", ".join(self.gen_expr(ft, env, 2) for _, ft in fields) + ")"))
        elif t in self.newtypes:                            # `type Nt = int`: construct via Nt(intexpr)
            choices.append(("nt", lambda: t + "(" + self.gen_expr(self.newtypes[t], env, 2) + ")"))
        if not choices:
            # fallback: a literal of t
            return self.fallback_lit(t, env)
        return self.r.choice(choices)[1]()

    def fallback_lit(self, t, env):
        if t == "int": return str(self.r.randint(0,9))
        if t == "float": return "1.0"
        if t == "string": return '"x"'
        if t == "Option(int)": return "Some(0)"
        if t == "Option(string)": return 'Some("x")'
        if t == "(int, string)": return '(0, "x")'
        if t.startswith("{") and t.endswith("}"):   # any map, incl. array/nested-map values
            kt, vt = self._kv(t)                     # (byte-identical to the old hardcoded literals)
            return "[" + ('"k0"' if kt == "string" else "0") + ": " + self.fallback_lit(vt, env) + "]"
        if t.startswith("["):
            el = t[1:-1]
            return "[" + self.fallback_lit(el, env) + "]"
        if t in self.structs:
            return t + "(" + ", ".join(self.fallback_lit(ft, env) for _, ft in self.structs[t]) + ")"
        if t in self.newtypes:
            return t + "(" + self.fallback_lit(self.newtypes[t], env) + ")"
        return "0"

    # ---- contribute a value's checksum into `acc` (a named int accumulator,
    # "acc" by default; the value-semantics self-check passes a private temp so
    # it can snapshot the same value twice and compare). -------------------
    def checksum_into(self, ind, name, t, env, acc="acc"):
        add = lambda ind2, e: self.emit(ind2, acc + " = " + acc + " + " + e)
        if t == "int":
            add(ind, name)
        elif t == "float":
            add(ind, "to_int(" + name + ")")   # truncates; identical in both compilers
        elif t in self.newtypes:                                  # unwrap (zero-cost) and checksum as the base
            u = self.fresh("u")
            self.emit(ind, u + " := to_under(" + name + ")")
            env2 = dict(env); env2[u] = self.newtypes[t]
            self.checksum_into(ind, u, self.newtypes[t], env2, acc)
        elif t == "string":
            add(ind, "len(" + name + ")")
        elif t.startswith("[") and t[1:-1] == "int":
            i = self.fresh("i")
            self.emit(ind, "for " + i + " in range(len(" + name + ")):")
            add(ind+1, name + "[" + i + "]")
        elif t.startswith("[") and t[1:-1] == "float":
            i = self.fresh("i")
            self.emit(ind, "for " + i + " in range(len(" + name + ")):")
            add(ind+1, "to_int(" + name + "[" + i + "])")
        elif t.startswith("[") and t[1:-1] == "string":
            i = self.fresh("i")
            self.emit(ind, "for " + i + " in range(len(" + name + ")):")
            add(ind+1, "len(" + name + "[" + i + "])")
        elif t.startswith("Option(") and t.endswith(")"):
            inner = t[7:-1]                                  # "int" or "string"
            contrib = "x" if inner == "int" else "len(x)"
            self.emit(ind, "match " + name + ":")
            self.emit(ind+1, "Some(x):")
            add(ind+2, contrib)
            self.emit(ind+1, "None:")
            add(ind+2, "0")
        elif t == "(int, string)":
            add(ind, name + ".0")
            add(ind, "len(" + name + ".1)")
        elif t.startswith("{") and t.endswith("}"):   # {string|int : int|float}
            kt, vt = self._kv(t)
            ks = self.fresh("k"); ii = self.fresh("i")
            self.emit(ind, ks + " := keys(" + name + ")")   # [string] or [int] keys
            self.emit(ind, "for " + ii + " in range(len(" + ks + ")):")
            add(ind+1, "len(str(map_has(" + name + ", " + ks + "[" + ii + "])))")   # str(bool): present key -> "true"
            if vt.startswith("["):                  # composite (array) value: bind the borrow + recurse
                gv = self.fresh("gv")
                self.emit(ind+1, gv + " := map_get(" + name + ", " + ks + "[" + ii + "], []" + vt[1:-1] + ")")
                self.checksum_into(ind+1, gv, vt, env, acc)
            elif vt.startswith("{"):                # nested-map value: bind the borrow + recurse
                nkt, nvt = self._kv(vt)
                gv = self.fresh("gv")
                self.emit(ind+1, gv + " := map_get(" + name + ", " + ks + "[" + ii + "], []" + nkt + ": " + nvt + ")")
                self.checksum_into(ind+1, gv, vt, env, acc)
            else:
                dflt = '""' if vt == "string" else ("0" if vt == "int" else "0.0")
                g = "map_get(" + name + ", " + ks + "[" + ii + "], " + dflt + ")"
                contrib = g if vt == "int" else ("len(" + g + ")" if vt == "string" else "to_int(" + g + ")")
                add(ind+1, contrib)   # SUM is key-order independent
        elif t == "Result([int], string)":                # heap Ok payload + Err string, matched both arms
            xs = self.fresh("xs"); er = self.fresh("e"); ii = self.fresh("i")
            self.emit(ind, "match " + name + ":")
            self.emit(ind+1, "Ok(" + xs + "):")
            self.emit(ind+2, "for " + ii + " in range(len(" + xs + ")):")
            add(ind+3, xs + "[" + ii + "]")
            self.emit(ind+1, "Err(" + er + "):")
            add(ind+2, "len(" + er + ")")
        elif t.startswith("[Option(") and t.endswith(")]"):
            inner = t[8:-2]                                  # "int" or "string"
            contrib = "x" if inner == "int" else "len(x)"
            ii = self.fresh("i")
            self.emit(ind, "for " + ii + " in range(len(" + name + ")):")
            self.emit(ind+1, "match " + name + "[" + ii + "]:")
            self.emit(ind+2, "Some(x):")
            add(ind+3, contrib)
            self.emit(ind+2, "None:")
            add(ind+3, "0")
        elif t in self.structs:
            for f, ft in self.structs[t]:
                self.checksum_into(ind, name + "." + f, ft, env, acc)
        elif t in self.enums:
            add(ind, "sum" + t + "(" + name + ")")

    # ---- statements ----------------------------------------------------
    def gen_stmt(self, ind, env, budget):
        kinds = ["decl", "copybind", "checksum"]
        heap_vars = [(n, ty) for n, ty in env.items() if self.is_heap(ty)]
        arr_vars = [(n, ty) for n, ty in env.items() if ty.startswith("[")]
        struct_vars = [(n, ty) for n, ty in env.items() if ty in self.structs]
        str_vars = [n for n, ty in env.items() if ty == "string"]
        slice_vars = [(n, ty) for n, ty in env.items() if ty == "string" or (ty.startswith("[") and ty[1:-1] in ("int", "string", "float"))]
        if heap_vars: kinds.append("copybind")
        if arr_vars: kinds += ["push", "push"]
        if struct_vars: kinds.append("fieldmut")
        if slice_vars: kinds.append("slice")
        if str_vars: kinds += ["char_append", "char_append"]
        if budget > 2: kinds += ["loop", "if"]
        if self.enums: kinds += ["enum_use", "enum_use"]
        kinds += ["inout_fill", "call_ret", "result_use", "soa_use", "orret_use", "opt_res_eq", "map_in"]
        # constructs hierc0 historically miscompiled -- keep the class covered:
        kinds += ["multiassign", "negrange", "matchpop", "orret_loop"]
        # self-referential shadow decls (`x := f(x)` in a nested scope) -- the RHS
        # reads the ENCLOSING binding, so codegen must evaluate it into a temp
        # before the new local exists. hierc once segfaulted on the lambda-capture
        # variant (captures live on the lifted proc, not in lhs/rhs/args).
        kinds += ["shadow", "shadow"]
        # concurrency (CC-1..5) + bidirectional inference (B-0..3) coverage.
        # All deterministic by construction: int reductions and channel SUMS are
        # thread-count/interleaving independent, so the differential oracle holds.
        # Conc kinds only at the function top level (ind == 1): inside generated
        # NESTED loops they multiply OS-thread creation (a parallel for spawns
        # ncpu threads per iteration) into wall-clock timeouts on both sides.
        if ind <= 1:
            kinds += ["spawn_use", "parfor_use", "chan_use"]
        kinds += ["infer_ground", "infer_lambda", "float_adapt"]
        if any(b == "string" for b in self.newtypes.values()):
            kinds += ["ntkey_use"]                  # newtype-keyed map ([Nt: int])
        if self.fenum:
            kinds += ["enumkey_use"]                # fieldless-enum-keyed map ([FzColor: int])
        if str_vars:
            kinds += ["inout_str"]                  # mut string: reassignment through the borrow
        # value-semantics self-check candidates: a mutable-heap var with a
        # checksum-changing mutation (push to an array, or to a struct's array field).
        vs_arr = [(n, ty) for n, ty in env.items() if ty in ("[int]", "[string]", "[float]")]
        vs_struct = [(n, ty) for n, ty in env.items() if ty in self.structs
                     and any(ft.startswith("[") for _, ft in self.structs[ty])]
        vs_map = [(n, ty) for n, ty in env.items() if ty.startswith("{")]   # #2: in-place m[k] must not alias a copy
        if vs_arr or vs_struct or vs_map:
            kinds += ["vscheck", "vscheck"]
        if vs_arr:                                 # pop the last element (guarded), fold it into the checksum
            kinds += ["pop"]
        int_arr_vars = [n for n, ty in env.items() if ty == "[int]"]
        if int_arr_vars:
            kinds += ["arr_rebuild", "arr_realloc", "arr_slice"]   # liveness-driven buffer recycle
        # new surface: compound assignment, foreach, in-place map accumulator
        int_vars = [n for n, ty in env.items() if ty == "int"]
        map_vars = [(n, ty) for n, ty in env.items() if ty.startswith("{")]
        cmp_struct = [(n, ty) for n, ty in env.items()
                      if ty in self.structs and any(ft == "int" for _, ft in self.structs[ty])]
        if int_vars or int_arr_vars or cmp_struct:
            kinds += ["compound", "compound"]
        if arr_vars or str_vars:
            kinds += ["foreach", "foreach"]
        if int_vars or int_arr_vars or str_vars:
            kinds += ["closure", "closure"]            # lambda/closure: capture by value, call, fold into the checksum
        if int_vars or int_arr_vars:
            kinds += ["escclosure"]                    # ESCAPING closure: bind one returned from a factory, call it later
        if int_vars:
            kinds += ["fncarr"]                        # fn values in a container + call-on-expression (call array elements)
        if int_vars:
            kinds += ["fstring"]                       # f-string interpolation -> the str()-concat desugaring
        if map_vars:
            kinds += ["map_accum", "map_place", "map_place"]   # #2: m[k] as a place
        res_vars = [n for n, ty in env.items() if ty in ("[int]", "[string]", "[float]")]
        if res_vars:
            kinds += ["reserve"]
        k = self.r.choice(kinds)

        if k == "reserve":             # reserve(arr, n): capacity hint, behavior-neutral.
            # Reserving below/above the current length must not change contents,
            # length, or the checksum -- the differential + ASan/LSan + vscheck
            # catch any miscompile (bad copy on regrow, recycle corruption).
            self.emit(ind, "reserve(" + self.r.choice(res_vars) + ", " + str(self.r.randint(0, 9)) + ")")
            return

        if k == "map_in":              # B5.1: `k in m` membership on string- and int-keyed maps, folded into acc
            sm = self.fresh("im")
            self.emit(ind, sm + " : [string: int] = []")
            self.emit(ind, sm + "[\"a\"] = " + str(self.r.randint(1, 9)))
            self.emit(ind, "if \"a\" in " + sm + ":")        # present
            self.emit(ind+1, "acc = acc + 1")
            self.emit(ind, "if not \"q\" in " + sm + ":")    # absent, via `not`
            self.emit(ind+1, "acc = acc + 2")
            im = self.fresh("im")
            self.emit(ind, im + " : [int: int] = []")
            self.emit(ind, im + "[" + str(self.r.randint(0, 5)) + "] = 1")
            self.emit(ind, "if " + str(self.r.randint(0, 5)) + " in " + im + ":")   # int key, sometimes present
            self.emit(ind+1, "acc = acc + 4")
            self.emit(ind, "delete " + sm + "[\"a\"]")       # B5.2: delete then re-check (len + membership, not key order)
            self.emit(ind, "if not \"a\" in " + sm + ":")
            self.emit(ind+1, "acc = acc + 8")
            self.emit(ind, "acc = acc + len(" + sm + ")")
            return
        if k == "ntkey_use":           # newtype-keyed map: declared key (a raw base is a type error),
            # base hashing/storage, keys() returns the WRAPPED key array, m[k] is a place.
            n0 = self.r.choice([n for n, b in self.newtypes.items() if b == "string"])
            m = self.fresh("nm")
            self.emit(ind, m + " : [" + n0 + ": int] = []")
            self.emit(ind, m + " = map_set(" + m + ", " + n0 + "(\"a\"), " + self.gen_expr("int", env, 2) + ")")
            self.emit(ind, m + "[" + n0 + "(\"b\")] = " + str(self.r.randint(0, 9)))
            self.emit(ind, "acc = acc + map_get(" + m + ", " + n0 + "(\"a\"), 0) + len(" + m + ")")
            if self.r.random() < 0.5:
                self.emit(ind, "if map_has(" + m + ", " + n0 + "(\"b\")):")
                self.emit(ind+1, "acc = acc + 1")
            ks = self.fresh("ks")
            self.emit(ind, ks + " := keys(" + m + ")")
            i = self.fresh("i")
            self.emit(ind, "for " + i + " in range(len(" + ks + ")):")
            self.emit(ind+1, "acc = acc + len(to_under(" + ks + "[" + i + "]))")
            return
        if k == "enumkey_use":         # fieldless-enum key: stored as its TAG, keys() rebuilds the
            # wrapped singletons (which must round-trip through match and map_get)
            m = self.fresh("em")
            self.emit(ind, m + " : [FzColor: int] = []")
            self.emit(ind, m + " = map_set(" + m + ", FzA, " + self.gen_expr("int", env, 2) + ")")
            self.emit(ind, m + "[FzB] = " + str(self.r.randint(0, 9)))
            self.emit(ind, "acc = acc + map_get(" + m + ", FzA, 0) + map_get(" + m + ", FzB, 0) + len(" + m + ")")
            if self.r.random() < 0.5:
                self.emit(ind, m + " = map_del(" + m + ", FzA)")
                self.emit(ind, "acc = acc + len(" + m + ")")
            ks = self.fresh("ek")
            self.emit(ind, ks + " := keys(" + m + ")")
            i = self.fresh("i")
            self.emit(ind, "for " + i + " in range(len(" + ks + ")):")
            self.emit(ind+1, "match " + ks + "[" + i + "]:")
            self.emit(ind+2, "FzA:")
            self.emit(ind+3, "acc = acc + 1")
            self.emit(ind+2, "FzB:")
            self.emit(ind+3, "acc = acc + 2")
            self.emit(ind+2, "FzC:")
            self.emit(ind+3, "acc = acc + 3")
            return
        if k == "inout_str":           # callee reassigns the caller's string through the borrow;
            # the new bytes must land in the CALLER's arena (dangle -> ASan)
            cands = [n for n in str_vars if n not in self.loop_vars]
            if not cands:
                self.emit(ind, "acc = acc + 0")
                return
            sv = self.r.choice(cands)
            self.emit(ind, "fz_sgrow(&" + sv + ", " + str(self.r.randint(1, 4)) + ")")
            self.emit(ind, "acc = acc + len(" + sv + ")")
            return

        if k == "compound":            # x op= e on a var / array element / struct field
            op = self.r.choice(["+=", "-=", "&=", "|=", "^="])
            rhs = self.r.choice([str(self.r.randint(1, 5)), self.gen_expr("int", env, 1)])
            if self.r.random() < 0.25:                          # modulo: safe literal divisor
                op = "%="; rhs = str(self.r.randint(1, 9))
            targets = [("var", n) for n in int_vars if n not in self.loop_vars]
            targets += [("arr", n) for n in int_arr_vars]
            targets += [("fld", n, ty) for n, ty in cmp_struct]
            if not targets:                                 # only loop counters in scope
                self.emit(ind, "acc += " + str(self.r.randint(1, 5)))
                return
            pick = self.r.choice(targets)
            if pick[0] == "var":
                self.emit(ind, pick[1] + " " + op + " " + rhs)
            elif pick[0] == "arr":                              # element compound: the double-eval path
                self.emit(ind, "if len(" + pick[1] + ") >= 1:")  # guard OOB (else exit 1)
                self.emit(ind+1, pick[1] + "[0] " + op + " " + rhs)
            else:
                f = self.r.choice([f for f, ft in self.structs[pick[2]] if ft == "int"])
                self.emit(ind, pick[1] + "." + f + " " + op + " " + rhs)
            return
        if k == "foreach":             # for x in <array|string>: checksum each element/byte
            if arr_vars and (not str_vars or self.r.random() < 0.6):
                n0, t0 = self.r.choice(arr_vars)
                el = t0[1:-1]; x = self.fresh("x")
                self.loop_vars.add(x)    # conservative: foreach var is read-only too
                self.emit(ind, "for " + x + " in " + n0 + ":")
                benv = dict(env); benv[x] = el
                self.checksum_into(ind+1, x, el, benv)
            else:
                x = self.fresh("c")
                self.emit(ind, "for " + x + " in " + self.r.choice(str_vars) + ":")
                self.emit(ind+1, "acc = acc + " + x)            # x is a byte (int 0..255)
            return
        if k == "shadow":              # self-referential shadow decl in a NESTED scope.
            # `x := f(x)` rebinds x reading the ENCLOSING x (Go/Odin lexical scope +
            # value semantics) and binds a fresh value -- codegen evaluates the RHS
            # into a temp BEFORE the new local is in scope, else it reads itself
            # uninitialised (use-before-init UB). Same-scope redeclare is rejected,
            # so always wrap in an (always-true) if. All deterministic, so the
            # differential oracle holds; a miscompile crashes one side or skews acc.
            r = self.r.random()
            if r < 0.4:                                     # scalar int (plain + has-call paths)
                v = self.fresh("sh")
                self.emit(ind, v + " := " + self.gen_expr("int", env, 1))
                self.emit(ind, "if 1 > 0:")
                if self.r.random() < 0.5:                   # plain SDecl path
                    self.emit(ind+1, v + " := " + v + " + " + str(self.r.randint(1, 9)))
                else:                                       # has-call -> scalar-transient path
                    self.emit(ind+1, v + " := len(str(" + v + " + " + str(self.r.randint(0, 9)) + "))")
                self.emit(ind+1, "acc = acc + " + v)
            elif r < 0.7:                                   # heap: self-append string shadow
                v = self.fresh("ss")
                self.emit(ind, v + " := str(" + self.gen_expr("int", env, 1) + ")")
                self.emit(ind, "if 1 > 0:")
                self.emit(ind+1, v + " := " + v + " + " + self.r.choice(['"a"', '"bb"', '"."']))
                self.emit(ind+1, "acc = acc + len(" + v + ")")
            else:                                           # lambda-CAPTURE self-ref (the fixed segfault)
                f = self.fresh("shf")
                self.emit(ind, f + " := fn(p: int) -> int: p + " + str(self.r.randint(0, 5)))
                self.emit(ind, "if 1 > 0:")
                self.emit(ind+1, f + " := fn(p: int) -> int: " + f + "(p) + " + str(self.r.randint(1, 9)))
                self.emit(ind+1, "acc = acc + " + f + "(" + str(self.r.randint(0, 5)) + ")")
            return
        if k == "closure":             # a lambda value (closure): capture BY VALUE, call, fold into the checksum.
            # The mutate-then-call variants are value-semantics probes: a closure
            # captures a COPY, so the result must not see the later mutation. hierc
            # and hierc0 must agree (differential) and ASan/LSan must stay clean
            # (the env lives in the function arena, freed on return; never escapes).
            r = self.r.random()
            f = self.fresh("cl")
            if int_vars and r < 0.5:                            # scalar capture
                cap = self.r.choice(int_vars)
                self.emit(ind, f + " := fn(x: int) -> int: x + " + cap)
                if self.r.random() < 0.5:                       # mutate AFTER capture -> closure keeps the old value
                    self.emit(ind, cap + " = " + cap + " + 7")
                self.emit(ind, "acc = acc + " + f + "(" + str(self.r.randint(0, 9)) + ")")
            elif int_arr_vars and (r < 0.8 or not str_vars):    # heap capture (array); len is value-semantic
                cap = self.r.choice(int_arr_vars)
                self.emit(ind, f + " := fn() -> int: len(" + cap + ")")
                if self.r.random() < 0.5:                       # push AFTER capture -> closure keeps its len-N copy
                    self.emit(ind, "push(" + cap + ", 1)")
                self.emit(ind, "acc = acc + " + f + "()")
            elif str_vars:                                      # heap capture (string)
                cap = self.r.choice(str_vars)
                self.emit(ind, f + " := fn() -> int: len(" + cap + ")")
                self.emit(ind, "acc = acc + " + f + "()")
            else:
                self.emit(ind, "acc = acc + 0")
            return
        if k == "escclosure":          # bind a closure RETURNED from a factory (its env re-homed into our arena), call it later.
            f = self.fresh("ec")
            if int_vars and (not int_arr_vars or self.r.random() < 0.5):
                self.emit(ind, f + " := mkadder(" + self.r.choice(int_vars) + ")")   # scalar capture escaped
            else:
                self.emit(ind, f + " := mksum(" + self.r.choice(int_arr_vars) + ")")  # heap capture escaped + re-homed
            self.emit(ind, "acc = acc + " + f + "(" + str(self.r.randint(0, 9)) + ")")
            return
        if k == "fncarr":              # fn values stored in an array, then CALLED by index (call-on-expression).
            # mixes a ref, a lambda, and an escaped closure (mkadder); when the array
            # is built its closure env re-homes. Both compilers must agree (differential)
            # and ASan/LSan must stay clean (the array + envs live in our arena).
            a = self.fresh("fa")
            cap = self.r.choice(int_vars)
            self.emit(ind, a + " := [mkadder(" + cap + "), fn(x: int) -> int: x + 1]")
            self.emit(ind, "acc = acc + " + a + "[0](" + str(self.r.randint(0, 9)) + ") + " + a + "[1](" + str(self.r.randint(0, 9)) + ")")
            return
        if k == "map_accum":           # m = map_set(m, k, v): in-place map accumulator (is_self_mapset)
            n0, t0 = self.r.choice(map_vars)
            kt, vt = self._kv(t0)
            key = '"k' + str(self.r.randint(0, 4)) + '"' if kt == "string" else str(self.r.randint(0, 4))
            self.emit(ind, n0 + " = map_set(" + n0 + ", " + key + ", " + self.gen_expr(vt, env, 1) + ")")
            return
        if k == "map_place":           # m[k] as a place (#2): assign or scalar compound, fresh + existing keys
            n0, t0 = self.r.choice(map_vars)
            kt, vt = self._kv(t0)
            key = '"k' + str(self.r.randint(0, 4)) + '"' if kt == "string" else str(self.r.randint(0, 4))
            if vt.startswith("["):                                 # array-valued map: grow the value list in place
                el = vt[1:-1]; r = self.r.random()
                if r < 0.55:                                       # push into the map value (fresh key auto-inserts [])
                    self.emit(ind, "push(" + n0 + "[" + key + "], " + self.gen_expr(el, env, 2) + ")")
                elif r < 0.75:                                     # reserve into the map value (capacity hint)
                    self.emit(ind, "reserve(" + n0 + "[" + key + "], " + str(self.r.randint(0, 6)) + ")")
                else:                                              # replace the whole value list
                    self.emit(ind, n0 + "[" + key + "] = " + self.gen_expr(vt, env, 1))
            elif vt in ("int", "float") and self.r.random() < 0.5:  # read-modify-write the scalar value slot
                op = self.r.choice(["+=", "-="])
                rhs = str(self.r.randint(1, 5)) if vt == "int" else str(self.r.randint(1, 5)) + ".0"
                self.emit(ind, n0 + "[" + key + "] " + op + " " + rhs)
            else:                                                   # plain assign (auto-inserts a fresh key)
                self.emit(ind, n0 + "[" + key + "] = " + self.gen_expr(vt, env, 1))
            return

        if k == "arr_rebuild":         # reassign an array from a call that READS it
            n0 = self.r.choice(int_arr_vars)
            self.emit(ind, n0 + " = xform(" + n0 + ")")
            return
        if k == "arr_slice":           # reassign from a non-call RHS (a = a[:]) -> axis-2 recycle
            n0 = self.r.choice(int_arr_vars)   # whole-array slice: always in bounds, value-preserving
            self.emit(ind, n0 + " = " + n0 + "[:]")
            return
        if k == "arr_realloc":         # reassign an array from a call that does NOT read it.
            n0 = self.r.choice(int_arr_vars)   # combined with an earlier `b := n0` copybind this
            self.emit(ind, n0 + " = mkarr(" + str(self.r.randint(1, 6)) + ")")  # is the move-alias case:
            return                             # recycling n0's old buffer must not touch b's (shared via move)

        if k == "vscheck":
            # Copy a heap value, mutate the COPY, assert the ORIGINAL is unchanged.
            # If a deep-copy/aliasing bug makes the copy share the original's
            # storage, mutating the copy changes the original -> _s1 != _s0 -> die().
            # This catches value-semantics miscompiles present in BOTH compilers,
            # which the hierc-vs-hierc0 differential structurally cannot (they would
            # agree on the same wrong answer). die() exits 1 -> the runner flags it.
            if vs_map and (not (vs_arr or vs_struct) or self.r.random() < 0.34):
                # #2: mutate a COPIED map in place via m[k]; the original must not move.
                # A slotptr that reached through a shared table would change the original.
                a, t0 = self.r.choice(vs_map)
                kt, vt = self._kv(t0)
                key = '"vk"' if kt == "string" else "2"
                s0 = self.fresh("vs"); s1 = self.fresh("vs"); b = self.fresh("cp")
                self.emit(ind, s0 + " := 0")
                self.checksum_into(ind, a, t0, env, s0)
                self.emit(ind, b + " := " + a)               # deep copy (value semantics)
                if vt.startswith("["):                       # array value: grow the COPY's list in place
                    self.emit(ind, "push(" + b + "[" + key + "], " + self.gen_expr(vt[1:-1], env, 2) + ")")
                elif vt.startswith("{"):                     # nested-map value: replace the COPY's whole inner map
                    self.emit(ind, b + "[" + key + "] = " + self.gen_expr(vt, env, 2))
                else:                                        # scalar value: overwrite the COPY's slot
                    newv = {"int": "987654", "float": "987.0", "string": '"zzzzzz"'}[vt]
                    self.emit(ind, b + "[" + key + "] = " + newv)
                self.emit(ind, s1 + " := 0")
                self.checksum_into(ind, a, t0, env, s1)       # re-checksum the ORIGINAL
                self.emit(ind, "if " + s1 + " != " + s0 + ":")
                self.emit(ind+1, 'die("value-semantics violated: in-place m[k] on a copy changed the original")')
                env[b] = t0
                return
            PUSHC = {"int": "7", "string": '"qq"', "float": "2.0"}
            if vs_struct and (not vs_arr or self.r.random() < 0.5):
                a, t0 = self.r.choice(vs_struct)
                f, ft = self.r.choice([(f, ft) for f, ft in self.structs[t0] if ft.startswith("[")])
                target, el = a + "." + f, ft[1:-1]
            else:
                a, t0 = self.r.choice(vs_arr)
                target, el = None, t0[1:-1]
            s0 = self.fresh("vs"); s1 = self.fresh("vs"); b = self.fresh("cp")
            self.emit(ind, s0 + " := 0")
            self.checksum_into(ind, a, t0, env, s0)
            self.emit(ind, b + " := " + a)               # deep copy (value semantics)
            self.emit(ind, "push(" + (b + "." + f if target else b) + ", " + PUSHC[el] + ")")
            self.emit(ind, s1 + " := 0")
            self.checksum_into(ind, a, t0, env, s1)       # re-checksum the ORIGINAL
            self.emit(ind, "if " + s1 + " != " + s0 + ":")
            self.emit(ind+1, 'die("value-semantics violated: mutating a copy changed the original")')
            env[b] = t0
            return

        if k == "soa_use":                          # SOA core ops (the hierc0-supported subset:
            n = self.r.randint(1, 4)                # empty literal, push, len, a[i].f read/write, gather)
            s = self.fresh("sp"); kk = self.fresh("k"); g = self.fresh("g"); ii = self.fresh("i")
            self.emit(ind, s + " := soa []SoaP")
            self.emit(ind, "for " + kk + " in range(" + str(n) + "):")
            self.emit(ind+1, "push(" + s + ", SoaP(" + kk + ", " + kk + " + 1))")
            self.emit(ind, s + "[0].a = " + s + "[0].a + 1")          # scatter (n>=1 -> in bounds)
            self.emit(ind, "for " + ii + " in range(len(" + s + ")):")
            self.emit(ind+1, "acc = acc + " + s + "[" + ii + "].a + " + s + "[" + ii + "].b")
            self.emit(ind, g + " := " + s + "[0]")                    # whole-element gather
            self.emit(ind, "acc = acc + " + g + ".a")
            return
        if k == "multiassign":         # TWO multi-assign `a, b = f()` stmts in the SAME block
            a = self.fresh("ma"); b = self.fresh("mb")
            self.emit(ind, a + " := 0")
            self.emit(ind, b + " := 0")
            self.emit(ind, a + ", " + b + " = pair2(" + self.gen_expr("int", env, 1) + ")")
            self.emit(ind, a + ", " + b + " = pair2((" + b + " % 50))")   # second one feeds off the first
            self.emit(ind, "acc = acc + " + a + " + " + b)
            env[a] = "int"; env[b] = "int"
            return
        if k == "negrange":            # negative-step range: for i in range(hi, lo, -1|-2)
            i = self.fresh("i")
            hi = self.r.randint(2, 5)
            step = self.r.choice(["-1", "-2"])
            self.emit(ind, "for " + i + " in range(" + str(hi) + ", 0, " + step + "):")
            self.emit(ind+1, "acc = acc + " + i)
            return
        if k == "matchpop":            # match on a SIDE-EFFECTING subject: match pop(arr)
            a = self.fresh("mp"); x = self.fresh("px")
            self.emit(ind, a + " := [Some(" + str(self.r.randint(0, 9)) + "), None, Some(" + str(self.r.randint(0, 9)) + ")]")
            env[a] = "[Option(int)]"
            self.emit(ind, "if len(" + a + ") > 0:")    # guard: pop on empty dies
            self.emit(ind+1, "match pop(" + a + "):")
            self.emit(ind+2, "Some(" + x + "):")
            self.emit(ind+3, "acc = acc + " + x)
            self.emit(ind+2, "None:")
            self.emit(ind+3, "acc = acc + 1")
            self.emit(ind, "acc = acc + len(" + a + ")")   # the subject's side effect must be visible (len shrank by 1)
            return
        if k == "orret_loop":          # or_return helper called IN A LOOP with the Err path taken
            i = self.fresh("i"); rv = self.fresh("r"); v = self.fresh("v"); e = self.fresh("e")
            self.emit(ind, "for " + i + " in range(" + str(self.r.randint(2, 4)) + "):")
            self.emit(ind+1, rv + " := orret_chain((" + i + " - 1))")    # i=0 -> orret_chain(-1) -> Err propagated
            self.emit(ind+1, "match " + rv + ":")
            self.emit(ind+2, "Ok(" + v + "):")
            self.emit(ind+3, "acc = acc + " + v)
            self.emit(ind+2, "Err(" + e + "):")
            self.emit(ind+3, "acc = acc + len(" + e + ")")
            return
        if k == "orret_use":                        # or_return: helper unwraps Ok in place / propagates Err
            r = self.fresh("r"); v = self.fresh("v"); e = self.fresh("e")
            self.emit(ind, r + " := orret_chain(" + str(self.r.randint(-1, 4)) + ")")
            self.emit(ind, "match " + r + ":")
            self.emit(ind+1, "Ok(" + v + "):")
            self.emit(ind+2, "acc = acc + " + v)
            self.emit(ind+1, "Err(" + e + "):")
            self.emit(ind+2, "acc = acc + len(" + e + ")")
            return

        if k == "opt_res_eq":          # A3: structural ==/!= on Option/Result, deep value equality folded into acc
            a = self.fresh("oe"); b = self.fresh("oe"); c = self.fresh("oe")
            self.emit(ind, a + " := Some(" + str(self.r.randint(0, 4)) + ")")    # Option(int): scalar payload
            self.emit(ind, b + " := Some(" + str(self.r.randint(0, 4)) + ")")
            self.emit(ind, c + " : Option(int) = None")
            env[a] = "Option(int)"; env[b] = "Option(int)"; env[c] = "Option(int)"
            self.emit(ind, "if " + a + " == " + b + ":")        # Some(x)==Some(y)
            self.emit(ind+1, "acc = acc + 1")
            self.emit(ind, "if " + a + " != " + c + ":")        # Some vs None: tag differs
            self.emit(ind+1, "acc = acc + 2")
            os1 = self.fresh("oe"); os2 = self.fresh("oe")
            self.emit(ind, os1 + ' := Some("' + self.r.choice(["a", "bb", "cc"]) + '")')   # Option(string): heap payload
            self.emit(ind, os2 + ' := Some("' + self.r.choice(["a", "bb", "cc"]) + '")')
            env[os1] = "Option(string)"; env[os2] = "Option(string)"
            self.emit(ind, "if " + os1 + " == " + os2 + ":")
            self.emit(ind+1, "acc = acc + 4")
            r1 = self.fresh("re"); r2 = self.fresh("re")
            self.emit(ind, r1 + " := mkRes(" + str(self.r.randint(-1, 5)) + ")")  # Result([int], string): deep eq through both arms
            self.emit(ind, r2 + " := mkRes(" + str(self.r.randint(-1, 5)) + ")")
            env[r1] = "Result([int], string)"; env[r2] = "Result([int], string)"
            self.emit(ind, "if " + r1 + " == " + r2 + ":")
            self.emit(ind+1, "acc = acc + 8")
            self.emit(ind, "if " + r1 + " != " + r2 + ":")
            self.emit(ind+1, "acc = acc + 16")
            return
        if k == "result_use":                       # bind a heap Result from a helper, then checksum both arms
            r = self.fresh("r")
            self.emit(ind, r + " := mkRes(" + str(self.r.randint(-1, 5)) + ")")
            env[r] = "Result([int], string)"
            if self.r.random() < 0.5:               # value-semantics stressor on a heap Ok payload
                r2 = self.fresh("r"); self.emit(ind, r2 + " := " + r); env[r2] = env[r]
            self.checksum_into(ind, r, "Result([int], string)", env)
            return
        if k == "slice":                            # owning sub-copy: b := v[lo:hi]
            n0, t0 = self.r.choice(slice_vars)
            # string slices CLAMP out-of-range bounds (hier_str_substr) -> any form is safe.
            # array slices ERROR + exit(1) on OOB (hierc.c E_SLICE) -> only whole-array
            # forms ([:], [0:], both = len) are guaranteed in-bounds at unknown runtime length.
            form = self.r.choice([":", "0:", ":1", "0:1"]) if t0 == "string" else self.r.choice([":", "0:"])
            n = self.fresh("sl")
            self.emit(ind, n + " := " + n0 + "[" + form + "]")
            env[n] = t0
            return
        if k == "char_append":                      # char paths: 'x' literal, '0'+int, string+char append
            s = self.r.choice(str_vars)
            if self.r.random() < 0.5:
                piece = "'" + self.r.choice(["a", "Z", "0", " "]) + "'"
            else:
                piece = "('0' + " + self.gen_expr("int", env, 1) + ")"
            self.emit(ind, s + " = " + s + " + " + piece)
            return

        if k == "enum_use":
            en = self.r.choice(list(self.enums.keys()))
            n = self.fresh("e")
            self.emit(ind, n + " := build" + en + "(" + str(self.r.randint(1, 4)) + ")")
            env[n] = en
            self.emit(ind, "acc = acc + sum" + en + "(" + n + ")")
            return
        if k == "inout_fill":                       # mutate a caller array through a mut param (_ina_)
            a = self.fresh("a")
            self.emit(ind, a + " := []int")
            env[a] = "[int]"
            self.emit(ind, "fillA(&" + a + ", " + str(self.r.randint(1, 6)) + ")")
            return
        if k == "spawn_use":                        # CC-1/2: spawn/wait, t.wait(), and an UNWAITED task (implicit join)
            t1 = self.fresh("tk")
            a = self.r.randint(1, 5); b = self.r.randint(0, 9)
            self.emit(ind, t1 + " := spawn fz_work(mkarr(" + str(a) + "), " + str(b) + ")")
            if self.r.random() < 0.5:               # a second task with a heap (string) result, waited UFCS-style
                t2 = self.fresh("tk")
                self.emit(ind, t2 + " := spawn fz_join(\"s\", " + str(self.r.randint(0, 6)) + ")")
                self.emit(ind, "acc = acc + len(" + t2 + ".wait())")
            if self.r.random() < 0.3:               # never waited: CC-2 joins+frees it at this block's end
                self.emit(ind, self.fresh("uw") + " := spawn fz_work(mkarr(2), 1)")
            self.emit(ind, "acc = acc + wait(" + t1 + ")")
            return
        if k == "parfor_use":                       # CC-3: reduction partials (K-independent ints) + optional capture
            p = self.fresh("pf")
            self.emit(ind, p + " := 0")
            ints = [n0 for n0, t0 in arr_vars if t0 == "[int]"]
            if ints and self.r.random() < 0.5:      # capture an env array: per-chunk deep copy
                src = self.r.choice(ints)
                self.emit(ind, "parallel for i" + p + " in range(len(" + src + ")):")
                self.emit(ind + 1, p + " += " + src + "[i" + p + "] % 50")
            else:
                self.emit(ind, "parallel for i" + p + " in range(" + str(self.r.randint(3, 25)) + "):")
                self.emit(ind + 1, p + " += (i" + p + " * 7 + 3) % 97")
            self.emit(ind, "acc = acc + " + p)
            return
        if k == "chan_use":                         # CC-4/5: lock-free channel; the consumer drains while main
            ch = self.fresh("ch")                   # sends, so small capacities hit park/wake deadlock-free
            co = self.fresh("co")
            n = self.r.randint(1, 12); base = self.r.randint(0, 20)
            self.emit(ind, ch + " := channel(int, " + str(self.r.choice([1, 2, 4, 8])) + ")")
            self.emit(ind, co + " := spawn fz_drain(" + ch + ")")
            self.emit(ind, "for i" + ch + " in range(" + str(n) + "):")
            self.emit(ind + 1, "send(" + ch + ", i" + ch + " + " + str(base) + ")")
            self.emit(ind, "close(" + ch + ")")
            self.emit(ind, "acc = acc + wait(" + co + ")")
            return
        if k == "infer_ground":                     # B-0/B-3: bare [] / [] map / None ground from first use
            e = self.fresh("ig")
            self.emit(ind, e + " := []")
            for _ in range(self.r.randint(1, 3)):
                self.emit(ind, "push(" + e + ", " + str(self.r.randint(0, 30)) + ")")
            self.emit(ind, "acc = acc + " + e + "[0] + len(" + e + ")")
            env[e] = "[int]"                        # grounded: an ordinary [int] for the kinds that follow
            if self.r.random() < 0.5:
                m = self.fresh("im")
                self.emit(ind, m + " := []")
                self.emit(ind, m + " = map_set(" + m + ", \"g\", " + str(self.r.randint(0, 30)) + ")")
                self.emit(ind, "acc = acc + map_get(" + m + ", \"g\", 0)")
            if self.r.random() < 0.5:
                o = self.fresh("io")
                self.emit(ind, o + " := None")
                self.emit(ind, o + " = Some(" + str(self.r.randint(0, 30)) + ")")
                self.emit(ind, "match " + o + ":")
                self.emit(ind + 1, "Some(x" + o + "):")
                self.emit(ind + 2, "acc = acc + x" + o)
                self.emit(ind + 1, "None:")
                self.emit(ind + 2, "acc = acc + 0")
            return
        if k == "infer_lambda":                     # B-2: lambda param/ret elision at a typed call site
            self.emit(ind, "acc = acc + fz_apply(fn(x): x * " + str(self.r.randint(1, 9))
                      + " + 1, " + str(self.r.randint(0, 9)) + ")")
            return
        if k == "fstring":                          # f-string interpolation -> the str()-concat desugaring
            # `f"...{e}..."` lowers to str()-of-each-hole concatenated with the literal
            # segments. Fold the result LENGTH into the checksum: a miscompiled desugar
            # (wrong str(), a dropped/reordered segment or hole) shifts the length, which
            # the hierc-vs-hierc0 differential catches; ASan catches a bad concat. Holes
            # are int vars / an int expr / a string var ONLY -- a string LITERAL inside a
            # hole would terminate the f-string at the lexer.
            iv = self.r.choice(int_vars)
            body = "p={" + iv + "} q={(" + iv + " + " + str(self.r.randint(0, 9)) + ")}"
            if str_vars and self.r.random() < 0.7:
                body += " r={" + self.r.choice(str_vars) + "}"
            self.emit(ind, 'acc = acc + len(f"' + body + '")')
            return
        if k == "float_adapt":                      # B-1: int LITERALS adapt in float arithmetic (exact .5 halves)
            f = self.fresh("fl")
            self.emit(ind, f + " := " + str(self.r.randint(1, 9)) + ".5")
            self.emit(ind, "acc = acc + to_int((" + f + " + " + str(self.r.randint(1, 9)) + ") * 2)")
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
        elif k == "pop" and vs_arr:
            n0, t0 = self.r.choice(vs_arr)            # [int]/[string]/[float]: element folds into the checksum
            el = t0[1:-1]
            pv = self.fresh("pv")
            self.emit(ind, "if len(" + n0 + ") > 0:")  # guard: popping an empty array dies
            self.emit(ind + 1, pv + " := pop(" + n0 + ")")
            self.checksum_into(ind + 1, pv, el, env)   # popped value -> acc (str:len, int:value, float:to_int)
        elif k == "fieldmut" and struct_vars:
            n0, t0 = self.r.choice(struct_vars)
            f, ft = self.r.choice(self.structs[t0])
            self.emit(ind, n0 + "." + f + " = " + self.gen_expr(ft, env, 1))
        elif k == "loop" and budget > 2:
            i = self.fresh("i")
            self.loop_vars.add(i)        # counter must never be written (termination)
            self.emit(ind, "for " + i + " in range(" + str(self.r.randint(2,3)) + "):")
            benv = dict(env); benv[i] = "int"
            if self.r.random() < 0.45:           # heap built, THEN a conditional break/continue:
                bc = self.r.choice(["continue", "break"])   # the loop-iteration arena (and the
                ls = self.fresh("ls")                       # if-block arena in hierc0) must free
                self.emit(ind+1, ls + " := mkarr(" + str(self.r.randint(1,4)) + ")")  # on the jump
                self.emit(ind+1, "acc = acc + len(" + ls + ")")
                self.emit(ind+1, "if " + i + " % 2 == 1:")
                self.emit(ind+2, bc)
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
        self.out += ["fn fillA(xs: mut [int], n: int):", "    for i in range(n):", "        push(xs, i)", ""]
        self.out += ["fn fz_sgrow(s: mut string, n: int):", "    for i in range(n):", "        s = s + \"y\"", ""]
        self.out += ["fn mkarr(n: int) -> [int]:", "    r := []int", "    for i in range(n):", "        push(r, (i + 1))", "    return r", ""]
        # transform [int]->[int] used by the `arr_rebuild` kind: `a = xform(a)`
        # reassigns a loop-carried array from a CALL, exercising the liveness-
        # driven buffer-recycle codegen (the call reads a's old buffer to build
        # the result, so recycling must happen after the call, not before).
        # bounded (result in [0,99]) so repeated `a = xform(a)` can't overflow long.
        self.out += ["fn xform(a: [int]) -> [int]:", "    r := []int", "    for i in range(len(a)):",
                     "        v := a[i] + 1", "        push(r, (v - (v / 100) * 100))", "    return r", ""]
        self.out += ["fn mkRes(d: int) -> Result([int], string):", "    if d < 0:", "        return Err(\"neg\")",
                     "    r := []int", "    for i in range(d):", "        push(r, (i + 1))", "    return Ok(r)", ""]
        # SOA element struct (int fields, so it checksums) + or_return helpers.
        self.out += ["struct SoaP:", "    a: int", "    b: int", ""]
        self.out += ["fn orret_mk(d: int) -> Result(int, string):", "    if d < 0:", "        return Err(\"neg\")",
                     "    return Ok(d)", ""]
        self.out += ["fn orret_chain(d: int) -> Result(int, string):", "    x := orret_mk(d) or_return",
                     "    y := orret_mk(d + 1) or_return", "    return Ok(x + y)", ""]
        # multi-return helper for the `multiassign` kind (`a, b = pair2(n)`).
        # bounded: callers pass small / %50-capped args, so n*2 can't overflow.
        self.out += ["fn pair2(n: int) -> (int, int):", "    return n + 1, n * 2", ""]
        # escaping-closure factories: the returned closure's captured env re-homes
        # into the caller's arena. Used by the `escclosure` kind. mkadder captures a
        # scalar; mksum captures a heap array (its env array re-homes too).
        self.out += ["fn mkadder(n: int) -> fn(int) -> int:", "    return fn(x: int) -> int: x + n", ""]
        self.out += ["fn mksum(a: [int]) -> fn(int) -> int:", "    return fn(x: int) -> int: x + len(a)", ""]
        # concurrency + inference fuzz vocabulary (the CC/B kinds); deterministic
        # by construction (sums are interleaving/thread-count independent).
        self.out += ["fn fz_work(a: [int], k: int) -> int:", "    s := 0", "    for i in range(len(a)):",
                     "        s = s + a[i] * k", "    return s % 1000", ""]
        self.out += ["fn fz_join(s: string, n: int) -> string:", "    r := s", "    for i in range(n):",
                     "        r = r + \"x\"", "    return r", ""]
        self.out += ["fn fz_drain(ch: Channel(int)) -> int:", "    s := 0", "    for true:",
                     "        match recv(ch):", "            Some(v):", "                s = s + v",
                     "            None:", "                return s", "    return s", ""]
        self.out += ["fn fz_apply(f: fn(int) -> int, x: int) -> int:", "    return f(x)", ""]
        # FFI: a fixed extern vocabulary (backed by fuzz/ffi_shim.c, linked by
        # run.py). gen_expr weaves calls to these into typed expressions so the
        # differential + ASan oracle exercises the extern / ptr / string-return
        # arena-copy codegen of both compilers.
        self.out += [
            "extern fn fz_addi(a: int, b: int) -> int",
            "extern fn fz_addf(a: float, b: float) -> float",
            "extern fn fz_echo(s: string) -> string",
            "extern fn fz_nullify(s: string) -> string",
            "extern fn fz_slen(s: string) -> int",
            "extern fn fz_handle(id: int) -> ptr",
            "extern fn fz_unwrap(h: ptr) -> int",
            "",
        ]

    def generate(self):
        for _ in range(self.r.randint(0, 2)):
            self.gen_struct()
        for _ in range(self.r.randint(0, 2)):
            self.enums["E" + str(len(self.enums))] = self.r.choice(["int", "string"])
        for _ in range(self.r.randint(0, 2)):
            # distinct newtypes over scalar AND aggregate underlying (int-biased)
            self.newtypes["Nt" + str(len(self.newtypes))] = self.r.choice(["int", "int", "string", "[int]"])
        self.fenum = self.r.random() < 0.5          # a fieldless enum, usable as a map key
        if self.fenum:
            self.out += ["enum FzColor:", "    FzA", "    FzB", "    FzC", ""]
        for name, base in self.newtypes.items():
            self.out.append("type " + name + " = " + base)
        if self.newtypes:
            self.out.append("")
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
