#!/usr/bin/env python3
"""Makes wasm/calls.c: a wrapper for each C function Cabby's libraries (and
programs) declare with `extern`, for the interpreter built for WebAssembly,
which has no libffi.

    tools/make-wasm-calls.py out.c [files...]

Each wrapper takes the arguments as the interpreter lays them out (an array
of pointers, each at a value encoded as its Cabby type) and calls the
function with its own C types, so WebAssembly, which checks every call's
types exactly, never sees a function called through a pointer of another
shape. Cabby's own runtime functions (cby_..., rt_...) are declared from
their extern lines, weakly: one that isn't in the WebAssembly build (a
window's, a socket's) is missing, and its wrapper says so. C's own
functions (strlen, sqrt) are called through C's headers, which convert
Cabby's numbers to C's (a C `long` is 32 bits there; a Cabby `long`, 64).
Those that don't compile in WebAssembly's C library are left out.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Cabby type -> (C type, how a slot is read)
SCALARS = {
    "bool": "_Bool", "char": "char", "signed char": "signed char", "unsigned char": "unsigned char",
    "short": "int16_t", "unsigned short": "uint16_t", "int": "int32_t", "unsigned": "uint32_t",
    "unsigned int": "uint32_t", "long": "int64_t", "long long": "int64_t", "unsigned long": "uint64_t",
    "unsigned long long": "uint64_t", "float": "float", "double": "double",
}


def c_type(t, libc):
    """The C type for a Cabby parameter or return type, or None if a
    wrapper can't be made for it (a struct, a callback)."""
    t = t.strip()
    if t in SCALARS:
        return SCALARS[t]
    if t == "str":
        return "const char *"
    if t == "void":
        return "void"
    if t.startswith("address"):
        inner = t[len("address"):].strip()
        if not inner:
            return "void *"
        it = c_type(inner, libc)
        if it is None or it == "void":
            return "void *" if it == "void" else None
        if not libc:
            return "void *"
        # (C's own: its pointer types, as its headers give them)
        if inner in ("long", "unsigned long"):
            return None   # a C long* isn't a Cabby long*: 32 bits against 64
        return it + " *" if not it.endswith("*") else it + "*"
    return None


# C's own that take or give a C long, which is 32 bits in WebAssembly (a
# Cabby long is 64): each is called as its long long twin instead
LONG_TWINS = {"labs": "llabs", "atol": "atoll", "strtol": "strtoll", "strtoul": "strtoull"}

TYPE_WORDS = {"int", "long", "char", "double", "float", "short", "bool", "unsigned", "signed", "address", "str", "void"}


def read_type(toks, i):
    """A Cabby type starting at toks[i]: (its text, the next position)"""
    t = toks[i]
    if t == "address":
        if i + 1 < len(toks) and toks[i + 1] in TYPE_WORDS - {"str", "void"}:
            inner, j = read_type(toks, i + 1)
            return "address " + inner, j
        return "address", i + 1
    if t in ("unsigned", "signed"):
        if i + 1 < len(toks) and toks[i + 1] in ("char", "short", "int", "long"):
            if toks[i + 1] == "long" and i + 2 < len(toks) and toks[i + 2] == "long":
                return t + " long long", i + 3
            return t + " " + toks[i + 1], i + 2
        return t, i + 1
    if t == "long" and i + 1 < len(toks) and toks[i + 1] == "long":
        return "long long", i + 2
    return t, i + 1


def parse(line):
    """extern RET NAME P1, P2 -> (name, ret, [(type, is_ref)], variadic)"""
    m = re.match(r"^extern\s+(.*)$", line.strip())
    if not m:
        return None
    rest = m.group(1).split("//")[0].strip()
    toks = rest.split()
    if len(toks) < 2:
        return None
    ret, i = read_type(toks, 0)
    if i >= len(toks):
        return None
    name = toks[i]
    params = " ".join(toks[i + 1:])
    out = []
    variadic = False
    if params.strip() and params.strip() != "void":
        for p in params.split(","):
            p = p.strip()
            if not p:
                continue
            if p == "...":
                variadic = True
                continue
            ref = "&" in p
            pt = p.replace("&", " ").split()
            if len(pt) < 2:
                return None
            t, k = read_type(pt, 0)
            if k != len(pt) - 1:   # (a type of more words: a struct, a callback's)
                return None
            out.append((t, ref))
    return name, ret, out, variadic


