# math — square roots, powers, trigonometry, rounding

```cabby
use math
```

The C math library. Every function takes and returns `double`; `int` and
`float` arguments are converted for you.

| Declaration | Gives |
|---|---|
| `extern double sqrt double x` | the square root of x |
| `extern double cbrt double x` | the cube root of x |
| `extern double pow double x, double y` | x to the power y |
| `extern double exp double x` | e to the power x |
| `extern double log double x` | the natural logarithm of x (base e) |
| `extern double log10 double x` | the logarithm of x, base 10 |
| `extern double log2 double x` | the logarithm of x, base 2 |
| `extern double sin double x` | the sine of x (x in radians) |
| `extern double cos double x` | the cosine of x |
| `extern double tan double x` | the tangent of x |
| `extern double asin double x` | the angle whose sine is x, in radians |
| `extern double acos double x` | the angle whose cosine is x |
| `extern double atan double x` | the angle whose tangent is x |
| `extern double atan2 double y, double x` | the angle of the point (x, y), from -π to π |
| `extern double hypot double x, double y` | the length of the hypotenuse, √(x² + y²) |
| `extern double floor double x` | x rounded down to a whole number |
| `extern double ceil double x` | x rounded up |
| `extern double round double x` | x rounded to the nearest whole number (halves away from zero) |
| `extern double trunc double x` | x with its fraction cut off (toward zero) |
| `extern double fabs double x` | x without its sign |
| `extern double fmod double x, double y` | the remainder of x / y, with x's sign |

<!-- run -->
```cabby
use math

const double PI = 3.141592653589793

int main {
	println sqrt(2.0), cbrt(8.0), pow(2.0, 10.0)
	println floor(2.5), ceil(2.5), round(2.5), trunc(-2.5)
	println hypot(3.0, 4.0), fabs(-1.5), fmod(7.5, 2.0)
	println sin(PI / 2), cos(0.0), atan2(1.0, 1.0) * 4
	println log(exp(1.0)), log10(1000.0), log2(1024.0)
}
```

```text
1.4142135623730951 2.0 1024.0
2.0 3.0 3.0 -2.0
5.0 1.5 1.5
1.0 1.0 3.141592653589793
1.0 3.0 10.0
```

## Notes

* **Angles are in radians.** A full turn is 2π. To convert degrees:
  `degrees * PI / 180`. There is no `PI` constant in the header; declare
  one as above.
* **Rounding to a number of decimals:** to round to cents,
  `floor(x * 100.0 + 0.5) / 100.0`.
* **Rounded results are still doubles.** To get an integer, cast:
  `int n = (int)round(x)`.
* **Out-of-range input doesn't panic.** `sqrt -1.0` and `log 0.0` give
  `NaN` and `-inf`, as floating point does (manual chapter 4).
* **The last digit can differ** between systems for some functions: with
  glibc, `cbrt 27.0` is 3.0000000000000004, not exactly 3. Compare
  results with a small tolerance rather than `==`.
* These are the system's own math routines, tuned over decades; most
  languages' math libraries call the same ones underneath.

## See also

The manual's chapter 4, "Floating point", and chapter 27, "Calling C".
