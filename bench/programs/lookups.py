def main():
    n = @N@
    half = n // 2
    counts = {}
    for i in range(n):
        k = i * 7 % half
        if k in counts:
            counts[k] += 1
        else:
            counts[k] = 1
    total = 0
    for k, v in counts.items():
        total += k * v
    for i in range(half // 2):
        del counts[i * 2]
    words = {}
    for i in range(n // 10):
        w = "w" + str(i % 1000)
        if w in words:
            words[w] += 1
        else:
            words[w] = 1
    print(len(counts), total, len(words), words["w7"])


main()
