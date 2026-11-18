#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    int n = @N@;
    size_t len = 0, cap = 16;
    char *s = malloc(cap);
    char num[24];
    for (int i = 0; i < n; i++) {
        int k = snprintf(num, sizeof num, "%d", i);
        if (len + k + 2 > cap) { while (len + k + 2 > cap) cap *= 2; s = realloc(s, cap); }
        memcpy(s + len, num, k);
        len += k;
        s[len++] = ' ';
    }
    long sevens = 0;
    for (size_t i = 0; i < len; i++)
        if (s[i] == '7') sevens++;
    printf("%zu %ld\n", len, sevens);
    free(s);
    return 0;
}
