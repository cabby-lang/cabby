def main():
    n = @N@
    values = []
    for i in range(n):
        values.append(i * 7 % 1000)
    total = 0
    for v in values:
        total += v
    words = "alpha beta gamma delta epsilon".split(" ")
    letters = 0
    for i in range(n // 100):
        letters += len(",".join(words))
    print(len(values), total, values.index(999) if 999 in values else -1, letters)


main()
