#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* A growable array, doubling when full, as the other languages' lists do. */
int main(void) {
    int n = @N@;
    size_t len = 0, cap = 4;
    int *values = malloc(cap * sizeof *values);
    for (int i = 0; i < n; i++) {
        if (len == cap) { cap *= 2; values = realloc(values, cap * sizeof *values); }
        values[len++] = i * 7 % 1000;
    }
    long sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += values[i];
    long found = -1;
    for (size_t i = 0; i < len; i++)
        if (values[i] == 999) { found = (long)i; break; }
    const char *words[] = { "alpha", "beta", "gamma", "delta", "epsilon" };
    long letters = 0;
    for (int i = 0; i < n / 100; i++) {
        size_t total = 4;
        for (int k = 0; k < 5; k++) total += strlen(words[k]);
        char *joined = malloc(total + 1), *o = joined;
        for (int k = 0; k < 5; k++) {
            if (k) *o++ = ',';
            size_t w = strlen(words[k]);
            memcpy(o, words[k], w);
            o += w;
        }
        *o = 0;
        letters += (long)strlen(joined);
        free(joined);
    }
    printf("%zu %ld %ld %ld\n", len, sum, found, letters);
    free(values);
    return 0;
}
