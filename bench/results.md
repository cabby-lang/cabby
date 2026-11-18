# Cabby benchmark results

* Computer: Intel(R) Xeon(R) Processor @ 2.30GHz, 2 cores, Linux 6.18.44-fc-v445
* Python: Python 3.13.17
* C: gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, `-O2`
* Rust: rustc 1.97.0 (2d8144b78 2026-07-07), `-O`
* Cabby: compiled with `cabby -c` (`-O2`), Ubuntu clang version 18.1.3 (1ubuntu1)
* Best of 5 runs; memory is peak resident memory.

## Interpreted: Cabby against Python

| Program | Size | cabby time | python time | cabby memory | python memory |
|---|---|---|---|---|---|
| hello |  | 1.4 ms | 10 ms | 2.8 MB | 10.7 MB |
| fib | 30 | 50 ms | 79 ms | 2.9 MB | 10.7 MB |
| sieve | 2,000,000 | 112 ms | 171 ms | 33.5 MB | 26.0 MB |
| mandelbrot | 250 | 45 ms | 92 ms | 3.0 MB | 10.8 MB |
| sort | 300,000 | 174 ms | 404 ms | 7.5 MB | 22.1 MB |
| strings | 300,000 | 58 ms | 83 ms | 4.7 MB | 12.6 MB |
| objects | 300,000 | 39 ms | 50 ms | 2.9 MB | 10.7 MB |
| text | 100,000 | 35 ms | 45 ms | 3.0 MB | 10.9 MB |
| lists | 300,000 | 16 ms | 30 ms | 7.5 MB | 19.7 MB |
| lookups | 300,000 | 44 ms | 48 ms | 14.5 MB | 25.3 MB |

Relative to Python (below 1× is faster or smaller, above is slower or bigger):

| Program | What it measures | cabby time | cabby memory |
|---|---|---|---|
| hello | start-up and baseline memory | 0.14× | 0.26× |
| fib | function calls (recursive fib) | 0.63× | 0.27× |
| sieve | loops and arrays (primes) | 0.65× | 1.29× |
| mandelbrot | floating point | 0.49× | 0.28× |
| sort | quicksort, arrays passed by borrow | 0.43× | 0.34× |
| strings | building and reading text | 0.71× | 0.38× |
| objects | making objects, virtual calls | 0.78× | 0.27× |
| text | string tools: trim, find, slice, replace | 0.76× | 0.28× |
| lists | lists: add, go through, find, split and join | 0.56× | 0.38× |
| lookups | lookups: count by key, go through, remove | 0.92× | 0.58× |

## Compiled: Cabby against C and Rust

| Program | Size | cabby -c time | c time | rust time | cabby -c memory | c memory | rust memory |
|---|---|---|---|---|---|---|---|
| hello |  | 0.9 ms | 0.9 ms | 1.0 ms | 1.9 MB | 1.4 MB | 2.2 MB |
| fib | 38 | 102 ms | 51 ms | 102 ms | 1.8 MB | 1.8 MB | 2.1 MB |
| sieve | 50,000,000 | 286 ms | 283 ms | 303 ms | 49.5 MB | 49.4 MB | 49.7 MB |
| mandelbrot | 2,500 | 209 ms | 209 ms | 203 ms | 1.8 MB | 1.8 MB | 2.1 MB |
| sort | 10,000,000 | 801 ms | 737 ms | 794 ms | 39.9 MB | 39.9 MB | 40.3 MB |
| strings | 10,000,000 | 379 ms | 358 ms | 178 ms | 109.0 MB | 108.8 MB | 109.2 MB |
| objects | 50,000,000 | 385 ms | 355 ms | 488 ms | 1.8 MB | 1.8 MB | 2.1 MB |
| text | 3,000,000 | 470 ms | 461 ms | 421 ms | 1.9 MB | 1.8 MB | 2.1 MB |
| lists | 10,000,000 | 41 ms | 34 ms | 33 ms | 71.9 MB | 71.7 MB | 72.1 MB |
| lookups | 10,000,000 | 913 ms | 451 ms | 1.06 s | 180.8 MB | 257.6 MB | 145.9 MB |

Relative to C (below 1× is faster or smaller, above is slower or bigger):

| Program | What it measures | cabby -c time | rust time | cabby -c memory | rust memory |
|---|---|---|---|---|---|
| hello | start-up and baseline memory | 1.09× | 1.11× | 1.33× | 1.58× |
| fib | function calls (recursive fib) | 2.00× | 1.99× | 0.99× | 1.19× |
| sieve | loops and arrays (primes) | 1.01× | 1.07× | 1.00× | 1.01× |
| mandelbrot | floating point | 1.00× | 0.97× | 1.00× | 1.18× |
| sort | quicksort, arrays passed by borrow | 1.09× | 1.08× | 1.00× | 1.01× |
| strings | building and reading text | 1.06× | 0.50× | 1.00× | 1.00× |
| objects | making objects, virtual calls | 1.08× | 1.37× | 1.00× | 1.18× |
| text | string tools: trim, find, slice, replace | 1.02× | 0.91× | 1.05× | 1.18× |
| lists | lists: add, go through, find, split and join | 1.20× | 0.95× | 1.00× | 1.00× |
| lookups | lookups: count by key, go through, remove | 2.02× | 2.36× | 0.70× | 0.57× |
