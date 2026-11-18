#!/usr/bin/env python3
"""Checks the Cabby Quest tutorials (77 to 88): each numbered section has
no more than ten lines of code (blank lines and comments don't count), and
the sections are numbered in order within each tutorial.

    python3 tools/check-sections.py tutorials/*-quest-*.cby"""
import re, sys

bad = 0
for path in sys.argv[1:]:
    sections, current = [], None
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*// ==== (\d+)\.(\d+)\s+(.*?)\s*=*\s*$", line)
        if m:
            current = [(int(m.group(1)), int(m.group(2))), m.group(3), 0]
            sections.append(current)
            continue
        code = line.strip()
        if current and code and not code.startswith("//") and not code.startswith("// ----"):
            if re.match(r"^// -+ output", code):
                current = None
                continue
            current[2] += 1
        if code.startswith("// script:") or code.startswith("// ----------"):
            current = None
    for (num, title, n) in sections:
        if n > 10:
            print("%s: section %d.%d (%s) has %d lines of code" % (path, num[0], num[1], title, n))
            bad += 1
    last = {}
    for (num, title, n) in sections:
        if num[1] <= last.get(num[0], 0):
            print("%s: section %d.%d comes after %d.%d" % (path, num[0], num[1], num[0], last[num[0]]))
            bad += 1
        last[num[0]] = num[1]
# no two sections with the same title (a reader, and the tools, tell them
# apart by it)
for path in sys.argv[1:]:
    seen = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*// ==== (\d+\.\d+)  (.*?)(  \(changed in [0-9, ]+\))?\s*=*\s*$", line)
        if m:
            if m.group(2) in seen and seen[m.group(2)] != m.group(1):
                print("%s: sections %s and %s are both \"%s\"" % (path, seen[m.group(2)], m.group(1), m.group(2)))
                bad += 1
            seen[m.group(2)] = m.group(1)

# the same section has the same title in every tutorial it's in
titles = {}
for path in sys.argv[1:]:
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*// ==== (\d+\.\d+)  (.*?)(  \(changed in [0-9, ]+\))?\s*=*\s*$", line)
        if m:
            if m.group(1) in titles and titles[m.group(1)][0] != m.group(2):
                print("%s: section %s is \"%s\", but \"%s\" in %s" % (path, m.group(1), m.group(2),
                      titles[m.group(1)][0], titles[m.group(1)][1]))
                bad += 1
            titles.setdefault(m.group(1), (m.group(2), path))
sys.exit(1 if bad else 0)