def wrapper(name, ret, params, libc):
    """The wrapper's C text, or None"""
    rt = c_type(ret, libc)
    if rt is None:
        return None
    args, decl = [], []
    slot = 0
    for t, ref in params:
        if t == "handler":
            if libc:
                return None
            decl += ["void *", "void *"]
            args += [f"*(void **)a[{slot}]", f"*(void **)a[{slot + 1}]"]
            slot += 2
            continue
        ct = c_type(t, libc)
        if ct is None or ct == "void":
            return None
        if ref:
            if libc and t in ("long", "unsigned long"):
                return None
            decl.append(ct + " *")
            args.append(f"*({ct} **)a[{slot}]")
        elif ct.endswith("*"):
            decl.append(ct)
            args.append(f"*({ct} *)a[{slot}]")
        else:
            decl.append(ct)
            args.append(f"*({ct} *)a[{slot}]")
        slot += 1
    call = f"{LONG_TWINS.get(name, name) if libc else name}({', '.join(args)})"
    if rt == "void":
        body = f"\t{call};\n"
    elif rt.endswith("*"):
        body = f"\t*(void **)r = (void *){call};\n"
    elif rt == "float":
        body = f"\t*(float *)r = {call};\n"
    elif rt == "double":
        body = f"\t*(double *)r = {call};\n"
    elif rt.startswith("u") or rt == "_Bool" or rt == "unsigned char":
        body = f"\t*(uint64_t *)r = (uint64_t){call};\n"
    else:
        body = f"\t*(int64_t *)r = (int64_t){call};\n"
    proto = None
    if not libc:
        proto = f"__attribute__((weak)) {rt} {name}({', '.join(decl) if decl else 'void'});\n"
        check = f"\tif (!{name})\n\t\tcby_wasm_missing(\"{name}\");\n"
    else:
        check = ""
    text = f"static void w_{name}(void **a, void *r)\n{{\n\t(void)a; (void)r;\n{check}{body}}}\n"
    return proto, text


def missing(defined_file, out, files):
    """--missing: a stand-in for each of Cabby's runtime functions that the
    libraries declare but the WebAssembly runtime hasn't (a window's, a
    socket's), for programs compiled for it: it links, and says so if
    called"""
    defined = set(open(defined_file).read().split())
    seen = {}
    for f in files:
        for line in open(f, encoding="utf-8", errors="replace"):
            if line.startswith("extern "):
                p = parse(line)
                if p and p[0].startswith(("cby_", "rt_")) and p[0] not in defined and p[0] not in seen and not p[3]:
                    seen[p[0]] = p
    n = 0
    with open(out, "w") as o:
        o.write("/* made by tools/make-wasm-calls.py --missing; don't edit. Cabby's runtime\n"
                " * functions that aren't in the WebAssembly runtime (windows, sockets...),\n"
                " * each saying so when called, for programs compiled with -c --wasm. */\n"
                "#include <stdint.h>\n#include <stdbool.h>\n#include <stdlib.h>\n#include <stdio.h>\n"
                "#include \"rt.h\"\n\n"
                "__attribute__((weak)) _Noreturn void cby_wasm_missing(const char *name)\n{\n"
                "\trt_panic(NULL, 0, 0, \"'%s' isn't here (in a browser): its library needs the computer itself\", name);\n"
                "\tabort();\n}\n\n")
        for name, (_, ret, params, _v) in sorted(seen.items()):
            rt = c_type(ret, False)
            decl = []
            ok = rt is not None
            for t, ref in params:
                if t == "handler":
                    decl += ["void *", "void *"]
                    continue
                ct = c_type(t, False)
                if ct is None or ct == "void":
                    ok = False
                    break
                decl.append(ct + " *" if ref else ct)
            if not ok:
                continue
            ps = ", ".join(f"{d} a{i}" for i, d in enumerate(decl)) or "void"
            o.write(f"{rt} {name}({ps})\n{{\n")
            o.write("".join(f"\t(void)a{i};\n" for i in range(len(decl))))
            o.write(f"\tcby_wasm_missing(\"{name}\");\n}}\n")
            n += 1
    print(f"{out}: {n} stand-ins")


