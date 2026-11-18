#!/usr/bin/env python3
"""Rewrites Cabby code's `is`, `is not`, `and`, `or` and `not` as `==`,
`!=`, `&&`, `||` and `!` (0.36.0, when the words were taken out).

    tools/words-to-symbols.py FILE...

A .cby or .hby file is rewritten whole; a Markdown file only in its
```cabby blocks. Strings, characters and comments are left as they are.
`not` took the whole comparison after it (`not A is B` was !(A == B)),
so its operand gets parentheses unless it's a single name, call or
member (`not Done` is `!Done`, `not Names.contains(X)` is
`!Names.contains(X)`).
"""
import re
import sys

TWO = ["&&", "||", "==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "++", "--", "->"]


def tokens(src):
    """(kind, text) pairs covering src exactly."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "\n":
            out.append(("nl", c)); i += 1
        elif c in " \t\r":
            j = i
            while j < n and src[j] in " \t\r":
                j += 1
            out.append(("ws", src[i:j])); i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(("comment", src[i:j])); i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(("comment", src[i:j])); i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(("str", src[i:j])); i = j
        elif c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] == "_"):
                j += 1
            out.append(("ident", src[i:j])); i = j
        elif c.isdigit():
            j = i
            while j < n and (src[j].isalnum() or src[j] in "._" or (src[j] in "+-" and src[j - 1] in "eE" and not src[i:j].startswith("0x"))):
                j += 1
            out.append(("num", src[i:j])); i = j
        else:
            two = src[i:i + 2]
            if two in TWO:
                out.append(("punct", two)); i += 2
            else:
                out.append(("punct", c)); i += 1
    return out


def swap_words(toks):
    """is not / is / and / or, in place (as text)."""
    out = []
    k = 0
    while k < len(toks):
        kind, text = toks[k]
        if kind == "ident" and text == "is":
            # `is not`?
            j = k + 1
            while j < len(toks) and toks[j][0] == "ws":
                j += 1
            if j < len(toks) and toks[j] == ("ident", "not"):
                out.append(("punct", "!="))
                k = j + 1
                continue
            out.append(("punct", "=="))
        elif kind == "ident" and text == "and":
            out.append(("punct", "&&"))
        elif kind == "ident" and text == "or":
            out.append(("punct", "||"))
        else:
            out.append((kind, text))
        k += 1
    return out


OPEN, CLOSE = "([{", ")]}"
STOP_PUNCT = {"&&", "||", ")", "]", "}", ",", "{", ";", "?", ":", "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="}
STOP_IDENT = {"else", "to", "down", "by"}


def operand_end(toks, start):
    """index after `not`'s operand, which begins at start"""
    depth = 0
    k = start
    while k < len(toks):
        kind, text = toks[k]
        if kind == "punct" and text in OPEN:
            if depth == 0 and text == "{":
                return k
            depth += 1
        elif kind == "punct" and text in CLOSE:
            if depth == 0:
                return k
            depth -= 1
        elif depth == 0:
            if kind == "nl":
                return k
            if kind == "comment":
                return k
            if kind == "punct" and text in STOP_PUNCT:
                return k
            if kind == "ident" and text in STOP_IDENT:
                return k
        k += 1
    return k


def simple(part):
    """a name, call or member, with nothing after it: no parentheses needed"""
    p = [t for t in part]
    while p and p[-1][0] == "ws":
        p.pop()
    if not p:
        return True
    k = 0
    kind, text = p[0]
    if kind in ("ident", "num", "str"):
        k = 1
    elif kind == "punct" and text == "(":
        depth = 0
        while k < len(p):
            if p[k][0] == "punct" and p[k][1] in OPEN:
                depth += 1
            elif p[k][0] == "punct" and p[k][1] in CLOSE:
                depth -= 1
                if depth == 0:
                    k += 1
                    break
            k += 1
    else:
        return False
    while k < len(p):
        kind, text = p[k]
        if kind == "punct" and text == "." and k + 1 < len(p) and p[k + 1][0] == "ident":
            k += 2
        elif kind == "punct" and text in "([":
            depth = 0
            while k < len(p):
                if p[k][0] == "punct" and p[k][1] in OPEN:
                    depth += 1
                elif p[k][0] == "punct" and p[k][1] in CLOSE:
                    depth -= 1
                    if depth == 0:
                        k += 1
                        break
                k += 1
        else:
            return False
    return True


COMPARE = {"==", "!=", "<", ">", "<=", ">="}


def flip(part):
    """one == or != at the top, and no other comparison: the other one"""
    depth = 0
    at = None
    for k, (kind, text) in enumerate(part):
        if kind == "punct" and text in OPEN:
            depth += 1
        elif kind == "punct" and text in CLOSE:
            depth -= 1
        elif depth == 0 and kind == "punct" and text in COMPARE:
            if at is not None or text not in ("==", "!="):
                return None
            at = k
    if at is None:
        return None
    other = "!=" if part[at][1] == "==" else "=="
    return part[:at] + [("punct", other)] + part[at + 1:]


def swap_nots(toks):
    """each `not X` as !X, or !(X); the last first, so an inner one is done
    before the one around it"""
    while True:
        at = None
        for k in range(len(toks) - 1, -1, -1):
            if toks[k] == ("ident", "not"):
                at = k
                break
        if at is None:
            return toks
        j = at + 1
        while j < len(toks) and toks[j][0] == "ws":
            j += 1
        end = operand_end(toks, j)
        part = toks[j:end]
        trail = []
        while part and part[-1][0] == "ws":
            trail.insert(0, part.pop())
        flipped = flip(part)
        if simple(part):
            new = [("punct", "!")] + part
        elif flipped is not None:
            new = flipped     # !(A == B) is A != B
        else:
            new = [("punct", "!"), ("punct", "(")] + part + [("punct", ")")]
        toks = toks[:at] + new + trail + toks[end:]


def convert_code(src):
    toks = tokens(src)
    toks = swap_words(toks)
    toks = swap_nots(toks)
    return "".join(t for _, t in toks)


def convert_markdown(src):
    out = []
    lines = src.split("\n")
    k = 0
    while k < len(lines):
        line = lines[k]
        out.append(line)
        if line.strip() == "```cabby":
            body = []
            k += 1
            while k < len(lines) and lines[k].strip() != "```":
                body.append(lines[k])
                k += 1
            out.extend(convert_code("\n".join(body)).split("\n") if body else [])
            if k < len(lines):
                out.append(lines[k])
        k += 1
    return "\n".join(out)


def main():
    for path in sys.argv[1:]:
        with open(path) as f:
            src = f.read()
        new = convert_markdown(src) if path.endswith(".md") else convert_code(src)
        if new != src:
            with open(path, "w") as f:
                f.write(new)
            print("changed", path)


if __name__ == "__main__":
    main()
