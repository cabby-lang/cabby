#include <stdio.h>
static int xs[@N@];
static void quicksort(int *a, int lo, int hi) {
    if (lo >= hi) return;
    int pivot = a[(lo + hi) / 2], i = lo, j = hi;
    while (i <= j) {
        while (a[i] < pivot) i++;
        while (a[j] > pivot) j--;
        if (i <= j) { int t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
    }
    quicksort(a, lo, j);
    quicksort(a, i, hi);
}
int main(void) {
    int n = @N@;
    long seed = 42;
    for (int i = 0; i < n; i++) {
        seed = (seed * 1103515245 + 12345) % 2147483648L;
        xs[i] = (int)(seed % 1000000);
    }
    quicksort(xs, 0, n - 1);
    long check = 0;
    for (int i = 0; i < n; i++) check = (check * 31 + xs[i]) % 1000000007;
    printf("%ld\n", check);
    return 0;
}
