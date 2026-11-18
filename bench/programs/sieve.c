#include <stdio.h>
#include <stdbool.h>
static bool composite[@N1@];
int main(void) {
    int n = @N@, count = 0;
    for (int i = 2; i <= n; i++)
        if (!composite[i]) {
            count++;
            for (long j = (long)i * i; j <= n; j += i) composite[j] = true;
        }
    printf("%d\n", count);
    return 0;
}
