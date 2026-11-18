class Shape:
    def __init__(self, size):
        self.size = size
    def area(self):
        return 0

class Square(Shape):
    def area(self):
        return self.size * self.size

class Triangle(Shape):
    def area(self):
        return self.size * self.size // 2

def measure(s):
    return s.area()


def main():
    n = @N@
    total = 0
    for i in range(n):
        if i % 2 == 0:
            total += measure(Square(i % 100))
        else:
            total += measure(Triangle(i % 100))
    print(total)


main()
