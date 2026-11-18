def main():
    n = @N@
    inside = 0
    for py in range(n):
        ci = 2.0 * py / n - 1.0
        for px in range(n):
            cr = 3.0 * px / n - 2.0
            zr = zi = 0.0
            k = 0
            while k < 50 and zr * zr + zi * zi <= 4.0:
                t = zr * zr - zi * zi + cr
                zi = 2.0 * zr * zi + ci
                zr = t
                k += 1
            if k == 50:
                inside += 1
    print(inside)


main()
