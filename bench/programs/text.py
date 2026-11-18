def main():
    n = @N@
    total = hits = sevens = smaller = 0
    for i in range(n):
        line = "  item " + str(i) + ": the cat sat on the mat  "
        trimmed = line.strip()
        colon = trimmed.find(":")
        name = trimmed[:colon]
        rest = trimmed[colon + 2:]
        if "cat" in rest:
            hits += 1
        changed = rest.replace("cat", "dog")
        total += len(changed) + len(name)
        if name.upper().startswith("ITEM 7"):
            sevens += 1
        if name < "item 5":
            smaller += 1
    print(total, hits, sevens, smaller)


main()
