def main():
    n = @N@
    composite = [False] * (n + 1)
    count = 0
    for i in range(2, n + 1):
        if not composite[i]:
            count += 1
            j = i * i
            while j <= n:
                composite[j] = True
                j += i
    print(count)


main()
