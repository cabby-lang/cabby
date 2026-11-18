#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each step makes a new string, as the other languages do. */
static char *slice(const char *s, size_t a, size_t n) {
    char *p = malloc(n + 1);
    memcpy(p, s + a, n);
    p[n] = 0;
    return p;
}

static char *replace(const char *s, const char *from, const char *to) {
    size_t fn = strlen(from), tn = strlen(to), count = 0;
    for (const char *p = s; (p = strstr(p, from)); p += fn) count++;
    size_t n = strlen(s) + count * (tn - fn);
    char *r = malloc(n + 1), *o = r;
    const char *p = s, *q;
    while ((q = strstr(p, from))) {
        memcpy(o, p, q - p); o += q - p;
        memcpy(o, to, tn); o += tn;
        p = q + fn;
    }
    strcpy(o, p);
    return r;
}

int main(void) {
    int n = @N@;
    long total = 0, hits = 0, sevens = 0, smaller = 0;
    for (int i = 0; i < n; i++) {
        char num[24];
        snprintf(num, sizeof num, "%d", i);
        size_t ln = strlen(num);
        char *line = malloc(ln + 40);
        strcpy(line, "  item "); strcat(line, num); strcat(line, ": the cat sat on the mat  ");
        size_t a = 0, b = strlen(line);
        while (a < b && line[a] == ' ') a++;
        while (b > a && line[b - 1] == ' ') b--;
        char *trimmed = slice(line, a, b - a);
        size_t colon = strchr(trimmed, ':') - trimmed;
        char *name = slice(trimmed, 0, colon);
        char *rest = slice(trimmed, colon + 2, strlen(trimmed) - colon - 2);
        if (strstr(rest, "cat")) hits++;
        char *changed = replace(rest, "cat", "dog");
        total += strlen(changed) + strlen(name);
        char *up = slice(name, 0, strlen(name));
        for (char *p = up; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
        if (strncmp(up, "ITEM 7", 6) == 0) sevens++;
        if (strcmp(name, "item 5") < 0) smaller++;
        free(line); free(trimmed); free(name); free(rest); free(changed); free(up);
    }
    printf("%ld %ld %ld %ld\n", total, hits, sevens, smaller);
    return 0;
}
