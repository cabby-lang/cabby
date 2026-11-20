# Cabby benchmark results

* Computer: Intel(R) Xeon(R) Processor @ 2.10GHz, 2 cores, Linux 6.18.44-fc-v726
* Python: Python 3.13.17
* C: gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, `-O2`
* Rust: rustc 1.97.0 (2d8144b78 2026-07-07), `-O`
* Cabby: compiled with `cabby -c` (`-O2`), Ubuntu clang version 18.1.3 (1ubuntu1)
* Best of 5 runs; memory is peak resident memory.

## Interpreted: Cabby against Python

| Program | Size | cabby time | python time | cabby memory | python memory |
|---|---|---|---|---|---|
| hello |  | 1.8 ms | 10 ms | 3.2 MB | 10.9 MB |
| fib | 30 | 64 ms | 81 ms | 3.2 MB | 10.9 MB |
| sieve | 2,000,000 | 108 ms | 197 ms | 5.2 MB | 26.1 MB |
| mandelbrot | 250 | 53 ms | 88 ms | 3.3 MB | 10.9 MB |
| sort | 300,000 | 256 ms | 515 ms | 4.4 MB | 22.2 MB |
| strings | 300,000 | 67 ms | 94 ms | 5.3 MB | 12.7 MB |
| objects | 300,000 | 45 ms | 57 ms | 3.4 MB | 10.9 MB |
| text | 100,000 | 36 ms | 55 ms | 3.4 MB | 11.0 MB |
| lists | 300,000 | 21 ms | 41 ms | 7.9 MB | 19.9 MB |
| lookups | 300,000 | 54 ms | 52 ms | 14.8 MB | 25.4 MB |

Relative to Python (below 1× is faster or smaller, above is slower or bigger):

| Program | What it measures | cabby time | cabby memory |
|---|---|---|---|
| hello | start-up and baseline memory | 0.17× | 0.30× |
| fib | function calls (recursive fib) | 0.79× | 0.29× |
| sieve | loops and arrays (primes) | 0.55× | 0.20× |
| mandelbrot | floating point | 0.60× | 0.30× |
| sort | quicksort, arrays passed by borrow | 0.50× | 0.20× |
| strings | building and reading text | 0.72× | 0.42× |
| objects | making objects, virtual calls | 0.80× | 0.32× |
| text | string tools: trim, find, slice, replace | 0.66× | 0.31× |
| lists | lists: add, go through, find, split and join | 0.50× | 0.40× |
| lookups | lookups: count by key, go through, remove | 1.04× | 0.58× |

## Compiled: Cabby against C and Rust

| Program | Size | cabby -c time | c time | rust time | cabby -c memory | c memory | rust memory |
|---|---|---|---|---|---|---|---|
| hello |  | 1.1 ms | 0.9 ms | 1.1 ms | 1.9 MB | 1.4 MB | 2.1 MB |
| fib | 38 | 112 ms | 64 ms | 130 ms | 1.8 MB | 1.8 MB | 2.2 MB |
| sieve | 50,000,000 | 351 ms | 356 ms | 344 ms | 49.5 MB | 49.4 MB | 49.7 MB |
| mandelbrot | 2,500 | 238 ms | 252 ms | 202 ms | 1.9 MB | 1.8 MB | 2.1 MB |
| sort | 10,000,000 | 890 ms | 836 ms | 947 ms | 40.0 MB | 39.9 MB | 40.3 MB |
| strings | 10,000,000 | 485 ms | 412 ms | 206 ms | 109.0 MB | 108.8 MB | 109.2 MB |
| objects | 50,000,000 | 64 ms | 422 ms | 587 ms | 1.9 MB | 1.8 MB | 2.2 MB |
| text | 3,000,000 | 568 ms | 515 ms | 491 ms | 1.9 MB | 1.8 MB | 2.1 MB |
| lists | 10,000,000 | 53 ms | 48 ms | 52 ms | 71.9 MB | 71.7 MB | 72.2 MB |
| lookups | 10,000,000 | 672 ms | 572 ms | 1.64 s | 180.7 MB | 257.6 MB | 145.9 MB |

Relative to C (below 1× is faster or smaller, above is slower or bigger):

| Program | What it measures | cabby -c time | rust time | cabby -c memory | rust memory |
|---|---|---|---|---|---|
| hello | start-up and baseline memory | 1.16× | 1.15× | 1.35× | 1.49× |
| fib | function calls (recursive fib) | 1.74× | 2.03× | 1.00× | 1.21× |
| sieve | loops and arrays (primes) | 0.99× | 0.97× | 1.00× | 1.01× |
| mandelbrot | floating point | 0.95× | 0.80× | 1.05× | 1.18× |
| sort | quicksort, arrays passed by borrow | 1.06× | 1.13× | 1.00× | 1.01× |
| strings | building and reading text | 1.18× | 0.50× | 1.00× | 1.00× |
| objects | making objects, virtual calls | 0.15× | 1.39× | 1.10× | 1.24× |
| text | string tools: trim, find, slice, replace | 1.10× | 0.95× | 1.09× | 1.18× |
| lists | lists: add, go through, find, split and join | 1.10× | 1.07× | 1.00× | 1.01× |
| lookups | lookups: count by key, go through, remove | 1.17× | 2.86× | 0.70× | 0.57× |
