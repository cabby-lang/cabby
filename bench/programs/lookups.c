#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Open addressing with linear probing, as a plain C program would write it:
 * one table for int keys and one for string keys, growing at half full. */
enum { EMPTY, USED, GONE };

typedef struct { int *keys, *vals; unsigned char *state; size_t cap, used, len; } itable;
static size_t ihash(int k) { return (size_t)((uint64_t)(uint32_t)k * 0x9E3779B97F4A7C15ULL >> 32); }
static void iput_raw(itable *t, int k, int v);
static void igrow(itable *t) {
    itable o = *t;
    t->cap = o.cap ? o.cap * 2 : 16;
    t->keys = malloc(t->cap * sizeof(int)); t->vals = malloc(t->cap * sizeof(int));
    t->state = calloc(t->cap, 1); t->used = t->len = 0;
    for (size_t i = 0; i < o.cap; i++) if (o.state[i] == USED) iput_raw(t, o.keys[i], o.vals[i]);
    free(o.keys); free(o.vals); free(o.state);
}
static long ifind(itable *t, int k) {
    if (!t->cap) return -1;
    for (size_t i = ihash(k) & (t->cap - 1);; i = (i + 1) & (t->cap - 1)) {
        if (t->state[i] == EMPTY) return -1;
        if (t->state[i] == USED && t->keys[i] == k) return (long)i;
    }
}
static void iput_raw(itable *t, int k, int v) {
    size_t i = ihash(k) & (t->cap - 1);
    while (t->state[i] == USED) i = (i + 1) & (t->cap - 1);
    if (t->state[i] == EMPTY) t->used++;
    t->keys[i] = k; t->vals[i] = v; t->state[i] = USED; t->len++;
}
static void iput(itable *t, int k, int v) {
    long f = ifind(t, k);
    if (f >= 0) { t->vals[f] = v; return; }
    if ((t->used + 1) * 2 > t->cap) igrow(t);
    iput_raw(t, k, v);
}

typedef struct { char **keys; int *vals; unsigned char *state; size_t cap, used, len; } stable;
static size_t shash(const char *s) { uint64_t h = 1469598103934665603ULL; for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; } return (size_t)h; }
static void sput_raw(stable *t, char *k, int v) {
    size_t i = shash(k) & (t->cap - 1);
    while (t->state[i] == USED) i = (i + 1) & (t->cap - 1);
    if (t->state[i] == EMPTY) t->used++;
    t->keys[i] = k; t->vals[i] = v; t->state[i] = USED; t->len++;
}
static void sgrow(stable *t) {
    stable o = *t;
    t->cap = o.cap ? o.cap * 2 : 16;
    t->keys = malloc(t->cap * sizeof(char *)); t->vals = malloc(t->cap * sizeof(int));
    t->state = calloc(t->cap, 1); t->used = t->len = 0;
    for (size_t i = 0; i < o.cap; i++) if (o.state[i] == USED) sput_raw(t, o.keys[i], o.vals[i]);
    free(o.keys); free(o.vals); free(o.state);
}
static long sfind(stable *t, const char *k) {
    if (!t->cap) return -1;
    for (size_t i = shash(k) & (t->cap - 1);; i = (i + 1) & (t->cap - 1)) {
        if (t->state[i] == EMPTY) return -1;
        if (t->state[i] == USED && strcmp(t->keys[i], k) == 0) return (long)i;
    }
}

int main(void) {
    int n = @N@, half = n / 2;
    itable counts = { 0 };
    for (int i = 0; i < n; i++) {
        int k = i * 7 % half;
        long f = ifind(&counts, k);
        if (f >= 0) counts.vals[f] += 1;
        else iput(&counts, k, 1);
    }
    long sum = 0;
    for (size_t i = 0; i < counts.cap; i++)
        if (counts.state[i] == USED) sum += (long)counts.keys[i] * counts.vals[i];
    for (int i = 0; i < half / 2; i++) {
        long f = ifind(&counts, i * 2);
        if (f >= 0) { counts.state[f] = GONE; counts.len--; }
    }
    stable words = { 0 };
    char buf[32];
    for (int i = 0; i < n / 10; i++) {
        snprintf(buf, sizeof buf, "w%d", i % 1000);
        long f = sfind(&words, buf);
        if (f >= 0) words.vals[f] += 1;
        else {
            if ((words.used + 1) * 2 > words.cap) sgrow(&words);
            sput_raw(&words, strdup(buf), 1);
        }
    }
    printf("%zu %ld %zu %d\n", counts.len, sum, words.len, words.vals[sfind(&words, "w7")]);
    for (size_t i = 0; i < words.cap; i++) if (words.state[i] == USED) free(words.keys[i]);
    free(counts.keys); free(counts.vals); free(counts.state);
    free(words.keys); free(words.vals); free(words.state);
    return 0;
}
