def quicksort(a, lo, hi):
    if lo >= hi:
        return
    pivot = a[(lo + hi) // 2]
    i, j = lo, hi
    while i <= j:
        while a[i] < pivot:
            i += 1
        while a[j] > pivot:
            j -= 1
        if i <= j:
            a[i], a[j] = a[j], a[i]
            i += 1
            j -= 1
    quicksort(a, lo, j)
    quicksort(a, i, hi)


def main():
    n = @N@
    xs = [0] * n
    seed = 42
    for i in range(n):
        seed = (seed * 1103515245 + 12345) % 2147483648
        xs[i] = seed % 1000000
    quicksort(xs, 0, n - 1)
    check = 0
    for x in xs:
        check = (check * 31 + x) % 1000000007
    print(check)


main()