def main():
    if sys.argv[1] == "--missing":
        return missing(sys.argv[2], sys.argv[3], sys.argv[4:])
    out = sys.argv[1]
    files = sys.argv[2:]
    seen = {}
    for f in files:
        for line in open(f, encoding="utf-8", errors="replace"):
            if not line.startswith("extern "):
                continue
            p = parse(line)
            if p and p[0] not in seen and not p[3]:
                seen[p[0]] = p
    own, libc = [], []
    for name, (n, ret, params, var) in sorted(seen.items()):
        (own if name.startswith(("cby_", "rt_")) else libc).append((name, ret, params))
    clang = os.environ.get("WASI_CLANG", "clang")
    cflags = os.environ.get("WASI_CFLAGS", "").split()
    headers = ("#include <stdint.h>\n#include <stdbool.h>\n#include <string.h>\n#include <stdlib.h>\n"
               "#include <stdio.h>\n#include <math.h>\n#include <ctype.h>\n#include <time.h>\n#include <wchar.h>\n")
    # C's own: each kept only if it compiles here
    libc_ok = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, ret, params in libc:
            w = wrapper(name, ret, params, True)
            if not w:
                continue
            src = os.path.join(tmp, "t.c")
            open(src, "w").write(headers + w[1])
            r = subprocess.run([clang] + cflags + ["-Werror=implicit-function-declaration",
                               "-Werror=int-conversion", "-Werror=incompatible-pointer-types",
                               "-fsyntax-only", src], capture_output=True)
            if r.returncode == 0:
                libc_ok.append((name, w[1]))
    entries = []
    with open(out, "w") as o:
        o.write("/* wasm/calls.c — made by tools/make-wasm-calls.py; don't edit. A wrapper\n"
                " * for each C function the libraries declare, for the interpreter built\n"
                " * for WebAssembly (see wasm/ffi.h). */\n")
        o.write(headers)
        o.write("\ntypedef void (*cby_wasm_call)(void **args, void *ret);\n")
        o.write("_Noreturn void cby_wasm_missing(const char *name);\n\n")
        o.write("/* ---- C's own */\n")
        for name, text in libc_ok:
            o.write(text)
            entries.append(name)
        o.write("\n/* ---- Cabby's runtime (those not in the WebAssembly build are missing) */\n")
        for name, ret, params in own:
            w = wrapper(name, ret, params, False)
            if not w:
                continue
            o.write(w[0])
            o.write(w[1])
            entries.append(name)
        entries.sort()
        o.write("\nstatic const struct { const char *name; cby_wasm_call call; } calls[] = {\n")
        for name in entries:
            o.write(f"\t{{ \"{name}\", w_{name} }},\n")
        o.write("};\n\n")
        o.write("cby_wasm_call cby_wasm_find(const char *name)\n{\n"
                "\tsize_t lo = 0, hi = sizeof calls / sizeof *calls;\n"
                "\twhile (lo < hi) {\n\t\tsize_t mid = (lo + hi) / 2;\n"
                "\t\tint c = strcmp(name, calls[mid].name);\n"
                "\t\tif (c == 0)\n\t\t\treturn calls[mid].call;\n"
                "\t\tif (c < 0)\n\t\t\thi = mid;\n\t\telse\n\t\t\tlo = mid + 1;\n\t}\n\treturn NULL;\n}\n")
    print(f"{out}: {len(libc_ok)} of C's own, {len(entries) - len(libc_ok)} of Cabby's runtime")


main()
