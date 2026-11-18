def main():
    n = @N@
    s = ""
    for i in range(n):
        s += str(i)
        s += " "
    sevens = 0
    for i in range(len(s)):
        if s[i] == "7":
            sevens += 1
    print(len(s), sevens)


main()
