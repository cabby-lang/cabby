#!/usr/bin/env python3
"""Numbers a Cabby Quest tutorial's own sections 1, 2, 3 ... in the order
they appear (the sections of earlier tutorials keep their numbers). With
--part 78, it numbers part 78's sections instead, in each file given: the
later tutorials, which have the same sections in the same order.

    python3 tools/renumber-sections.py tutorials/079-quest-areas.cby
    python3 tools/renumber-sections.py --part 78 tutorials/*-quest-*.cby"""
import re, sys, os

args = sys.argv[1:]
part = None
if args and args[0] == "--part":
    part, args = args[1], args[2:]
import glob

def numbers_in(path, prefix):
    """a tutorial's own numbers for its sections, by title"""
    found = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*// ==== (%s\.\d+)  (.*?)(  \(changed in [0-9, ]+\))?\s*=*\s*$" % prefix, line)
        if m:
            found[m.group(2)] = m.group(1)
    return found

for path in args:
    own = part or os.path.basename(path)[:2]
    # a later tutorial takes the part's own numbers, by title (a section it
    # took out leaves a gap, rather than moving the rest up)
    home = glob.glob(os.path.join(os.path.dirname(path) or ".", own + "-*.cby"))
    known = numbers_in(home[0], own) if part and home and os.path.basename(home[0]) != os.path.basename(path) else {}
    n = 0
    def number(m):
        global n
        if m.group(1) != own:
            return m.group(0)
        indent = m.group(0)[:m.start(1) - m.start(0) - 8]
        title = re.match(r"(.*?)(  \(changed in [0-9, ]+\))?\s*=*\s*$", m.string[m.end():m.string.find("\n", m.end())]).group(1)
        if title in known:
            return "%s// ==== %s  " % (indent, known[title])
        n += 1
        return "%s// ==== %s.%d  " % (indent, own, n)
    text = open(path, encoding="utf-8").read()
    # (only headings that begin a line: not one quoted in a comment)
    text = re.sub(r"^[ \t]*// ==== (\d+)\.\d+  ", number, text, flags=re.M)
    # every heading the same width: ==== up to the 76th column
    def even(m):
        indent, head = m.group(1), m.group(2).rstrip()
        width = len(indent.replace("\t", "    ")) + len(head)
        return indent + head + " " + "=" * max(4, 75 - width)
    text = re.sub(r"^([ \t]*)(// ==== \d+\.\d+  .*?)\s*=*[ \t]*$", even, text, flags=re.M)
    open(path, "w", encoding="utf-8").write(text)
